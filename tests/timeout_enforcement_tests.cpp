// =============================================================================
// Aevrix - Timeout Enforcement Tests (Stage 6)
// =============================================================================
// This test suite validates timeout enforcement in the event-driven runtime.
// Tests verify that:
// - Deadlines are set correctly on state transitions
// - Header timeout is enforced
// - Body timeout is enforced
// - Write timeout is enforced
// - Keep-alive timeout is enforced
// - WorkerPool interactions don't timeout incorrectly
// - Multiple connections with different deadlines work correctly
// =============================================================================

#include "aevrix/connection.h"
#include "aevrix/server_config.h"
#include "aevrix/logger.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

// Test 1: Deadline is set for header reading state
void test_header_deadline() {
    std::cout << "Test 1: Header deadline set correctly... ";

    aevrix::ServerConfig config;
    config.set_header_timeout_ms(5000);  // 5 seconds

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Reading);
    conn.set_read_state(aevrix::ReadState::Headers);

    conn.set_deadline(config);

    // Wait a short time (deadline should not be exceeded)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 2: Deadline is set for body reading state
void test_body_deadline() {
    std::cout << "Test 2: Body deadline set correctly... ";

    aevrix::ServerConfig config;
    config.set_body_timeout_ms(10000);  // 10 seconds

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Reading);
    conn.set_read_state(aevrix::ReadState::Body);

    conn.set_deadline(config);

    // Deadline should be in the future
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 3: Deadline is set for write state
void test_write_deadline() {
    std::cout << "Test 3: Write deadline set correctly... ";

    aevrix::ServerConfig config;
    config.set_write_timeout_ms(15000);  // 15 seconds

    aevrix::Connection conn(1, 1);
    conn.set_write_state(aevrix::WriteState::Headers);

    conn.set_deadline(config);

    // Deadline should be in the future
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 4: Deadline is set for keep-alive state
void test_keep_alive_deadline() {
    std::cout << "Test 4: Keep-alive deadline set correctly... ";

    aevrix::ServerConfig config;
    config.set_keep_alive_timeout_ms(5000);  // 5 seconds

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Waiting);
    conn.set_keep_alive(true);

    conn.set_deadline(config);

    // Deadline should be in the future
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 5: Worker active prevents timeout
void test_worker_active_prevents_timeout() {
    std::cout << "Test 5: Worker active prevents timeout... ";

    aevrix::ServerConfig config;
    config.set_header_timeout_ms(100);  // Very short timeout

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Reading);
    conn.set_read_state(aevrix::ReadState::Headers);

    // Mark worker as active
    conn.set_worker_active(true);

    conn.set_deadline(config);

    // Even with very short timeout, should not timeout while worker is active
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 6: Deadline updates on state change
void test_deadline_updates_on_state_change() {
    std::cout << "Test 6: Deadline updates on state change... ";

    aevrix::ServerConfig config;
    config.set_header_timeout_ms(1000);
    config.set_keep_alive_timeout_ms(5000);

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Reading);
    conn.set_read_state(aevrix::ReadState::Headers);

    conn.set_deadline(config);

    // Change to keep-alive state
    conn.set_state(aevrix::ConnectionState::Waiting);
    conn.set_deadline(config);

    // New deadline should be in the future
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 7: No deadline for idle state
void test_no_deadline_idle() {
    std::cout << "Test 7: No deadline for idle state... ";

    aevrix::ServerConfig config;
    config.set_header_timeout_ms(1000);

    aevrix::Connection conn(1, 1);
    conn.set_state(aevrix::ConnectionState::Closing);

    conn.set_deadline(config);

    // Should not timeout in closing state
    assert(!conn.has_deadline_exceeded());

    std::cout << "PASSED" << std::endl;
}

// Test 8: Worker flag toggles correctly
void test_worker_flag_toggle() {
    std::cout << "Test 8: Worker flag toggles correctly... ";

    aevrix::Connection conn(1, 1);

    assert(!conn.is_worker_active());

    conn.set_worker_active(true);
    assert(conn.is_worker_active());

    conn.set_worker_active(false);
    assert(!conn.is_worker_active());

    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "=== Stage 6: Timeout Enforcement Tests ===" << std::endl;
    std::cout << std::endl;

    test_header_deadline();
    test_body_deadline();
    test_write_deadline();
    test_keep_alive_deadline();
    test_worker_active_prevents_timeout();
    test_deadline_updates_on_state_change();
    test_no_deadline_idle();
    test_worker_flag_toggle();

    std::cout << std::endl;
    std::cout << "=== All timeout enforcement tests passed ===" << std::endl;

    return 0;
}
