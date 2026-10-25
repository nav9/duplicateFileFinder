#include "dupman/result.hpp"
#include "dupman/fake_filesystem.hpp"
#include <cassert>
#include <iostream>
#include <cstring>

void test_result() {
    // Test successful result
    dupman::Result<int> r1(42);
    assert(r1.ok());
    assert(!r1.has_error());
    assert(r1.value() == 42);
    
    // Test error result
    dupman::Error err;
    err.code = dupman::ErrorCode::NotFound;
    err.message = "File not found";
    dupman::Result<int> r2(err);
    assert(!r2.ok());
    assert(r2.has_error());
    assert(r2.error().code == dupman::ErrorCode::NotFound);
    
    // Test void result
    dupman::Result<void> r3;
    assert(r3.ok());
    
    dupman::Result<void> r4(dupman::Error{dupman::ErrorCode::IoError, "IO failed", "", ""});
    assert(!r4.ok());
    assert(r4.has_error());
    
    std::cout << "Result tests passed\n";
}

void test_fake_filesystem() {
    dupman::FakeFileSystem fs;
    fs.add_file("/test/file1.txt", "hello");
    fs.add_directory("/test");
    fs.add_directory("/test/subdir");
    fs.add_file("/test/subdir/file2.txt", "world");
    
    // Test exists
    auto exists1 = fs.exists("/test/file1.txt");
    assert(exists1.ok() && exists1.value() == true);
    
    auto exists2 = fs.exists("/nonexistent");
    assert(exists2.ok() && exists2.value() == false);
    
    // Test is_regular_file
    auto isfile = fs.is_regular_file("/test/file1.txt");
    assert(isfile.ok() && isfile.value() == true);
    
    auto isdir = fs.is_directory("/test");
    assert(isdir.ok() && isdir.value() == true);
    
    // Test list directory
    auto list = fs.list_directory("/test");
    assert(list.ok());
    assert(list.value().size() >= 1);
    
    // Test read
    auto read_result = fs.open_read("/test/file1.txt");
    assert(read_result.ok());
    auto data = read_result.value()->read_all();
    assert(data.ok());
    assert(data.value().size() == 5);
    
    // Test permission denied
    dupman::FakeFileSystem fs_perm;
    fs_perm.add_file("/test/file1.txt", "hello");
    fs_perm.set_permission_denied("/test/file1.txt", true);
    auto denied = fs_perm.open_read("/test/file1.txt");
    assert(denied.has_error());
    assert(denied.error().code == dupman::ErrorCode::PermissionDenied);
    
    // Test read failure
    dupman::FakeFileSystem fs2;
    fs2.add_file("/test/bad.txt", "content");
    fs2.set_read_failure("/test/bad.txt", true);
    auto read_fail = fs2.open_read("/test/bad.txt");
    assert(read_fail.ok());
    auto bad_data = read_fail.value()->read_all();
    assert(bad_data.has_error());
    
    std::cout << "Fake filesystem tests passed\n";
}

int main() {
    test_result();
    test_fake_filesystem();
    std::cout << "All tests passed\n";
    return 0;
}
