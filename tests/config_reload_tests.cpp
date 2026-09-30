// =============================================================================
// Aevrix - Configuration Reload Tests (Phase 24)
// =============================================================================
// Unit tests for the atomic configuration store that backs SIGHUP reloads:
// - atomic publish/swap semantics (no partial state, old generations stay
//   valid for as long as a reader holds them)
// - all-or-nothing validation (a bad config never degrades the live one)
// - hot vs restart-required change classification
// - concurrency: readers snapshotting while reloads happen (ThreadSanitizer)
// - the signal handler's flag/eventfd wake-up path
//
// The tests avoid the network entirely: reloads are driven through
// ServerConfigStore::try_reload_from_parser() and through temporary files.
// =============================================================================

#include "aevrix/config_parser.h"
#include "aevrix/connection_manager.h"
#include "aevrix/logger.h"
#include "aevrix/server_config.h"
#include "aevrix/server_config_store.h"
#include "aevrix/signal_handler.h"

#include <atomic>
#include <cassert>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#endif

using namespace aevrix;

namespace {

// =============================================================================
// Test helpers
// =============================================================================

/// Absolute, writable scratch paths (no directories to create).
std::string scratch_path(const std::string& name) {
    return "/tmp/aevrix-phase24-" + name + ".conf";
}

bool write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::trunc);
    out << contents;
    return out.good();
}

void remove_file(const std::string& path) {
    std::remove(path.c_str());
}

/// Build a parser from "key = value" lines (the in-memory reload seam).
///
/// ConfigParser::parse_line() is private, so the lines go through a scratch
/// file: the store then receives an already-parsed object, which keeps reload
/// tests independent of the store's own path resolution and of file probing.
ConfigParser parser_from(const std::vector<std::string>& lines) {
    static std::atomic<int> sequence{0};
    const std::string path =
        scratch_path("parser-" + std::to_string(sequence.fetch_add(1)));

    std::ofstream out(path, std::ios::trunc);
    for (const std::string& line : lines) {
        out << line << "\n";
    }
    out.close();

    ConfigParser parser;
    parser.parse_file(path);
    remove_file(path);
    return parser;
}

/// A startup config with a non-default shape so reload diffs are meaningful.
ServerConfig startup_config() {
    ServerConfig config;
    config.set_host("127.0.0.1");
    config.set_port(18080);
    config.set_workers(2);
    config.set_document_root("./public");
    config.set_keep_alive_timeout_ms(5000);
    config.set_header_timeout_ms(10000);
    config.set_body_timeout_ms(30000);
    config.set_log_level("info");
    return config;
}

std::shared_ptr<ServerConfigStore> make_store(ServerConfig config,
                                             const std::string& path = "") {
    return std::make_shared<ServerConfigStore>(
        std::make_shared<ServerConfig>(std::move(config)), path);
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    for (const std::string& value : values) {
        if (value == needle) {
            return true;
        }
    }
    return false;
}

/// Key part of a "key = value" line (used to apply overrides).
std::string key_of(const std::string& line) {
    const size_t equals = line.find('=');
    std::string key = (equals == std::string::npos) ? line : line.substr(0, equals);
    const size_t first = key.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return key;
    }
    const size_t last = key.find_last_not_of(" \t");
    return key.substr(first, last - first + 1);
}

/// A complete configuration file whose values match startup_config(), so a
/// reload of the same key set produces an empty change set.
std::vector<std::string> baseline_lines() {
    return {
        "host = 127.0.0.1",
        "port = 18080",
        "workers = 2",
        "document_root = ./public",
        "header_timeout_ms = 10000",
        "body_timeout_ms = 30000",
        "keep_alive_timeout_ms = 5000",
        "write_timeout_ms = 30000",
        "max_connections = 1000",
        "max_buffer_size = 65536",
        "max_request_body = 10485760",
        "log_level = info",
        "admin_api_enabled = false",
    };
}

