#pragma once

#include "dupli/result.hpp"
#include "dupli/filesystem.hpp"
#include <string>
#include <vector>
#include <array>

extern "C" {
#include "blake3.h"
}

namespace dupli {

struct ContentDigest {
    std::string algorithm = "BLAKE3";
    std::array<uint8_t, 32> bytes;
    std::string hex;
    size_t bytes_hashed = 0;
    bool complete = false;
};

class IHasher {
public:
    virtual ~IHasher() = default;
    virtual Result<ContentDigest> hash_file(IReadableFile& file, size_t buffer_size = 65536) = 0;
    virtual Result<ContentDigest> hash_data(const std::vector<std::byte>& data) = 0;
};

class Blake3Hasher : public IHasher {
public:
    Blake3Hasher() = default;
    
    Result<ContentDigest> hash_file(IReadableFile& file, size_t buffer_size = 65536) override {
        if (buffer_size == 0) {
            return Error{ErrorCode::InvalidArgument, "Buffer size cannot be zero", "", "hash_file"};
        }
        
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        
        std::vector<std::byte> buffer(buffer_size);
        size_t total_bytes = 0;
        
        while (true) {
            auto read_result = file.read(buffer.data(), buffer_size);
            if (read_result.has_error()) {
                return read_result.error();
            }
            size_t bytes_read = read_result.value();
            if (bytes_read == 0) break;
            
            blake3_hasher_update(&hasher, buffer.data(), bytes_read);
            total_bytes += bytes_read;
        }
        
        ContentDigest digest;
        digest.algorithm = "BLAKE3";
        digest.bytes_hashed = total_bytes;
        digest.complete = true;
        blake3_hasher_finalize(&hasher, digest.bytes.data(), 32);
        
        // Convert to hex
        digest.hex.clear();
        for (int i = 0; i < 32; ++i) {
            char hex_buf[3];
            snprintf(hex_buf, sizeof(hex_buf), "%02x", digest.bytes[i]);
            digest.hex += hex_buf;
        }
        
        return digest;
    }
    
    Result<ContentDigest> hash_data(const std::vector<std::byte>& data) override {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, data.data(), data.size());
        
        ContentDigest digest;
        digest.algorithm = "BLAKE3";
        digest.bytes_hashed = data.size();
        digest.complete = true;
        blake3_hasher_finalize(&hasher, digest.bytes.data(), 32);
        
        digest.hex.clear();
        for (int i = 0; i < 32; ++i) {
            char hex_buf[3];
            snprintf(hex_buf, sizeof(hex_buf), "%02x", digest.bytes[i]);
            digest.hex += hex_buf;
        }
        
        return digest;
    }
};

} // namespace dupli
