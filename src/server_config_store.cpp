// =============================================================================
// Aevrix - Runtime Configuration Store Implementation (Phase 24)
// =============================================================================
// See include/aevrix/server_config_store.h for the design notes.
// =============================================================================

#include "aevrix/server_config_store.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "aevrix/config_parser.h"
#include "aevrix/logger.h"

namespace aevrix {

namespace {

std::string joined(const std::vector<std::string>& values, const std::string& separator = ",") {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += values[i];
    }
    return out;
}

std::string bool_str(bool value) {
    return value ? "true" : "false";
}

/// Secrets are compared but never rendered in clear text.
std::string masked(const std::string& secret) {
    return secret.empty() ? std::string("(unset)") : "(set:" + std::to_string(secret.size()) + ")";
}

/// Changed keys plus a human rendering of each change ("key: old -> new").
struct ChangeSet {
    std::vector<std::string> keys;
    std::vector<std::string> entries;
};

ChangeSet diff_entries(const ServerConfig& old_config, const ServerConfig& new_config) {
    // flatten() is deterministic and key-ordered, so a positional walk is
    // enough (and keeps the log output stable for tests).
    const auto old_fields = ServerConfigStore::flatten(old_config);
    const auto new_fields = ServerConfigStore::flatten(new_config);

    ChangeSet changes;
    const size_t count = std::min(old_fields.size(), new_fields.size());
    for (size_t i = 0; i < count; ++i) {
        if (old_fields[i].first != new_fields[i].first) {
            // Key sets diverged: a flatten() maintenance bug, not a config
            // problem. Stop rather than report nonsense changes.
            break;
        }
        if (old_fields[i].second != new_fields[i].second) {
            changes.keys.push_back(old_fields[i].first);
            changes.entries.push_back(old_fields[i].first + ": " + old_fields[i].second + " -> " +
                                     new_fields[i].second);
        }
    }
    return changes;
}

} // namespace

ServerConfigStore::ServerConfigStore(std::shared_ptr<const ServerConfig> initial,
                                     std::string source_path)
    : active_(std::move(initial))
    , source_path_(std::move(source_path)) {
    if (!active_.load()) {
        throw std::invalid_argument("ServerConfigStore requires a non-null initial config");
    }
}

std::shared_ptr<const ServerConfig> ServerConfigStore::snapshot() const {
    return active_.load();
}

std::string ServerConfigStore::source_path() const {
    std::lock_guard<std::mutex> lock(meta_mutex_);
    return source_path_;
}

void ServerConfigStore::set_source_path(std::string path) {
    std::lock_guard<std::mutex> lock(meta_mutex_);
    source_path_ = std::move(path);
}

std::string ServerConfigStore::last_error() const {
    std::lock_guard<std::mutex> lock(meta_mutex_);
    return last_error_;
}

ConfigReloadOutcome ServerConfigStore::publish(std::shared_ptr<const ServerConfig> candidate,
                                              const std::string& label,
                                              std::string remember_path) {
    const std::shared_ptr<const ServerConfig> previous = active_.load();

    // The single atomic store that makes the new generation active. `previous`
    // stays alive because this function holds a reference, so any reader that
    // snapshotted it earlier is unaffected by the swap.
    active_.store(std::move(candidate));
    const uint64_t applied_generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;

    ConfigReloadOutcome outcome;
    outcome.success = true;
    outcome.generation = applied_generation;
    outcome.snapshot = active_.load();

    const ChangeSet changes = diff_entries(*previous, *outcome.snapshot);
    outcome.changes = changes.keys;
    for (const std::string& key : changes.keys) {
        if (is_restart_required(key)) {
            outcome.restart_required.push_back(key);
        } else {
            outcome.applied_hot.push_back(key);
        }
    }

    if (!changes.entries.empty()) {
        outcome.message = " changed=[" + joined(changes.entries, ", ") + "]";
    } else {
        // Explicit no-op wording so callers can distinguish "nothing changed"
        // from "the reload failed".
        outcome.message = "no values changed";
    }

    {
        std::lock_guard<std::mutex> lock(meta_mutex_);
        if (!remember_path.empty()) {
            source_path_ = std::move(remember_path);
        }
        last_error_.clear();
    }
    ++reload_success_count_;

    g_logger.info("Config reload applied from " + label + " (generation " +
                  std::to_string(applied_generation) + ")" +
                  (outcome.changes.empty() ? ": no values changed" : ":" + outcome.message));

    if (!outcome.applied_hot.empty()) {
        g_logger.info("Config reload takes effect immediately: " + joined(outcome.applied_hot));
    }
    if (!outcome.restart_required.empty()) {
        g_logger.warn("Config reload needs a RESTART to take effect (running values unchanged): " +
                      joined(outcome.restart_required));
    }
    return outcome;
}

