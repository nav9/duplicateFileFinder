#pragma once

#include "dupli/filesystem.hpp"
#include <filesystem>

#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace dupli {

namespace detail {

/// Volume/device comparison. This is only ever used as a hint to prefer rename
/// over copy: a false negative makes us copy (safe, source retained) and a
/// false positive makes us try rename, which fails safely with a cross-device
/// error that the executor handles by falling back to copy+verify.
inline Result<bool> paths_share_device(const std::filesystem::path& left,
                                       const std::filesystem::path& right) {
#ifdef _WIN32
    auto volume_serial = [](const std::filesystem::path& p, DWORD& serial) -> bool {
        HANDLE handle = ::CreateFileW(p.c_str(), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        const bool ok = ::GetFileInformationByHandle(handle, &info) != 0;
        ::CloseHandle(handle);
        if (ok) serial = info.dwVolumeSerialNumber;
        return ok;
    };
    DWORD left_serial = 0;
    DWORD right_serial = 0;
    if (!volume_serial(left, left_serial) || !volume_serial(right, right_serial)) {
        return false; // Unknown: assume different, which forces the safe copy path.
    }
    return left_serial == right_serial;
#else
    struct stat left_stat {};
    struct stat right_stat {};
    // stat the paths themselves so a mount point is detected at its own entry
    // (stat'ing the parent would hide the boundary until the mount is entered).
    if (::stat(left.c_str(), &left_stat) != 0 || ::stat(right.c_str(), &right_stat) != 0) {
        return Error{ErrorCode::IoError, "Cannot stat path to compare devices", "", "same_device"};
    }
    return left_stat.st_dev == right_stat.st_dev;
#endif
}

} // namespace detail

class RealFileSystem : public IFileSystem {
public:
    RealFileSystem() = default;
    
    Result<bool> exists(const std::filesystem::path& path) override {
        try {
            return std::filesystem::exists(path);
        } catch (const std::filesystem::filesystem_error& e) {
            return Error{ErrorCode::IoError, e.what(), path.string(), "exists"};
        }
    }
    
    Result<bool> is_directory(const std::filesystem::path& path) override {
        try {
            return std::filesystem::is_directory(path);
        } catch (const std::filesystem::filesystem_error& e) {
            return Error{ErrorCode::IoError, e.what(), path.string(), "is_directory"};
        }
    }
    
    Result<bool> is_regular_file(const std::filesystem::path& path) override {
        try {
            return std::filesystem::is_regular_file(path);
        } catch (const std::filesystem::filesystem_error& e) {
            return Error{ErrorCode::IoError, e.what(), path.string(), "is_regular_file"};
        }
    }

