#pragma once

#include "dupli/result.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

// IFileSystem abstracts every OS interaction the core needs. Business logic
// (scanning, grouping, planning, execution) only talks to this interface, so
// tests can use FakeFileSystem and never touch the host filesystem, and error
// paths (permission denied, missing file, collision) can be injected
// deterministically.
//
// Contract notes that matter for safety:
//  * is_symlink() reports the link itself and must not follow it.
//  * list_directory() returns entries of the directory but read/open must never
//    transparently follow a link: callers check is_symlink() first.
//  * copy_file() must fail with ErrorCode::Conflict if the destination exists;
//    no operation in this interface overwrites silently.
//  * same_device() is a hint used to prefer rename over copy; a false result
//    only means "assume copy is required", it never authorizes deletion.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual Result<bool> exists(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_directory(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_regular_file(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_symlink(const std::filesystem::path& path) = 0;
    virtual Result<bool> is_hidden(const std::filesystem::path& path) = 0;
    virtual Result<uintmax_t> file_size(const std::filesystem::path& path) = 0;
    virtual Result<bool> same_device(const std::filesystem::path& left,
                                     const std::filesystem::path& right) = 0;
    virtual Result<std::vector<std::filesystem::path>> list_directory(const std::filesystem::path& path) = 0;
    virtual Result<std::unique_ptr<IReadableFile>> open_read(const std::filesystem::path& path) = 0;
    virtual Result<std::unique_ptr<IWritableFile>> open_write(const std::filesystem::path& path) = 0;
    virtual Result<void> create_directory(const std::filesystem::path& path) = 0;
    virtual Result<void> remove(const std::filesystem::path& path) = 0;
    virtual Result<void> copy_file(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    /// Best-effort metadata transfer after a content copy. Metadata is treated
    /// separately from content identity: a failure here is reported as a
    /// warning, never as a failed content copy, and the platform adapter must
    /// only claim what it actually preserves (POSIX permissions and mtime here;
    /// ACLs, xattrs, resource forks and Windows alternate streams are not).
    virtual Result<void> copy_metadata(const std::filesystem::path& from,
                                       const std::filesystem::path& to) = 0;
    virtual Result<void> rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
};

// Helper used by platform adapters and by the scanner/executor: convert a
// filesystem path to a UTF-8 string for reports and messages. Keeping this in
// one place avoids lossy, ad-hoc conversions scattered across the code base.
std::string path_to_utf8(const std::filesystem::path& path);

// Inverse of path_to_utf8(). On Windows the std::string constructor would use
// the ANSI code page, so UTF-8 report/journal paths must be converted explicitly.
std::filesystem::path path_from_utf8(const std::string& utf8);

} // namespace dupli
