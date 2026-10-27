#pragma once

#include "dupli/result.hpp"
#include <filesystem>
#include <memory>
#include <vector>
#include <string>

namespace dupli {

class IReadableFile {
public:
    virtual ~IReadableFile() = default;
    virtual Result<std::vector<std::byte>> read_all() = 0;
    virtual Result<size_t> read(std::byte* buffer, size_t size) = 0;
    virtual Result<void> close() = 0;
};

class IWritableFile {
public:
    virtual ~IWritableFile() = default;
    virtual Result<size_t> write(const std::byte* buffer, size_t size) = 0;
    virtual Result<void> flush() = 0;
    virtual Result<void> close() = 0;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual Result<bool> exists(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_directory(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_regular_file(const std::filesystem::path& path) = 0;
    virtual Result<std::vector<std::filesystem::path>> list_directory(const std::filesystem::path& path) = 0;
    virtual Result<std::unique_ptr<IReadableFile>> open_read(const std::filesystem::path& path) = 0;
    virtual Result<std::unique_ptr<IWritableFile>> open_write(const std::filesystem::path& path) = 0;
    virtual Result<void> create_directory(const std::filesystem::path& path) = 0;
    virtual Result<void> remove(const std::filesystem::path& path) = 0;
    virtual Result<void> copy_file(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual Result<void> rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
};

} // namespace dupli

// IFileSystem interface: abstracts all file operations for dependency injection
// Allows unit tests to use FakeFileSystem instead of touching real filesystem