/// baseline_lines() with the listed "key = value" lines overriding by key.
std::vector<std::string> lines_with(const std::vector<std::string>& overrides) {
    std::vector<std::string> lines = baseline_lines();
    for (const std::string& override_line : overrides) {
        const std::string key = key_of(override_line);
        bool replaced = false;
        for (std::string& line : lines) {
            if (key_of(line) == key) {
                line = override_line;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            lines.push_back(override_line);
        }
    }
    return lines;
}

} // namespace

// =============================================================================
// Publish / snapshot semantics
// =============================================================================

void test_initial_generation_and_snapshot() {
    std::cout << "Testing initial publish and snapshot..." << std::endl;

    auto store = make_store(startup_config(), "/etc/aevrix/aevrix.conf");

    const auto first = store->snapshot();
    assert(first != nullptr && "Snapshot must never be null");
    assert(first->port() == 18080 && "Snapshot must carry the startup values");
    assert(store->generation() == 1 && "Startup is generation 1");
    assert(store->source_path() == "/etc/aevrix/aevrix.conf" && "Source path must be kept");
    assert(store->reload_success_count() == 0 && "No reloads yet");
    assert(store->reload_failure_count() == 0 && "No failures yet");

    // Every snapshot call returns the same generation until a reload swaps it.
    const auto second = store->snapshot();
    assert(second.get() == first.get() && "Same generation returns the same object");

    std::cout << "  PASSED" << std::endl;
}

void test_old_snapshot_survives_reload() {
    std::cout << "Testing that a held snapshot survives a reload..." << std::endl;

    auto store = make_store(startup_config());
    const std::shared_ptr<const ServerConfig> before = store->snapshot();

    const ConfigReloadOutcome outcome =
        store->try_reload_from_parser(parser_from({"keep_alive_timeout_ms = 1234"}));
    assert(outcome.success && "Valid reload must succeed");
    assert(outcome.generation == 2 && "First reload is generation 2");
    assert(store->generation() == 2 && "Store reports the new generation");

    // The reader that grabbed the previous generation keeps its values: this is
    // the in-flight request guarantee, and a use-after-free would trip ASan.
    assert(before->keep_alive_timeout_ms() == 5000 && "Old snapshot must keep old values");
    assert(store->snapshot()->keep_alive_timeout_ms() == 1234 &&
           "New snapshot must carry the new value");
    assert(outcome.snapshot->keep_alive_timeout_ms() == 1234 &&
           "Outcome must expose the newly published config");

    std::cout << "  PASSED" << std::endl;
}