ConfigReloadOutcome ServerConfigStore::try_reload_from_parser(const ConfigParser& parser,
                                                             const std::string& label,
                                                             std::string remember_path) {
    ConfigReloadOutcome outcome;

    // Build and validate the candidate entirely off to the side. Nothing here
    // touches active_, so a throw leaves the running configuration untouched.
    std::shared_ptr<ServerConfig> candidate = std::make_shared<ServerConfig>();
    try {
        candidate->load_from_parser(parser);
#ifdef AEVRIX_ENABLE_TLS
        if (candidate->tls_enabled()) {
            // Reject early rather than publish a configuration whose TLS
            // listener could never come up after a restart.
            std::ifstream cert(candidate->tls_cert_file());
            std::ifstream key(candidate->tls_key_file());
            if (!cert.good() || !key.good()) {
                throw std::runtime_error("TLS enabled but certificate/key unreadable (cert='" +
                                         candidate->tls_cert_file() + "' key='" +
                                         candidate->tls_key_file() + "')");
            }
        }
#endif
    } catch (const std::exception& e) {
        outcome.success = false;
        outcome.message = std::string("config reload REJECTED (") + label + "), keeping " +
                          "generation " + std::to_string(generation()) + ": " + e.what();
        outcome.generation = generation();
        ++reload_failure_count_;
        {
            std::lock_guard<std::mutex> lock(meta_mutex_);
            last_error_ = e.what();
        }
        g_logger.warn(outcome.message);
        return outcome;
    }

    return publish(std::move(candidate), label, std::move(remember_path));
}

ConfigReloadOutcome ServerConfigStore::try_reload(const std::string& path) {
    const std::string resolved = path.empty() ? source_path() : path;

    if (resolved.empty()) {
        ConfigReloadOutcome outcome;
        outcome.message = "config reload REJECTED: no configuration file path known "
                          "(start with -c/--config or pass a path)";
        outcome.generation = generation();
        ++reload_failure_count_;
        {
            std::lock_guard<std::mutex> lock(meta_mutex_);
            last_error_ = outcome.message;
        }
        g_logger.warn(outcome.message);
        return outcome;
    }

    // Explicit probe so a missing/unreadable file names the file instead of
    // surfacing a generic parser error.
    {
        std::ifstream probe(resolved);
        if (!probe.good()) {
            ConfigReloadOutcome outcome;
            outcome.message = "config reload REJECTED: cannot open configuration file '" + resolved +
                              "', keeping generation " + std::to_string(generation());
            outcome.generation = generation();
            ++reload_failure_count_;
            {
                std::lock_guard<std::mutex> lock(meta_mutex_);
                last_error_ = outcome.message;
            }
            g_logger.warn(outcome.message);
            return outcome;
        }
    }

    ConfigReloadOutcome outcome;
    try {
        ConfigParser parser;
        parser.parse_file(resolved);
        return try_reload_from_parser(parser, "path=" + resolved, resolved);
    } catch (const std::exception& e) {
        outcome.message = std::string("config reload REJECTED (path=") + resolved + "), keeping " +
                          "generation " + std::to_string(generation()) + ": " + e.what();
        outcome.generation = generation();
        ++reload_failure_count_;
        {
            std::lock_guard<std::mutex> lock(meta_mutex_);
            last_error_ = e.what();
        }
        g_logger.warn(outcome.message);
        return outcome;
    }
}

