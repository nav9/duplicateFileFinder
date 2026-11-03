#pragma once

// Small test helpers shared by the test executables. Assertions do not depend on
// NDEBUG (the test targets compile with assertions enabled) so Release test runs
// are meaningful too.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

namespace testutil {

[[noreturn]] inline void check_failed(const char* file, int line, const std::string& message) {
    std::cerr << file << ":" << line << ": CHECK failed: " << message << "\n";
    std::exit(1);
}

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ::testutil::check_failed(__FILE__, __LINE__, #condition);          \
        }                                                                      \
    } while (false)

#define CHECK_EQ(left, right)                                                  \
    do {                                                                       \
        const auto& check_left = (left);                                       \
        const auto& check_right = (right);                                     \
        if (!(check_left == check_right)) {                                    \
            ::testutil::check_failed(__FILE__, __LINE__,                       \
                                     std::string(#left " == " #right));        \
        }                                                                      \
    } while (false)

inline std::string unique_suffix() {
    static std::mt19937_64 generator(std::random_device{}());
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(now) + "_" + std::to_string(generator() % 1000000);
}

/// Unique directory under the OS temp dir, removed automatically on scope exit.
class TempDir {
public:
    explicit TempDir(const std::string& tag = "dupli") {
        path = std::filesystem::temp_directory_path() /
               ("dupli-" + tag + "-" + unique_suffix());
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error); // Best effort; never throws.
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& get() const { return path; }

    std::filesystem::path path;
};

inline void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    file.close();
    CHECK(file.good() || file.eof());
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace testutil
