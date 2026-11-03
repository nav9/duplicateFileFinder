#pragma once

#include "dupli/filesystem.hpp"
#include <map>
#include <set>
#include <cstring>
#include <algorithm>

namespace dupli {

class FakeReadableFile : public IReadableFile {
public:
    FakeReadableFile(std::vector<std::byte> data, bool fail_on_read = false)
        : m_data(std::move(data)), m_fail_on_read(fail_on_read), m_pos(0) {}
    
    Result<std::vector<std::byte>> read_all() override {
        if (m_fail_on_read) {
            return Error{ErrorCode::IoError, "Read failed", "", "read_all"};
        }
        return m_data;
    }
    
    Result<size_t> read(std::byte* buffer, size_t size) override {
        if (m_fail_on_read) {
            return Error{ErrorCode::IoError, "Read failed", "", "read"};
        }
        size_t to_read = std::min(size, m_data.size() - m_pos);
        if (to_read > 0 && buffer) {
            std::memcpy(buffer, m_data.data() + m_pos, to_read);
            m_pos += to_read;
        }
        return to_read;
    }
    
    Result<void> close() override {
        return Result<void>();
    }
    
private:
    std::vector<std::byte> m_data;
    bool m_fail_on_read;
    size_t m_pos;
};

class FakeWritableFile : public IWritableFile {
public:
    FakeWritableFile(std::vector<std::byte>& storage, bool fail_on_write = false)
        : m_storage(storage), m_fail_on_write(fail_on_write) {}
    
    Result<size_t> write(const std::byte* buffer, size_t size) override {
        if (m_fail_on_write) {
            return Error{ErrorCode::IoError, "Write failed", "", "write"};
        }
        if (buffer && size > 0) {
            size_t old_size = m_storage.size();
            m_storage.resize(old_size + size);
            std::memcpy(m_storage.data() + old_size, buffer, size);
        }
        return size;
    }
    
    Result<void> flush() override {
        if (m_fail_on_write) {
            return Error{ErrorCode::IoError, "Flush failed", "", "flush"};
        }
        return Result<void>();
    }
    
    Result<void> close() override {
        return Result<void>();
    }
    
private:
    std::vector<std::byte>& m_storage;
    bool m_fail_on_write;
};

class FakeFileSystem : public IFileSystem {
public:
    struct FileEntry {
        bool is_dir = false;
        std::vector<std::byte> content;
        bool readable = true;
        bool writable = true;
    };
    
    void add_file(const std::filesystem::path& path, const std::string& content) {
        std::vector<std::byte> bytes;
        bytes.reserve(content.size());
        for (char c : content) {
            bytes.push_back(static_cast<std::byte>(c));
        }
        m_files[path] = FileEntry{false, std::move(bytes), true, true};
        ensure_parent_dirs(path);
    }
    
    void add_file_bytes(const std::filesystem::path& path, const std::vector<std::byte>& content) {
        m_files[path] = FileEntry{false, content, true, true};
        ensure_parent_dirs(path);
    }
    
    void add_directory(const std::filesystem::path& path) {
        m_files[path] = FileEntry{true, std::vector<std::byte>(), true, true};
        ensure_parent_dirs(path);
    }
    
    void add_symlink(const std::filesystem::path& path) {
        m_symlinks.insert(path);
    }
    
    void set_read_failure(const std::filesystem::path& path, bool fail) {
        m_read_failures[path] = fail;
    }
    
    void set_permission_denied(const std::filesystem::path& path, bool denied) {
        if (denied) {
            m_permission_denied.insert(path);
        } else {
            m_permission_denied.erase(path);
        }
    }

    /// Pretend all paths live on the same filesystem (default) or on different
    /// filesystems, which makes the executor take its copy+verify path instead
    /// of the rename path.
    void set_same_device(bool same) { m_same_device = same; }

    /// Test hook: make copy_file() write truncated content, simulating a torn or
    /// corrupted copy. Used to prove the executor verifies destinations and
    /// never reports a bad copy as success.
    void set_corrupt_copy(bool corrupt) { m_corrupt_copy = corrupt; }
    
