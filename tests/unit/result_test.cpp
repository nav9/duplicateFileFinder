#include "dupli/result.hpp"
#include "dupli/fake_filesystem.hpp"
#include "dupli/hasher.hpp"
#include <cassert>
#include <iostream>
#include <cstring>

void test_result() {
    dupli::Result<int> r1(42);
    assert(r1.ok());
    assert(!r1.has_error());
    assert(r1.value() == 42);
    
    dupli::Error err;
    err.code = dupli::ErrorCode::NotFound;
    err.message = "File not found";
    dupli::Result<int> r2(err);
    assert(!r2.ok());
    assert(r2.has_error());
    assert(r2.error().code == dupli::ErrorCode::NotFound);
    
    dupli::Result<void> r3;
    assert(r3.ok());
    
    dupli::Result<void> r4(dupli::Error{dupli::ErrorCode::IoError, "IO failed", "", ""});
    assert(!r4.ok());
    assert(r4.has_error());
    
    std::cout << "Result tests passed\n";
}

void test_fake_filesystem() {
    dupli::FakeFileSystem fs;
    fs.add_file("/test/file1.txt", "hello");
    fs.add_directory("/test");
    fs.add_directory("/test/subdir");
    fs.add_file("/test/subdir/file2.txt", "world");
    
    auto exists1 = fs.exists("/test/file1.txt");
    assert(exists1.ok() && exists1.value() == true);
    
    auto exists2 = fs.exists("/nonexistent");
    assert(exists2.ok() && exists2.value() == false);
    
    auto isfile = fs.is_regular_file("/test/file1.txt");
    assert(isfile.ok() && isfile.value() == true);
    
    auto isdir = fs.is_directory("/test");
    assert(isdir.ok() && isdir.value() == true);
    
    auto list = fs.list_directory("/test");
    assert(list.ok());
    
    auto read_result = fs.open_read("/test/file1.txt");
    assert(read_result.ok());
    auto data = read_result.value()->read_all();
    assert(data.ok());
    assert(data.value().size() == 5);
    
    dupli::FakeFileSystem fs_perm;
    fs_perm.add_file("/test/file1.txt", "hello");
    fs_perm.set_permission_denied("/test/file1.txt", true);
    auto denied = fs_perm.open_read("/test/file1.txt");
    assert(denied.has_error());
    assert(denied.error().code == dupli::ErrorCode::PermissionDenied);
    
    dupli::FakeFileSystem fs2;
    fs2.add_file("/test/bad.txt", "content");
    fs2.set_read_failure("/test/bad.txt", true);
    auto read_fail = fs2.open_read("/test/bad.txt");
    assert(read_fail.ok());
    auto bad_data = read_fail.value()->read_all();
    assert(bad_data.has_error());
    
    std::cout << "Fake filesystem tests passed\n";
}

void test_hasher() {
    dupli::Blake3Hasher hasher;
    
    std::vector<std::byte> empty_data;
    auto empty_digest = hasher.hash_data(empty_data);
    assert(empty_digest.ok());
    assert(empty_digest.value().complete);
    assert(empty_digest.value().bytes_hashed == 0);
    assert(empty_digest.value().hex.length() == 64);
    
    std::vector<std::byte> hello_data;
    const char* hello = "hello";
    for (size_t i = 0; i < 5; ++i) {
        hello_data.push_back(static_cast<std::byte>(hello[i]));
    }
    auto hello_digest = hasher.hash_data(hello_data);
    assert(hello_digest.ok());
    assert(hello_digest.value().complete);
    assert(hello_digest.value().bytes_hashed == 5);
    
    std::vector<std::byte> hello_data2;
    for (size_t i = 0; i < 5; ++i) {
        hello_data2.push_back(static_cast<std::byte>(hello[i]));
    }
    auto hello_digest2 = hasher.hash_data(hello_data2);
    assert(hello_digest.ok() && hello_digest2.ok());
    assert(hello_digest.value().hex == hello_digest2.value().hex);
    
    std::vector<std::byte> world_data;
    const char* world = "world";
    for (size_t i = 0; i < 5; ++i) {
        world_data.push_back(static_cast<std::byte>(world[i]));
    }
    auto world_digest = hasher.hash_data(world_data);
    assert(world_digest.ok());
    assert(hello_digest.value().hex != world_digest.value().hex);
    
    dupli::FakeFileSystem fs;
    fs.add_file("/test/file.txt", "hello");
    auto file_result = fs.open_read("/test/file.txt");
    assert(file_result.ok());
    auto file_digest = hasher.hash_file(*file_result.value());
    assert(file_digest.ok());
    assert(file_digest.value().complete);
    assert(file_digest.value().bytes_hashed == 5);
    
    auto file_result2 = fs.open_read("/test/file.txt");
    assert(file_result2.ok());
    auto bad_digest = hasher.hash_file(*file_result2.value(), 0);
    assert(bad_digest.has_error());
    
    std::cout << "Hasher tests passed\n";
}

int main() {
    test_result();
    test_fake_filesystem();
    test_hasher();
    std::cout << "All tests passed\n";
    return 0;
}
