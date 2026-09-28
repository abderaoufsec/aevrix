// =============================================================================
// Aevrix - Path Security Tests (Stage 7)
// =============================================================================
// This test suite validates static-file security against path traversal attacks.
// Tests verify that:
// - Plain traversal (../) is rejected
// - Encoded traversal (%2e%2e/) is rejected
// - Double-encoded traversal is rejected
// - Absolute paths are rejected
// - Backslash-based traversal is rejected
// - Symlink policy is enforced (symlinks allowed only if target stays in root)
// - Document-root prefix collision is prevented
// - Legitimate encoded filenames work correctly
// =============================================================================

#include "aevrix/static_file_server.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <ctime>

namespace fs = std::filesystem;

// Test helper: Create temporary test directory structure
struct TestDir {
    fs::path root;
    fs::path public_dir;
    fs::path secret_dir;

    TestDir() {
        std::string timestamp = std::to_string(std::time(nullptr));
        root = fs::temp_directory_path() / ("aevrix_test_" + timestamp);
        public_dir = root / "public";
        secret_dir = root / "secret";

        fs::create_directories(public_dir);
        fs::create_directories(secret_dir);

        // Create test files
        std::ofstream(public_dir / "index.html") << "Public file";
        std::ofstream(secret_dir / "secret.txt") << "Secret file";
    }

    ~TestDir() {
        // Cleanup
        fs::remove_all(root);
    }
};

// Test 1: Plain traversal attack is rejected
void test_plain_traversal_rejected() {
    std::cout << "Test 1: Plain traversal (../) rejected... ";

#ifdef _WIN32
    std::cout << "SKIPPED (filesystem test requires Linux)" << std::endl;
    return;
#endif

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Try to access secret file via traversal
    bool is_safe = server.is_path_safe("../secret/secret.txt");
    assert(!is_safe);  // Should be rejected

    std::cout << "PASSED" << std::endl;
}

// Test 2: Encoded traversal attack is rejected
void test_encoded_traversal_rejected() {
    std::cout << "Test 2: Encoded traversal (%2e%2e/) rejected... ";

#ifdef _WIN32
    std::cout << "SKIPPED (filesystem test requires Linux)" << std::endl;
    return;
#endif

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Try to access secret file via URL-encoded traversal
    bool is_safe = server.is_path_safe("%2e%2e/secret/secret.txt");
    assert(!is_safe);  // Should be rejected

    std::cout << "PASSED" << std::endl;
}

// Test 3: Double-encoded traversal is rejected
void test_double_encoded_traversal_rejected() {
    std::cout << "Test 3: Double-encoded traversal rejected... ";

#ifdef _WIN32
    std::cout << "SKIPPED (filesystem test requires Linux)" << std::endl;
    return;
#endif

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Try to access secret file via double-encoded traversal
    try {
        (void)server.is_path_safe("%252e%252e/secret/secret.txt");
        assert(false);  // Should throw due to double-encoding detection
    } catch (...) {
        // Expected: double-encoded paths should be rejected
    }

    std::cout << "PASSED" << std::endl;
}

// Test 4: Absolute path is rejected
void test_absolute_path_rejected() {
    std::cout << "Test 4: Absolute path rejected... ";

#ifdef _WIN32
    std::cout << "SKIPPED (filesystem test requires Linux)" << std::endl;
    return;
#endif

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Try to access secret file via absolute path
    bool is_safe = server.is_path_safe(test_dir.secret_dir.string());
    assert(!is_safe);  // Should be rejected

    std::cout << "PASSED" << std::endl;
}

// Test 5: Backslash traversal is rejected
void test_backslash_traversal_rejected() {
    std::cout << "Test 5: Backslash traversal rejected... ";

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Try to access secret file via backslash traversal
    bool is_safe = server.is_path_safe("..\\secret\\secret.txt");
    assert(!is_safe);  // Should be rejected (backslashes normalized)

    std::cout << "PASSED" << std::endl;
}

