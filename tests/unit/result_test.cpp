#include "dupli/result.hpp"
#include "dupli/fake_filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/grouping.hpp"
#include "dupli/fingerprint.hpp"
#include "dupli/classifier.hpp"
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

void test_grouping() {
    dupli::FakeFileSystem fs;
    dupli::Blake3Hasher hasher;
    dupli::DuplicateGrouper grouper(fs, hasher);
    
    fs.add_file("/test/a.txt", "hello");
    fs.add_file("/test/b.txt", "hello");
    fs.add_file("/test/c.txt", "world");
    
    std::vector<std::filesystem::path> paths = {"/test/a.txt", "/test/b.txt", "/test/c.txt"};
    auto groups = grouper.group_duplicates(paths);
    assert(groups.ok());
    assert(groups.value().size() == 1);
    assert(groups.value()[0].files.size() == 2);
    
    dupli::FakeFileSystem fs2;
    fs2.add_file("/test/e1.txt", "");
    fs2.add_file("/test/e2.txt", "");
    dupli::DuplicateGrouper grouper2(fs2, hasher);
    std::vector<std::filesystem::path> paths2 = {"/test/e1.txt", "/test/e2.txt"};
    auto groups2 = grouper2.group_duplicates(paths2);
    assert(groups2.ok());
    assert(groups2.value().size() == 1);
    
    dupli::FakeFileSystem fs3;
    fs3.add_file("/test/x.txt", "abc");
    fs3.add_file("/test/y.txt", "def");
    dupli::DuplicateGrouper grouper3(fs3, hasher);
    std::vector<std::filesystem::path> paths3 = {"/test/x.txt", "/test/y.txt"};
    auto groups3 = grouper3.group_duplicates(paths3);
    assert(groups3.ok());
    assert(groups3.value().size() == 0);
    
    dupli::FakeFileSystem fs4;
    fs4.add_file("/test/good.txt", "hello");
    fs4.add_file("/test/bad.txt", "hello");
    fs4.set_read_failure("/test/bad.txt", true);
    dupli::DuplicateGrouper grouper4(fs4, hasher);
    std::vector<std::filesystem::path> paths4 = {"/test/good.txt", "/test/bad.txt"};
    auto groups4 = grouper4.group_duplicates(paths4);
    assert(groups4.ok());
    assert(groups4.value().size() == 0);
    
    std::cout << "Grouping tests passed\n";
}

void test_fingerprint() {
    dupli::FakeFileSystem fs;
    dupli::Blake3Hasher hasher;
    dupli::FolderFingerprinter fp(fs, hasher);
    
    fs.add_directory("/tree1");
    fs.add_file("/tree1/a.txt", "hello");
    fs.add_directory("/tree1/sub");
    fs.add_file("/tree1/sub/b.txt", "world");
    
    fs.add_directory("/tree2");
    fs.add_file("/tree2/a.txt", "hello");
    fs.add_directory("/tree2/sub");
    fs.add_file("/tree2/sub/b.txt", "world");
    
    auto fp1 = fp.fingerprint_tree("/tree1");
    auto fp2_res = fp.fingerprint_tree("/tree2");
    assert(fp1.ok() && fp2_res.ok());
    assert(fp1.value().complete && fp2_res.value().complete);
    assert(fp1.value().fingerprint == fp2_res.value().fingerprint);
    
    dupli::FakeFileSystem fs2;
    dupli::FolderFingerprinter fp2(fs2, hasher);
    fs2.add_directory("/tree1");
    fs2.add_file("/tree1/a.txt", "hello");
    fs2.add_directory("/tree2");
    fs2.add_file("/tree2/a.txt", "HELLO");
    
    auto fpa = fp2.fingerprint_tree("/tree1");
    auto fpb = fp2.fingerprint_tree("/tree2");
    assert(fpa.ok() && fpb.ok());
    assert(fpa.value().fingerprint != fpb.value().fingerprint);
    
    dupli::FakeFileSystem fs3;
    dupli::FolderFingerprinter fp3(fs3, hasher);
    fs3.add_directory("/empty1");
    fs3.add_directory("/empty2");
    auto fpe1 = fp3.fingerprint_tree("/empty1");
    auto fpe2 = fp3.fingerprint_tree("/empty2");
    assert(fpe1.ok() && fpe2.ok());
    assert(fpe1.value().fingerprint == fpe2.value().fingerprint);
    
    dupli::FakeFileSystem fs4;
    dupli::FolderFingerprinter fp4(fs4, hasher);
    fs4.add_directory("/tree");
    fs4.add_file("/tree/a.txt", "hello");
    fs4.set_read_failure("/tree/a.txt", true);
    auto fpi = fp4.fingerprint_tree("/tree");
    assert(fpi.ok());
    assert(!fpi.value().complete);
    
    std::cout << "Fingerprint tests passed\n";
}

void test_classifier() {
    dupli::FakeFileSystem fs;
    dupli::Blake3Hasher hasher;
    dupli::RelationshipClassifier classifier(fs, hasher);
    
    fs.add_file("/a/file1.txt", "hello");
    fs.add_file("/b/file1.txt", "hello");
    std::vector<std::filesystem::path> paths = {"/a/file1.txt", "/b/file1.txt"};
    auto rels = classifier.classify(paths);
    assert(rels.ok());
    assert(rels.value().size() >= 1);
    
    bool found_exact = false;
    for (const auto& r : rels.value()) {
        if (r.kind == dupli::RelationshipKind::ExactFileDuplicate) {
            found_exact = true;
            break;
        }
    }
    assert(found_exact);
    
    dupli::FakeFileSystem fs2;
    dupli::RelationshipClassifier classifier2(fs2, hasher);
    fs2.add_directory("/tree1");
    fs2.add_file("/tree1/a.txt", "test");
    fs2.add_directory("/tree2");
    fs2.add_file("/tree2/a.txt", "test");
    std::vector<std::filesystem::path> paths2 = {"/tree1", "/tree2"};
    auto rels2 = classifier2.classify(paths2);
    assert(rels2.ok());
    
    std::cout << "Classifier tests passed\n";
}

int main() {
    test_result();
    test_fake_filesystem();
    test_hasher();
    test_grouping();
    test_fingerprint();
    test_classifier();
    std::cout << "All tests passed\n";
    return 0;
}