    Result<bool> exists(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "exists"};
        }
        return m_files.count(path) > 0 || m_symlinks.count(path) > 0;
    }
    
    Result<bool> is_directory(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "is_directory"};
        }
        auto it = m_files.find(path);
        if (it != m_files.end()) return it->second.is_dir;
        return false;
    }
    
    Result<bool> is_regular_file(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "is_regular_file"};
        }
        auto it = m_files.find(path);
        if (it != m_files.end()) return !it->second.is_dir;
        return false;
    }

    Result<bool> is_symlink(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "is_symlink"};
        }
        return m_symlinks.count(path) > 0;
    }

    /// Hidden-ness is name based, matching the POSIX convention used by the
    /// scanner. Windows attribute based hiding is handled by RealFileSystem.
    Result<bool> is_hidden(const std::filesystem::path& path) override {
        const std::string name = path.filename().string();
        return !name.empty() && name.front() == '.';
    }

    Result<uintmax_t> file_size(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "file_size"};
        }
        auto it = m_files.find(path);
        if (it == m_files.end()) {
            return Error{ErrorCode::NotFound, "Not found", path.string(), "file_size"};
        }
        if (it->second.is_dir) {
            return Error{ErrorCode::Unsupported, "Directory has no content size", path.string(), "file_size"};
        }
        return static_cast<uintmax_t>(it->second.content.size());
    }

    Result<bool> same_device(const std::filesystem::path&, const std::filesystem::path&) override {
        return m_same_device;
    }
    
    Result<std::vector<std::filesystem::path>> list_directory(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "list_directory"};
        }
        std::vector<std::filesystem::path> result;
        std::string path_str = path.string();
        if (!path_str.empty() && path_str.back() != '/') path_str += '/';
        std::string path_str_no_slash = path.string();
        if (path_str_no_slash.empty()) path_str_no_slash = "/";
        
        for (const auto& [p, entry] : m_files) {
            std::string p_str = p.string();
            if (p_str == path_str_no_slash) continue;
            if (p_str.length() > path_str.length() && 
                p_str.substr(0, path_str.length()) == path_str) {
                std::string rest = p_str.substr(path_str.length());
                if (rest.find('/') == std::string::npos && !rest.empty()) {
                    result.push_back(p);
                }
            } else if (path_str_no_slash == "/" && p_str.length() > 1 && p_str[0] == '/' && 
                       p_str.find('/', 1) == std::string::npos) {
                result.push_back(p);
            }
        }
        // Symlinks are listed so the scanner can report them as skipped instead
        // of silently omitting them from the scan.
        for (const auto& link : m_symlinks) {
            std::string p_str = link.string();
            if (p_str.length() > path_str.length() &&
                p_str.substr(0, path_str.length()) == path_str) {
                std::string rest = p_str.substr(path_str.length());
                if (rest.find('/') == std::string::npos && !rest.empty()) {
                    result.push_back(link);
                }
            }
        }
        return result;
    }
    
    Result<std::unique_ptr<IReadableFile>> open_read(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "open_read"};
        }
        auto it = m_files.find(path);
        if (it == m_files.end()) {
            return Error{ErrorCode::NotFound, "File not found", path.string(), "open_read"};
        }
        if (it->second.is_dir) {
            return Error{ErrorCode::Unsupported, "Cannot read directory", path.string(), "open_read"};
        }
        bool fail = m_read_failures.count(path) && m_read_failures[path];
        return std::unique_ptr<IReadableFile>(new FakeReadableFile(it->second.content, fail));
    }
    
    Result<std::unique_ptr<IWritableFile>> open_write(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "open_write"};
        }
        if (m_files.count(path) && m_files[path].is_dir) {
            return Error{ErrorCode::Unsupported, "Cannot write to directory", path.string(), "open_write"};
        }
        if (m_files.find(path) == m_files.end()) {
            m_files[path] = FileEntry{false, std::vector<std::byte>(), true, true};
            ensure_parent_dirs(path);
        }
        return std::unique_ptr<IWritableFile>(new FakeWritableFile(m_files[path].content));
    }
    
    Result<void> create_directory(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "create_directory"};
        }
        if (m_files.count(path)) {
            return Error{ErrorCode::Conflict, "Already exists", path.string(), "create_directory"};
        }
        m_files[path] = FileEntry{true, std::vector<std::byte>(), true, true};
        ensure_parent_dirs(path);
        return Result<void>();
    }
    
    Result<void> remove(const std::filesystem::path& path) override {
        if (m_permission_denied.count(path)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", path.string(), "remove"};
        }
        if (m_files.find(path) == m_files.end()) {
            return Error{ErrorCode::NotFound, "Not found", path.string(), "remove"};
        }
        m_files.erase(path);
        return Result<void>();
    }
    
    Result<void> copy_file(const std::filesystem::path& from, const std::filesystem::path& to) override {
        if (m_permission_denied.count(from) || m_permission_denied.count(to)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", "", "copy_file"};
        }
        auto it = m_files.find(from);
        if (it == m_files.end()) {
            return Error{ErrorCode::NotFound, "Source not found", from.string(), "copy_file"};
        }
        if (m_files.count(to)) {
            return Error{ErrorCode::Conflict, "Destination exists", to.string(), "copy_file"};
        }
        m_files[to] = it->second;
        m_files[to].content = it->second.content;
        if (m_corrupt_copy && m_files[to].content.size() > 1) {
            m_files[to].content.pop_back();
        }
        ensure_parent_dirs(to);
        return Result<void>();
    }
    
    /// The fake filesystem models content only, so metadata transfer is a no-op.
    Result<void> copy_metadata(const std::filesystem::path&, const std::filesystem::path&) override {
        return Result<void>();
    }

    Result<void> rename(const std::filesystem::path& from, const std::filesystem::path& to) override {
        if (m_permission_denied.count(from) || m_permission_denied.count(to)) {
            return Error{ErrorCode::PermissionDenied, "Permission denied", "", "rename"};
        }
        auto it = m_files.find(from);
        if (it == m_files.end()) {
            return Error{ErrorCode::NotFound, "Source not found", from.string(), "rename"};
        }
        if (m_files.count(to)) {
            return Error{ErrorCode::Conflict, "Destination exists", to.string(), "rename"};
        }
        m_files[to] = it->second;
        m_files.erase(from);
        ensure_parent_dirs(to);
        return Result<void>();
    }
    
private:
    void ensure_parent_dirs(const std::filesystem::path& path) {
        if (path.has_parent_path()) {
            auto parent = path.parent_path();
            if (parent != path && !parent.empty() && parent.string() != "/") {
                if (m_files.find(parent) == m_files.end()) {
                    m_files[parent] = FileEntry{true, std::vector<std::byte>(), true, true};
                    ensure_parent_dirs(parent);
                }
            }
        }
    }
    
    std::map<std::filesystem::path, FileEntry> m_files;
    std::set<std::filesystem::path> m_symlinks;
    std::map<std::filesystem::path, bool> m_read_failures;
    std::set<std::filesystem::path> m_permission_denied;
    bool m_same_device = true;
    bool m_corrupt_copy = false;
};

} // namespace dupli
