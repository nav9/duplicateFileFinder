#pragma once

#include "dupli/filesystem.hpp"
#include <filesystem>

#include <fstream>
namespace dupli {

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
    
    Result<void> rename(const std::filesystem::path& from, const std::filesystem::path& to) override {
        try {
            if (std::filesystem::is_symlink(from)) {
                return Error{ErrorCode::Unsupported, "Symlinks are not moved", from.string(), "rename"};
            }
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
