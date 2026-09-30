// =============================================================================
// Aevrix - Runtime Configuration Store (Phase 24)
// =============================================================================
// Atomic, thread-safe holder for the active ServerConfig, enabling hot reload
// from SIGHUP or the /admin/reload-config endpoint.
//
// Copy-on-write model (no in-place mutation, ever):
// - A reload builds a *new* ServerConfig off to the side, parses and validates
//   it, and only then publishes it with a single atomic store of a
//   std::shared_ptr<const ServerConfig>.
// - Readers call snapshot() and get their own shared_ptr. That snapshot keeps
//   its generation alive by refcount, so an in-flight request keeps using the
//   config it started with while later requests see the new one. Old
//   generations are freed only when the last reference goes away.
// - There is therefore no window in which a reader observes a half-applied
//   configuration, and no lock is held while a request runs.
//
// std::atomic<std::shared_ptr<T>> (C++20) performs the swap. It has the same
// publish/subscribe semantics as a raw atomic pointer but keeps lifetime
// automatic, which is what makes "the old config stays valid until every
// reader is done" true by construction rather than by code review.
//
// Hot vs restart-required is a property of *where a value is read*, not of the
// store: values read per request/per tick from a fresh snapshot are hot, values
// captured once at startup (sockets, TLS contexts, worker threads) need a
// restart. diff_keys() + is_restart_required() classify a change set so a
// reload can log exactly what took effect and what did not.
//
// Signal safety: the SIGHUP handler only flips an atomic flag and writes to an
// eventfd (see SignalHandler). All file I/O, parsing, validation and the swap
// itself happen on the event-loop thread.
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aevrix/server_config.h"

namespace aevrix {

// Forward declaration: the parser is a parameter of the reload seam, not a
// member of the store.
class ConfigParser;

/**
 * @brief Outcome of one configuration reload attempt.
 *
 * Plain value type, safe to log or move across threads. On failure `changes`,
 * `applied_hot` and `restart_required` are empty and the previous generation is
 * still the active one.
 */
struct ConfigReloadOutcome {
    bool success = false;                        ///< True when the swap happened
    std::string message;                         ///< Human-readable status/error
    uint64_t generation = 0;                     ///< Active generation after attempt
    std::vector<std::string> changes;            ///< Keys whose rendered value differs
    std::vector<std::string> applied_hot;        ///< Changed keys effective immediately
    std::vector<std::string> restart_required;   ///< Changed keys needing a restart
    std::shared_ptr<const ServerConfig> snapshot; ///< New active config (null on failure)
};

/**
 * @brief Atomically swappable holder for the active ServerConfig.
 *
 * Thread safety: snapshot(), generation() and the counters may be called
 * concurrently from any thread. try_reload() may be called from any thread as
 * well; concurrent reloads each publish a complete configuration, so the store
 * is never observed mid-update.
 */
class ServerConfigStore {
public:
    /**
     * @brief Publish the startup configuration as generation 1.
     *
     * @param initial Fully validated config. Must not be null.
     * @param source_path File the config came from ("" when built in code).
     */
    explicit ServerConfigStore(std::shared_ptr<const ServerConfig> initial,
                               std::string source_path = "");

    ServerConfigStore(const ServerConfigStore&) = delete;
    ServerConfigStore& operator=(const ServerConfigStore&) = delete;

    /**
     * @brief Take an atomic snapshot of the active configuration.
     *
     * Cheap: one atomic load plus a refcount bump. The returned pointer stays
     * valid and immutable for as long as the caller holds it.
     */
    std::shared_ptr<const ServerConfig> snapshot() const;

    /**
     * @brief Active generation (1 at startup, +1 per successful reload).
     */
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

    /**
     * @brief Path used by try_reload() when no explicit path is given.
     */
    std::string source_path() const;

    /**
     * @brief Change the path used by future reloads.
     */
    void set_source_path(std::string path);

    /**
     * @brief Parse/validate/swap a configuration file.
     *
     * All-or-nothing: any failure (unreadable file, invalid value, failed
     * validation, unreadable TLS material) leaves the active configuration
     * untouched and reports the reason instead of degrading the server.
     *
     * @param path File to load. Empty = reuse source_path().
     */
    ConfigReloadOutcome try_reload(const std::string& path = "");

    /**
     * @brief Parse/validate/swap from an already-parsed config (test seam).
     *
     * Same validation and swap rules as try_reload(), without filesystem
     * access, so unit tests can drive reloads deterministically.
     *
     * @param parser Populated parser holding the candidate configuration.
     * @param label Description used in log lines (e.g. "path=/etc/aevrix.conf").
     * @param remember_path Source path to adopt when the swap succeeds.
     */
    ConfigReloadOutcome try_reload_from_parser(const ConfigParser& parser,
                                              const std::string& label = "memory",
                                              std::string remember_path = "");

    /**
     * @brief Reload counters (observability + tests).
     */
    uint64_t reload_success_count() const { return reload_success_count_.load(std::memory_order_relaxed); }
    uint64_t reload_failure_count() const { return reload_failure_count_.load(std::memory_order_relaxed); }
    std::string last_error() const;

    // =========================================================================
    // Change classification (pure helpers, unit-testable without a store)
    // =========================================================================

    /**
     * @brief Render every configuration key as a comparable string pair.
     */
    static std::vector<std::pair<std::string, std::string>> flatten(const ServerConfig& config);

    /**
     * @brief Names of the keys whose rendered value differs between configs.
     */
    static std::vector<std::string> diff_keys(const ServerConfig& old_config,
                                             const ServerConfig& new_config);

    /**
     * @brief Whether a key needs a process restart to take effect.
     *
     * True for values captured once at startup: listener address/port, TLS
     * contexts and their files, the worker-thread count, and the upstream
     * routing targets held by long-lived objects.
     */
    static bool is_restart_required(const std::string& key);

    /**
     * @brief Render an outcome as a single log-friendly line.
     */
    static std::string describe(const ConfigReloadOutcome& outcome);

private:
    /// Publish an already-validated candidate and classify what changed.
    ConfigReloadOutcome publish(std::shared_ptr<const ServerConfig> candidate,
                                const std::string& label,
                                std::string remember_path);

    std::atomic<std::shared_ptr<const ServerConfig>> active_;  ///< Published generation
    std::atomic<uint64_t> generation_{1};                      ///< 1 + successful swaps
    std::atomic<uint64_t> reload_success_count_{0};
    std::atomic<uint64_t> reload_failure_count_{0};

    mutable std::mutex meta_mutex_;  ///< Guards only the two strings below
    std::string source_path_;        ///< File candidates are loaded from
    std::string last_error_;         ///< Last reload failure diagnostic
};

} // namespace aevrix