// Test 6: Legitimate file access works
void test_legitimate_file_access() {
    std::cout << "Test 6: Legitimate file access works... ";

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Access legitimate file
    bool is_safe = server.is_path_safe("public/index.html");
    assert(is_safe);  // Should be allowed

    std::cout << "PASSED" << std::endl;
}

// Test 7: Symlink inside root is allowed
void test_symlink_inside_root_allowed() {
    std::cout << "Test 7: Symlink inside root allowed... ";

#ifdef __linux__
    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Create symlink inside public dir pointing to another file in public dir
    fs::path link_path = test_dir.public_dir / "link.html";
    fs::create_directory_symlink(test_dir.public_dir, link_path);

    // Symlink target is inside root, should be allowed
    bool is_safe = server.is_path_safe("public/link.html/index.html");
    assert(is_safe);  // Should be allowed
#else
    std::cout << "SKIPPED (symlink test requires Linux)" << std::endl;
    return;
#endif

    std::cout << "PASSED" << std::endl;
}

// Test 8: Symlink outside root is rejected
void test_symlink_outside_root_rejected() {
    std::cout << "Test 8: Symlink outside root rejected... ";

#ifdef __linux__
    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Create symlink inside public dir pointing outside root
    fs::path outside_dir = fs::temp_directory_path() / "aevrix_outside";
    fs::create_directories(outside_dir);
    std::ofstream(outside_dir / "outside.txt") << "Outside file";

    fs::path link_path = test_dir.public_dir / "outside_link";
    fs::create_directory_symlink(outside_dir, link_path);

    // Symlink target is outside root, should be rejected
    bool is_safe = server.is_path_safe("public/outside_link/outside.txt");
    assert(!is_safe);  // Should be rejected

    // Cleanup
    fs::remove_all(outside_dir);
#else
    std::cout << "SKIPPED (symlink test requires Linux)" << std::endl;
    return;
#endif

    std::cout << "PASSED" << std::endl;
}

// Test 9: Document-root prefix collision is prevented
void test_prefix_collision_prevented() {
    std::cout << "Test 9: Document-root prefix collision prevented... ";

    // Create two directories with similar names
    fs::path root = fs::temp_directory_path() / "aevrix_prefix_test";
    fs::path www = root / "www";
    fs::path www_secret = root / "www-secret";

    fs::create_directories(www);
    fs::create_directories(www_secret);

    std::ofstream(www / "index.html") << "Public file";
    std::ofstream(www_secret / "secret.txt") << "Secret file";

    aevrix::StaticFileServer server(www.string());

    // Try to access www-secret via prefix collision
    bool is_safe = server.is_path_safe("../www-secret/secret.txt");
    assert(!is_safe);  // Should be rejected

    // Cleanup
    fs::remove_all(root);

    std::cout << "PASSED" << std::endl;
}

// Test 10: Legitimate encoded filename works
void test_legitimate_encoded_filename() {
    std::cout << "Test 10: Legitimate encoded filename works... ";

    TestDir test_dir;
    aevrix::StaticFileServer server(test_dir.root.string());

    // Create file with space in name
    std::ofstream(test_dir.public_dir / "file with spaces.html") << "File with spaces";

    // Access file with URL-encoded space
    bool is_safe = server.is_path_safe("public/file%20with%20spaces.html");
    assert(is_safe);  // Should be allowed

    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "=== Stage 7: Path Security Tests ===" << std::endl;
    std::cout << std::endl;

#ifdef _WIN32
    std::cout << "Path security tests skipped on Windows (requires Linux filesystem)" << std::endl;
    std::cout << "Tests will run on Kali Linux during verification" << std::endl;
    return 0;
#endif

    test_plain_traversal_rejected();
    test_encoded_traversal_rejected();
    test_double_encoded_traversal_rejected();
    test_absolute_path_rejected();
    test_backslash_traversal_rejected();
    test_legitimate_file_access();
    test_symlink_inside_root_allowed();
    test_symlink_outside_root_rejected();
    test_prefix_collision_prevented();
    test_legitimate_encoded_filename();

    std::cout << std::endl;
    std::cout << "=== All path security tests passed ===" << std::endl;

    return 0;
}