void test_reload_classifies_hot_and_restart_changes() {
    std::cout << "Testing hot vs restart-required classification..." << std::endl;

    auto store = make_store(startup_config());

    // Hot: read from a fresh snapshot per request/tick.
    const ConfigReloadOutcome hot = store->try_reload_from_parser(parser_from(lines_with({
        "keep_alive_timeout_ms = 250",
        "log_level = debug",
        "max_request_body = 2048",
    })));
    assert(hot.success && "Hot reload must succeed");
    assert(contains(hot.changes, "keep_alive_timeout_ms") && "Change must be detected");
    assert(contains(hot.changes, "log_level") && "log_level change must be detected");
    assert(contains(hot.applied_hot, "keep_alive_timeout_ms") &&
           "Timeouts apply immediately");
    assert(contains(hot.applied_hot, "log_level") && "Log level applies immediately");
    assert(hot.restart_required.empty() && "Nothing here needs a restart");

    // Restart-required: captured once at startup (socket, pool, TLS context).
    // The hot keys keep their just-reloaded values, so the change set is exactly
    // the two restart-only keys.
    const ConfigReloadOutcome cold = store->try_reload_from_parser(parser_from(lines_with({
        "keep_alive_timeout_ms = 250",
        "log_level = debug",
        "max_request_body = 2048",
        "port = 18081",
        "workers = 9",
    })));
    assert(cold.success && "Swap still happens for restart-only changes");
    assert(contains(cold.restart_required, "port") && "Port needs a restart");
    assert(contains(cold.restart_required, "workers") && "Worker count needs a restart");
    assert(cold.applied_hot.empty() && "No hot keys in this change set");

    // No changes at all is a successful no-op, not a failure.
    const ConfigReloadOutcome same = store->try_reload_from_parser(parser_from(lines_with({
        "keep_alive_timeout_ms = 250",
        "log_level = debug",
        "max_request_body = 2048",
        "port = 18081",
        "workers = 9",
    })));
    assert(same.success && "Reloading identical values succeeds");
    assert(same.changes.empty() && "Identical values produce no change set");
    assert(same.message.find("no values changed") != std::string::npos &&
           "No-op reloads say so");
    assert(store->generation() == same.generation && "Generation still advances per attempt");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// All-or-nothing validation
// =============================================================================

void test_invalid_value_is_rejected_without_side_effects() {
    std::cout << "Testing rejection of invalid values..." << std::endl;

    auto store = make_store(startup_config());
    const std::shared_ptr<const ServerConfig> before = store->snapshot();

    // workers = 0 is rejected by ServerConfig::load_from_parser.
    ConfigReloadOutcome outcome =
        store->try_reload_from_parser(parser_from(lines_with({"workers = 0"})));
    assert(!outcome.success && "Invalid worker count must be rejected");
    assert(outcome.message.find("Invalid workers") != std::string::npos &&
           "Rejection must name the offending key");
    assert(outcome.generation == 1 && "Rejected reload must not advance the generation");
    assert(store->snapshot().get() == before.get() && "Active config must be untouched");
    assert(store->reload_failure_count() == 1 && "Failure must be counted");
    assert(store->reload_success_count() == 0 && "No successful reload happened");
    assert(!store->last_error().empty() && "Last error must be recorded");
    assert(store->last_error().find("Invalid workers") != std::string::npos &&
           "Last error must explain the rejection");

    // An unknown log level is rejected too: silent fallback would hide typos.
    outcome = store->try_reload_from_parser(parser_from(lines_with({"log_level = verbose"})));
    assert(!outcome.success && "Unknown log level must be rejected");
    assert(outcome.message.find("log_level") != std::string::npos &&
           "Rejection must mention log_level");
    assert(store->snapshot().get() == before.get() && "Active config must still be untouched");
    assert(store->reload_failure_count() == 2 && "Both failures must be counted");

    // A later good reload clears the error state.
    outcome = store->try_reload_from_parser(parser_from(lines_with({"log_level = warn"})));
    assert(outcome.success && "Valid reload must succeed after failures");
    assert(store->last_error().empty() && "Success must clear the last error");
    assert(store->generation() == 2 && "Generation advances on success");

    std::cout << "  PASSED" << std::endl;
}

void test_missing_file_is_rejected() {
    std::cout << "Testing rejection of a missing configuration file..." << std::endl;

    const std::string missing = scratch_path("does-not-exist");
    remove_file(missing);

    auto store = make_store(startup_config(), missing);
    const std::shared_ptr<const ServerConfig> before = store->snapshot();

    const ConfigReloadOutcome outcome = store->try_reload();
    assert(!outcome.success && "Missing file must be rejected");
    assert(outcome.message.find(missing) != std::string::npos &&
           "Rejection must name the missing file");
    assert(store->snapshot().get() == before.get() && "Active config must be untouched");
    assert(store->generation() == 1 && "Generation must not advance");
    assert(store->reload_failure_count() == 1 && "Failure must be counted");

    // A store without any known path cannot reload either.
    auto pathless = make_store(startup_config());
    const ConfigReloadOutcome no_path = pathless->try_reload();
    assert(!no_path.success && "No configured path must be reported, not guessed");
    assert(no_path.message.find("no configuration file path") != std::string::npos &&
           "Rejection must explain the missing path");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// File-driven reload (the SIGHUP path, minus the signal)
// =============================================================================

void test_file_reload_applies_and_updates_source_path() {
    std::cout << "Testing file-driven reload..." << std::endl;

    const std::string path = scratch_path("hot");
    const std::string original =
        "# Aevrix test configuration\n"
        "host = 127.0.0.1\n"
        "port = 18080\n"
        "workers = 2\n"
        "keep_alive_timeout_ms = 5000   # trailing comment\n"
        "log_level = info\n";
    assert(write_file(path, original) && "Scratch config must be writable");

    auto store = make_store(startup_config(), path);

    const std::string updated =
        "# reloaded with a different keep-alive budget\n"
        "host = 127.0.0.1\n"
        "port = 18080\n"
        "workers = 2\n"
        "keep_alive_timeout_ms = 750\n"
        "log_level = debug\n";
    assert(write_file(path, updated) && "Updated config must be writable");

    const ConfigReloadOutcome outcome = store->try_reload();
    assert(outcome.success && "Reload of a valid file must succeed");
    assert(outcome.generation == 2 && "Generation must advance");
    assert(store->source_path() == path && "Source path must track the reloaded file");
    assert(contains(outcome.changes, "keep_alive_timeout_ms") &&
           "Changed key must be reported");
    assert(contains(outcome.applied_hot, "keep_alive_timeout_ms") &&
           "Timeout changes are hot");
    assert(contains(outcome.applied_hot, "log_level") && "Log level changes are hot");
    assert(outcome.restart_required.empty() && "Nothing here needs a restart");
    assert(store->reload_success_count() == 1 && "Success must be counted");
    assert(store->snapshot()->keep_alive_timeout_ms() == 750 &&
           "Published config must carry the new value");

    // A broken file at the same path must not disturb the running config.
    assert(write_file(path, "host = 127.0.0.1\nport = 18080\nworkers = 0\n") &&
           "Broken config must be writable");
    const ConfigReloadOutcome broken = store->try_reload();
    assert(!broken.success && "Invalid file must be rejected");
    assert(store->generation() == 2 && "Generation must not advance on rejection");
    assert(store->snapshot()->keep_alive_timeout_ms() == 750 &&
           "Running config must keep serving with the old values");
    assert(store->reload_failure_count() == 1 && "Failure must be counted");

    remove_file(path);
    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Concurrency: readers must never observe a half-applied configuration
// =============================================================================

void test_concurrent_snapshots_during_reloads() {
    std::cout << "Testing concurrent snapshots during reloads..." << std::endl;

    // The paired values start equal (unlike the default config), so every
    // generation - including generation 1 - is internally consistent.
    ServerConfig paired = startup_config();
    paired.set_header_timeout_ms(1);
    paired.set_body_timeout_ms(1);
    auto store = make_store(paired);

    constexpr int kReloads = 300;
    std::atomic<bool> stop{false};
    std::atomic<int> torn_reads{0};
    std::atomic<int> reader_iterations{0};

    // The reload writer pairs two values in every generation. If a reader ever
    // saw header != body, the swap would not be atomic.
    std::thread writer([&store]() {
        for (int generation = 1; generation <= kReloads; ++generation) {
            const std::vector<std::string> lines = lines_with({
                "header_timeout_ms = " + std::to_string(generation),
                "body_timeout_ms = " + std::to_string(generation),
            });
            const ConfigReloadOutcome outcome =
                store->try_reload_from_parser(parser_from(lines));
            assert(outcome.success && "Concurrent reload must succeed");
        }
    });

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&store, &stop, &torn_reads, &reader_iterations]() {
            while (!stop.load(std::memory_order_acquire)) {
                const std::shared_ptr<const ServerConfig> snapshot = store->snapshot();
                if (snapshot->header_timeout_ms() != snapshot->body_timeout_ms()) {
                    torn_reads.fetch_add(1, std::memory_order_relaxed);
                }
                reader_iterations.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    writer.join();
    stop.store(true, std::memory_order_release);
    for (std::thread& reader : readers) {
        reader.join();
    }

    assert(torn_reads.load() == 0 && "Readers must never see a partially applied config");
    assert(reader_iterations.load() > 0 && "Readers must have actually run");
    assert(store->generation() == static_cast<uint64_t>(kReloads) + 1 &&
           "Every reload must have produced a generation");
    assert(store->reload_success_count() == static_cast<uint64_t>(kReloads) &&
           "Every reload must be counted");

    std::cout << "  PASSED (" << reader_iterations.load() << " concurrent snapshots)"
              << std::endl;
}

// =============================================================================
// Consumers of the store
// =============================================================================

void test_connection_manager_follows_reloads() {
    std::cout << "Testing that ConnectionManager follows reloads..." << std::endl;

    ServerConfig startup = startup_config();
    startup.set_max_connections(1);
    auto store = make_store(startup);

    {
        ConnectionManager manager(&startup, store.get());

        auto first = manager.register_connection(10);
        assert(first != nullptr && "First connection fits under the startup limit");
        assert(manager.at_capacity() && "One connection reaches a limit of one");
        assert(manager.register_connection(11) == nullptr &&
               "Second connection must be rejected while at capacity");

        // Raise the limit through the store: the manager reads it per use, so
        // no restart and no manager rebuild are needed.
        const ConfigReloadOutcome outcome =
            store->try_reload_from_parser(parser_from({"max_connections = 3"}));
        assert(outcome.success && "Reload must succeed");
        assert(!manager.at_capacity() && "Live limit must be the reloaded one");

        auto second = manager.register_connection(11);
        assert(second != nullptr && "Connection must fit under the reloaded limit");
    }

    // A manager built without a store keeps its startup behaviour (no reload).
    ServerConfig fixed = startup_config();
    fixed.set_max_connections(1);
    ConnectionManager standalone(&fixed);
    assert(standalone.register_connection(20) != nullptr && "Startup limit allows one");
    assert(standalone.register_connection(21) == nullptr && "Startup limit still applies");

    std::cout << "  PASSED" << std::endl;
}

void test_logger_level_configuration() {
    std::cout << "Testing log level parsing..." << std::endl;

    LogLevel parsed = LogLevel::ERR;
    assert(Logger::level_from_string("debug", parsed) && parsed == LogLevel::DEBUG);
    assert(Logger::level_from_string("INFO", parsed) && parsed == LogLevel::INFO);
    assert(Logger::level_from_string("  warn  ", parsed) && parsed == LogLevel::WARN);
    assert(Logger::level_from_string("error", parsed) && parsed == LogLevel::ERR);
    assert(Logger::level_from_string("err", parsed) && parsed == LogLevel::ERR);
    assert(!Logger::level_from_string("verbose", parsed) && "Unknown names must fail");
    assert(!Logger::level_from_string("", parsed) && "Empty names must fail");

    assert(Logger::level_to_string(LogLevel::DEBUG) == "debug");
    assert(Logger::level_to_string(LogLevel::ERR) == "error");

    // The global logger is what the reload path pushes the new level into.
    const LogLevel before = g_logger.level();
    assert(g_logger.set_level_from_string("debug") && g_logger.level() == LogLevel::DEBUG);
    assert(!g_logger.set_level_from_string("nonsense") &&
           "A bad name must not change the level");
    assert(g_logger.level() == LogLevel::DEBUG && "Level must be left as it was");
    g_logger.set_level(before);

    std::cout << "  PASSED" << std::endl;
}

void test_signal_handler_reload_wakeup() {
    std::cout << "Testing signal handler reload flag and wake-up..." << std::endl;

    SignalHandler handler;
    assert(!handler.reload_requested() && "No reload pending initially");
    assert(handler.reload_notify_fd() == -1 && "No wake-up descriptor by default");

    handler.request_reload();
    assert(handler.reload_requested() && "Flag must be set by request_reload()");
    assert(handler.consume_reload_request() && "Pending request must be consumable");
    assert(!handler.consume_reload_request() && "A request is consumed exactly once");
    assert(!handler.reload_requested() && "Flag must be cleared after consumption");

#ifdef __linux__
    // The eventfd path is what makes SIGHUP take effect immediately: the signal
    // handler writes 8 bytes, the event loop drains them.
    const int wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(wake_fd >= 0 && "eventfd must be available on Linux");
    handler.set_reload_notify_fd(wake_fd);
    assert(handler.reload_notify_fd() == wake_fd && "Descriptor must be registered");

    handler.request_reload();
    uint64_t counter = 0;
    assert(::read(wake_fd, &counter, sizeof(counter)) ==
           static_cast<ssize_t>(sizeof(counter)) && "Wake-up must be readable");
    assert(counter >= 1 && "Wake-up must increment the counter");

    handler.set_reload_notify_fd(-1);
    handler.request_reload();  // must not write anywhere / must not crash
    ::close(wake_fd);
#endif

    // Delivering a real SIGHUP must be enough to schedule a reload.
    handler.consume_reload_request();
    ::raise(SIGHUP);
    assert(handler.reload_requested() && "SIGHUP must request a reload");
    assert(handler.consume_reload_request() && "And that request must be consumable");

    std::cout << "  PASSED" << std::endl;
}

void test_flatten_and_diff_helpers() {
    std::cout << "Testing flatten/diff helpers..." << std::endl;

    const ServerConfig base = startup_config();
    const auto fields = ServerConfigStore::flatten(base);

    assert(!fields.empty() && "Every configuration key must be comparable");
    for (size_t i = 0; i < fields.size(); ++i) {
        assert(!fields[i].first.empty() && "Keys must be named");
        for (size_t j = i + 1; j < fields.size(); ++j) {
            assert(fields[i].first != fields[j].first && "flatten() must not repeat a key");
        }
    }

    assert(ServerConfigStore::diff_keys(base, base).empty() &&
           "A config compared to itself has no differences");

    ServerConfig changed = base;
    changed.set_port(19000);
    changed.set_log_level("warn");
    const std::vector<std::string> changes = ServerConfigStore::diff_keys(base, changed);
    assert(changes.size() == 2 && "Exactly the two changed keys must be reported");
    assert(contains(changes, "port") && "Port change must be reported");
    assert(contains(changes, "log_level") && "Log level change must be reported");

    // Secrets are compared but never rendered in clear text.
    ServerConfig with_token = base;
    with_token.set_admin_token("super-secret-token");
    bool token_masked = false;
    for (const auto& field : ServerConfigStore::flatten(with_token)) {
        if (field.first == "admin_token") {
            assert(field.second.find("super-secret-token") == std::string::npos &&
                   "The admin token must not appear in a diff/log rendering");
            token_masked = true;
        }
    }
    assert(token_masked && "admin_token must be part of the comparison");

    // Classification tables.
    assert(ServerConfigStore::is_restart_required("port") && "Port needs a restart");
    assert(ServerConfigStore::is_restart_required("tls_cert_file") &&
           "TLS material needs a restart");
    assert(ServerConfigStore::is_restart_required("workers") &&
           "Worker threads need a restart");
    assert(!ServerConfigStore::is_restart_required("keep_alive_timeout_ms") &&
           "Timeouts are hot");
    assert(!ServerConfigStore::is_restart_required("log_level") && "Log level is hot");

    std::cout << "  PASSED" << std::endl;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "=== Configuration Reload Tests (Phase 24) ===" << std::endl;
    std::cout << std::endl;

    test_initial_generation_and_snapshot();
    test_old_snapshot_survives_reload();
    test_reload_classifies_hot_and_restart_changes();
    test_invalid_value_is_rejected_without_side_effects();
    test_missing_file_is_rejected();
    test_file_reload_applies_and_updates_source_path();
    test_concurrent_snapshots_during_reloads();
    test_connection_manager_follows_reloads();
    test_logger_level_configuration();
    test_signal_handler_reload_wakeup();
    test_flatten_and_diff_helpers();

    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;

    return 0;
}