std::vector<std::pair<std::string, std::string>> ServerConfigStore::flatten(const ServerConfig& config) {
    return {
        {"host", config.host()},
        {"port", std::to_string(config.port())},
        {"workers", std::to_string(config.workers())},
        {"document_root", config.document_root()},
        {"header_timeout_ms", std::to_string(config.header_timeout_ms())},
        {"body_timeout_ms", std::to_string(config.body_timeout_ms())},
        {"keep_alive_timeout_ms", std::to_string(config.keep_alive_timeout_ms())},
        {"write_timeout_ms", std::to_string(config.write_timeout_ms())},
        {"max_connections", std::to_string(config.max_connections())},
        {"max_buffer_size", std::to_string(config.max_buffer_size())},
        {"max_request_body", std::to_string(config.max_request_body())},
        {"log_level", config.log_level()},
        {"admin_api_enabled", bool_str(config.admin_api_enabled())},
        {"admin_token", masked(config.admin_token())},
        {"tls_enabled", bool_str(config.tls_enabled())},
        {"tls_port", std::to_string(config.tls_port())},
        {"tls_cert_file", config.tls_cert_file()},
        {"tls_key_file", config.tls_key_file()},
        {"tls_min_version", config.tls_min_version()},
        {"tls_max_version", config.tls_max_version()},
        {"proxy_enabled", bool_str(config.proxy_enabled())},
        {"proxy_pass", config.proxy_pass()},
        {"proxy_prefix", config.proxy_prefix()},
        {"proxy_strip_prefix", bool_str(config.proxy_strip_prefix())},
        {"proxy_connect_timeout_ms", std::to_string(config.proxy_connect_timeout_ms())},
        {"proxy_read_timeout_ms", std::to_string(config.proxy_read_timeout_ms())},
        {"proxy_max_idle_connections", std::to_string(config.proxy_max_idle_connections())},
        {"proxy_max_response_bytes", std::to_string(config.proxy_max_response_bytes())},
        {"proxy_idle_timeout_ms", std::to_string(config.proxy_idle_timeout_ms())},
        {"websocket_enabled", bool_str(config.websocket_enabled())},
        {"websocket_paths", joined(config.websocket_allowed_paths())},
        {"websocket_max_message_bytes", std::to_string(config.websocket_max_message_bytes())},
        {"websocket_close_timeout_ms", std::to_string(config.websocket_close_timeout_ms())},
        {"websocket_ping_interval_ms", std::to_string(config.websocket_ping_interval_ms())},
        {"websocket_allowed_origins", joined(config.websocket_allowed_origins())},
    };
}

std::vector<std::string> ServerConfigStore::diff_keys(const ServerConfig& old_config,
                                                     const ServerConfig& new_config) {
    return diff_entries(old_config, new_config).keys;
}

bool ServerConfigStore::is_restart_required(const std::string& key) {
    // Values captured once at startup: listener sockets, TLS contexts and the
    // worker-thread pool, plus the upstream routing decisions held by the
    // long-lived ProxyHandler. Everything else is read from a fresh snapshot
    // per request or per maintenance pass and therefore applies immediately.
    static const std::vector<std::string> restart_keys = {
        "host", "port", "workers",
        "tls_enabled", "tls_port", "tls_cert_file", "tls_key_file",
        "tls_min_version", "tls_max_version",
        "proxy_enabled", "proxy_pass", "proxy_prefix",
    };
    for (const std::string& candidate : restart_keys) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

std::string ServerConfigStore::describe(const ConfigReloadOutcome& outcome) {
    std::ostringstream oss;
    oss << (outcome.success ? "applied" : "rejected") << " generation=" << outcome.generation;
    if (!outcome.changes.empty()) {
        oss << " changed=[" << joined(outcome.changes) << "]";
    }
    if (!outcome.applied_hot.empty()) {
        oss << " now=[" << joined(outcome.applied_hot) << "]";
    }
    if (!outcome.restart_required.empty()) {
        oss << " restart=[" << joined(outcome.restart_required) << "]";
    }
    if (!outcome.success && !outcome.message.empty()) {
        oss << " error=\"" << outcome.message << "\"";
    }
    return oss.str();
}

} // namespace aevrix