    /// Reports the link itself (never its target). Used by the scanner so links
    /// are reported and skipped, not traversed.
    Result<bool> is_symlink(const std::filesystem::path& path) override {
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec) {
            if (ec == std::errc::no_such_file_or_directory) {
                return Error{ErrorCode::NotFound, ec.message(), path.string(), "is_symlink"};
            }
            if (ec == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, ec.message(), path.string(), "is_symlink"};
            }
            return Error{ErrorCode::IoError, ec.message(), path.string(), "is_symlink"};
        }
        return std::filesystem::is_symlink(status);
    }

    /// Hidden means dot-prefixed on POSIX. On Windows the hidden attribute is
    /// authoritative, so both are checked; this is why hidden detection lives in
    /// the platform adapter rather than in the scanner.
    Result<bool> is_hidden(const std::filesystem::path& path) override {
        const std::string name = path.filename().string();
        if (!name.empty() && name.front() == '.') return true;
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            return Error{ErrorCode::IoError, "Cannot read file attributes", path.string(), "is_hidden"};
        }
        return (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
#else
        return false;
#endif
    }

    Result<uintmax_t> file_size(const std::filesystem::path& path) override {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            if (ec == std::errc::no_such_file_or_directory) {
                return Error{ErrorCode::NotFound, ec.message(), path.string(), "file_size"};
            }
            if (ec == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, ec.message(), path.string(), "file_size"};
            }
            return Error{ErrorCode::IoError, ec.message(), path.string(), "file_size"};
        }
        return size;
    }

    Result<bool> same_device(const std::filesystem::path& left,
                             const std::filesystem::path& right) override {
        return detail::paths_share_device(left, right);
    }
    
    Result<std::vector<std::filesystem::path>> list_directory(const std::filesystem::path& path) override {
        try {
            std::vector<std::filesystem::path> result;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                result.push_back(entry.path());
            }
            return result;
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), path.string(), "list_directory"};
            }
            if (e.code() == std::errc::no_such_file_or_directory) {
                return Error{ErrorCode::NotFound, e.what(), path.string(), "list_directory"};
            }
            return Error{ErrorCode::IoError, e.what(), path.string(), "list_directory"};
        }
    }
    
    Result<std::unique_ptr<IReadableFile>> open_read(const std::filesystem::path& path) override {
        try {
            // Check if it's a symlink - we must not follow symlinks per requirements
            if (std::filesystem::is_symlink(path)) {
                return Error{ErrorCode::Unsupported, "Symlinks are not followed", path.string(), "open_read"};
            }
            auto status = std::filesystem::status(path);
            if (std::filesystem::is_directory(status)) {
                return Error{ErrorCode::Unsupported, "Cannot read directory", path.string(), "open_read"};
            }
            if (!std::filesystem::is_regular_file(status)) {
                return Error{ErrorCode::Unsupported, "Not a regular file", path.string(), "open_read"};
            }
            return std::unique_ptr<IReadableFile>(new RealReadableFile(path));
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), path.string(), "open_read"};
            }
            if (e.code() == std::errc::no_such_file_or_directory) {
                return Error{ErrorCode::NotFound, e.what(), path.string(), "open_read"};
            }
            return Error{ErrorCode::IoError, e.what(), path.string(), "open_read"};
        }
    }
    
    Result<std::unique_ptr<IWritableFile>> open_write(const std::filesystem::path& path) override {
        try {
            return std::unique_ptr<IWritableFile>(new RealWritableFile(path));
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), path.string(), "open_write"};
            }
            return Error{ErrorCode::IoError, e.what(), path.string(), "open_write"};
        }
    }
    
    Result<void> create_directory(const std::filesystem::path& path) override {
        try {
            std::filesystem::create_directories(path);
            return Result<void>();
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), path.string(), "create_directory"};
            }
            return Error{ErrorCode::IoError, e.what(), path.string(), "create_directory"};
        }
    }
    
    Result<void> remove(const std::filesystem::path& path) override {
        try {
            std::filesystem::remove_all(path);
            return Result<void>();
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), path.string(), "remove"};
            }
            return Error{ErrorCode::IoError, e.what(), path.string(), "remove"};
        }
    }
    
    Result<void> copy_file(const std::filesystem::path& from, const std::filesystem::path& to) override {
        try {
            // Don't follow symlinks
            if (std::filesystem::is_symlink(from)) {
                return Error{ErrorCode::Unsupported, "Symlinks are not copied", from.string(), "copy_file"};
            }
            std::filesystem::copy_file(from, to, std::filesystem::copy_options::none);
            return Result<void>();
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), "", "copy_file"};
            }
            if (e.code() == std::errc::file_exists) {
                return Error{ErrorCode::Conflict, e.what(), to.string(), "copy_file"};
            }
            return Error{ErrorCode::IoError, e.what(), "", "copy_file"};
        }
    }
    
    /// Preserves POSIX permissions and modification time after a copy. Extended
    /// attributes, ACLs, resource forks and Windows alternate data streams are
    /// deliberately out of scope; callers must treat a failure as a warning and
    /// never claim full metadata preservation.
    Result<void> copy_metadata(const std::filesystem::path& from,
                               const std::filesystem::path& to) override {
        std::error_code error;
        const auto status = std::filesystem::status(from, error);
        if (error) {
            return Error{ErrorCode::IoError, error.message(), from.string(), "copy_metadata"};
        }
        std::filesystem::permissions(to, status.permissions(),
                                     std::filesystem::perm_options::replace, error);
        if (error) {
            return Error{ErrorCode::IoError, error.message(), to.string(), "copy_metadata"};
        }
        const auto modified = std::filesystem::last_write_time(from, error);
        if (!error) {
            std::filesystem::last_write_time(to, modified, error);
            if (error) {
                return Error{ErrorCode::IoError, error.message(), to.string(), "copy_metadata"};
            }
        }
        return Result<void>();
    }

    /// Rename/move that never overwrites. std::filesystem::rename() replaces an
    /// existing destination on POSIX, so regular files are published with a hard
    /// link (atomic create-if-absent on the same filesystem) followed by removing
    /// the original path. If the link cannot be created (directories, exotic
    /// filesystems) the code falls back to rename after an existence check.
    Result<void> rename(const std::filesystem::path& from, const std::filesystem::path& to) override {
        try {
            std::error_code exists_error;
            if (std::filesystem::exists(to, exists_error) && !exists_error) {
                return Error{ErrorCode::Conflict, "Destination already exists and will not be overwritten",
                             to.string(), "rename"};
            }
            if (std::filesystem::is_symlink(from)) {
                return Error{ErrorCode::Unsupported, "Symlinks are not moved", from.string(), "rename"};
            }
#ifndef _WIN32
            if (std::filesystem::is_regular_file(from)) {
                std::error_code link_error;
                std::filesystem::create_hard_link(from, to, link_error);
                if (!link_error) {
                    std::error_code remove_error;
                    std::filesystem::remove(from, remove_error);
                    if (remove_error) {
                        // The destination is published; report the partial state
                        // so the journal/recovery can reconcile it.
                        return Error{ErrorCode::IoError,
                                     "Moved but could not remove the original: " +
                                         remove_error.message(),
                                     from.string(), "rename"};
                    }
                    return Result<void>();
                }
                if (link_error != std::errc::operation_not_supported &&
                    link_error != std::errc::function_not_supported) {
                    return Error{ErrorCode::IoError, link_error.message(), from.string(), "rename"};
                }
            }
#endif
            std::filesystem::rename(from, to);
            return Result<void>();
        } catch (const std::filesystem::filesystem_error& e) {
            if (e.code() == std::errc::permission_denied) {
                return Error{ErrorCode::PermissionDenied, e.what(), "", "rename"};
            }
            if (e.code() == std::errc::file_exists) {
                return Error{ErrorCode::Conflict, e.what(), to.string(), "rename"};
            }
            return Error{ErrorCode::IoError, e.what(), "", "rename"};
        }
    }
    
private:
    class RealReadableFile : public IReadableFile {
    public:
        RealReadableFile(const std::filesystem::path& path) : m_file(path, std::ios::binary) {
            if (!m_file.is_open()) {
                throw std::filesystem::filesystem_error("Cannot open file", std::make_error_code(std::errc::permission_denied));
            }
        }
        
        Result<std::vector<std::byte>> read_all() override {
            try {
                m_file.seekg(0, std::ios::end);
                std::streamsize size = m_file.tellg();
                m_file.seekg(0, std::ios::beg);
                std::vector<std::byte> buffer(size);
                if (size > 0) {
                    m_file.read(reinterpret_cast<char*>(buffer.data()), size);
                    if (m_file.bad()) {
                        return Error{ErrorCode::IoError, "Read error", "", "read_all"};
                    }
                }
                return buffer;
            } catch (...) {
                return Error{ErrorCode::IoError, "Read error", "", "read_all"};
            }
        }
        
        Result<size_t> read(std::byte* buffer, size_t size) override {
            try {
                if (!buffer || size == 0) return size_t(0);
                m_file.read(reinterpret_cast<char*>(buffer), size);
                std::streamsize read_count = m_file.gcount();
                if (m_file.bad()) {
                    return Error{ErrorCode::IoError, "Read error", "", "read"};
                }
                return static_cast<size_t>(read_count);
            } catch (...) {
                return Error{ErrorCode::IoError, "Read error", "", "read"};
            }
        }
        
        Result<void> close() override {
            m_file.close();
            return Result<void>();
        }
        
    private:
        std::ifstream m_file;
    };
    
    class RealWritableFile : public IWritableFile {
    public:
        RealWritableFile(const std::filesystem::path& path) : m_file(path, std::ios::binary) {
            if (!m_file.is_open()) {
                throw std::filesystem::filesystem_error("Cannot open file", std::make_error_code(std::errc::permission_denied));
            }
        }
        
        Result<size_t> write(const std::byte* buffer, size_t size) override {
            try {
                if (!buffer || size == 0) return size_t(0);
                m_file.write(reinterpret_cast<const char*>(buffer), size);
                if (m_file.bad()) {
                    return Error{ErrorCode::IoError, "Write error", "", "write"};
                }
                return size;
            } catch (...) {
                return Error{ErrorCode::IoError, "Write error", "", "write"};
            }
        }
        
        Result<void> flush() override {
            try {
                m_file.flush();
                if (m_file.bad()) {
                    return Error{ErrorCode::IoError, "Flush error", "", "flush"};
                }
                return Result<void>();
            } catch (...) {
                return Error{ErrorCode::IoError, "Flush error", "", "flush"};
            }
        }
        
        Result<void> close() override {
            m_file.close();
            return Result<void>();
        }
        
    private:
        std::ofstream m_file;
    };
};

} // namespace dupli
