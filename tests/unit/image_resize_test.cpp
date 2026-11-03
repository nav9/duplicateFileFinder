#include "dupli/fake_filesystem.hpp"
#include "dupli/image_resize.hpp"
#include "test_utils.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

/// Minimal in-memory image format: "IMG1", width, height (little endian) and
/// RGBA bytes. It exists to exercise the resize service without an image
/// library; the production codec is the wxWidgets-backed one in the GUI.
class FakeCodec : public IImageCodec {
public:
    bool fail_decode = false;
    bool fail_encode = false;
    bool allow_encode = true;
    bool animated = false;

    std::vector<ImageFormat> supported_formats() const override {
        return {ImageFormat::Png, ImageFormat::Bmp};
    }

    bool can_encode(ImageFormat format) const override {
        return allow_encode && (format == ImageFormat::Png || format == ImageFormat::Bmp);
    }

    Result<RasterImage> decode(const std::vector<std::byte>& bytes, ImageFormat hint,
                               const ImageLimits& limits) override {
        if (fail_decode) {
            return Error{ErrorCode::Unsupported, "Decoder refused the file", "", "decode"};
        }
        if (bytes.size() < 16 || std::memcmp(bytes.data(), "IMG1", 4) != 0) {
            return Error{ErrorCode::Unsupported, "Not a fake image", "", "decode"};
        }
        uint32_t width = 0;
        uint32_t height = 0;
        std::memcpy(&width, bytes.data() + 4, 4);
        std::memcpy(&height, bytes.data() + 8, 4);
        if (width == 0 || height == 0) {
            return Error{ErrorCode::Unsupported, "Empty image", "", "decode"};
        }
        if (width > limits.max_dimension || height > limits.max_dimension) {
            return Error{ErrorCode::Unsupported, "Image dimension exceeds the limit", "", "decode"};
        }
        if (static_cast<uint64_t>(width) * height > limits.max_pixels) {
            return Error{ErrorCode::Unsupported, "Image pixel count exceeds the limit", "", "decode"};
        }
        const size_t expected = 16 + static_cast<size_t>(width) * height * 4;
        if (bytes.size() != expected) {
            return Error{ErrorCode::Unsupported, "Truncated image data", "", "decode"};
        }
        RasterImage image;
        image.width = width;
        image.height = height;
        image.format = hint;
        image.has_alpha = true;
        image.animated = animated;
        image.rgba.resize(static_cast<size_t>(width) * height * 4);
        std::memcpy(image.rgba.data(), bytes.data() + 16, image.rgba.size());
        return image;
    }

    Result<std::vector<std::byte>> encode(const RasterImage& image, ImageFormat format,
                                          int quality) override {
        (void)quality;
        if (fail_encode) {
            return Error{ErrorCode::IoError, "Encoder failed", "", "encode"};
        }
        if (!can_encode(format)) {
            return Error{ErrorCode::Unsupported, "Cannot encode this format", "", "encode"};
        }
        std::vector<std::byte> bytes(16 + image.rgba.size());
        std::memcpy(bytes.data(), "IMG1", 4);
        std::memcpy(bytes.data() + 4, &image.width, 4);
        std::memcpy(bytes.data() + 8, &image.height, 4);
        std::memcpy(bytes.data() + 16, image.rgba.data(), image.rgba.size());
        return bytes;
    }

    std::string name() const override { return "fake"; }
};

std::vector<std::byte> encode_rgba(uint32_t width, uint32_t height,
                                   const std::vector<uint8_t>& rgba) {
    std::vector<std::byte> bytes(16 + rgba.size());
    std::memcpy(bytes.data(), "IMG1", 4);
    std::memcpy(bytes.data() + 4, &width, 4);
    std::memcpy(bytes.data() + 8, &height, 4);
    std::memcpy(bytes.data() + 16, rgba.data(), rgba.size());
    return bytes;
}

std::vector<uint8_t> solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = r;
        rgba[i + 1] = g;
        rgba[i + 2] = b;
        rgba[i + 3] = a;
    }
    return rgba;
}

void test_target_size() {
    ImageResizeRule maxRule;
    maxRule.mode = ImageResizeRule::Mode::MaxDimensions;
    maxRule.max_width = 1920;
    maxRule.max_height = 1080;

    auto landscape = compute_target_size(4000, 2000, maxRule);
    CHECK(landscape.ok());
    CHECK_EQ(landscape.value().width, uint32_t(1920));
    CHECK_EQ(landscape.value().height, uint32_t(960));

    auto portrait = compute_target_size(2000, 4000, maxRule);
    CHECK(portrait.ok());
    CHECK_EQ(portrait.value().width, uint32_t(540));
    CHECK_EQ(portrait.value().height, uint32_t(1080));

    // No upscaling by default.
    auto small = compute_target_size(100, 50, maxRule);
    CHECK(small.ok());
    CHECK_EQ(small.value().width, uint32_t(100));
    CHECK_EQ(small.value().height, uint32_t(50));

    maxRule.allow_upscale = true;
    auto upscaled = compute_target_size(100, 50, maxRule);
    CHECK_EQ(upscaled.value().width, uint32_t(1920));

    ImageResizeRule percent;
    percent.mode = ImageResizeRule::Mode::Percent;
    percent.percent = 50.0;
    auto half = compute_target_size(101, 51, percent);
    CHECK(half.ok());
    CHECK_EQ(half.value().width, uint32_t(51));
    CHECK_EQ(half.value().height, uint32_t(26));

    percent.percent = 0.0;
    CHECK(compute_target_size(100, 100, percent).has_error());
    percent.percent = 50.0;
    CHECK(compute_target_size(0, 100, percent).has_error());

    ImageResizeRule none;
    none.max_width = 0;
    none.max_height = 0;
    CHECK(compute_target_size(100, 100, none).has_error());
    std::cout << "image: target size\n";
}

void test_bilinear_resize() {
    RasterImage source;
    source.width = 4;
    source.height = 4;
    source.format = ImageFormat::Png;
    source.has_alpha = true;
    source.rgba = solid(4, 4, 200, 100, 50, 255);

    RasterImage resized = resize_bilinear(source, 2, 2);
    CHECK(resized.valid());
    CHECK_EQ(resized.width, uint32_t(2));
    CHECK_EQ(resized.height, uint32_t(2));
    for (size_t i = 0; i < resized.rgba.size(); i += 4) {
        CHECK_EQ(int(resized.rgba[i]), 200);
        CHECK_EQ(int(resized.rgba[i + 1]), 100);
        CHECK_EQ(int(resized.rgba[i + 2]), 50);
        CHECK_EQ(int(resized.rgba[i + 3]), 255);
    }

    // A fully transparent image stays fully transparent; no colour is invented.
    RasterImage transparent;
    transparent.width = 4;
    transparent.height = 4;
    transparent.has_alpha = true;
    transparent.rgba = solid(4, 4, 10, 20, 30, 0);
    RasterImage shrunk = resize_bilinear(transparent, 2, 2);
    CHECK(shrunk.valid());
    for (size_t i = 0; i < shrunk.rgba.size(); i += 4) {
        CHECK_EQ(int(shrunk.rgba[i + 3]), 0);
    }

    // An invalid target produces an invalid image rather than a crash.
    RasterImage broken = resize_bilinear(source, 0, 2);
    CHECK(!broken.valid());
    std::cout << "image: bilinear resize\n";
}

struct Fixture {
    FakeFileSystem fs;
    FakeCodec codec;
    Blake3Hasher hasher;
    ImageResizeService service{fs, codec, hasher};

    Fixture() {
        fs.add_file_bytes("/root/pic.png",
                          encode_rgba(100, 50, solid(100, 50, 40, 80, 120, 255)));
        fs.add_file_bytes("/root/sub/small.bmp",
                          encode_rgba(10, 10, solid(10, 10, 1, 2, 3, 255)));
        fs.add_file("/root/readme.txt", "not an image");
    }
};

void test_discovery() {
    Fixture fixture;
    ScanOptions options;
    options.roots = {"/root"};

    auto discovered = fixture.service.discover(options, ImageLimits{});
    CHECK(discovered.ok());
    CHECK_EQ(discovered.value().candidates.size(), size_t(2));
    CHECK(discovered.value().complete);
    for (const auto& candidate : discovered.value().candidates) {
        CHECK(candidate.supported);
        CHECK(candidate.width > 0);
        CHECK(candidate.height > 0);
        CHECK_EQ(candidate.status, std::string("Ready"));
    }
    std::cout << "image: discovery\n";
}

void test_copy_mode() {
    Fixture fixture;
    ImageResizeRequest request;
    request.source = "/root/pic.png";
    request.output_directory = "/out";
    request.mode = ImageOutputMode::CreateCopies;
    request.rule.max_width = 40;
    request.rule.max_height = 40;

    auto resized = fixture.service.resize(request);
    CHECK(resized.ok());
    const ImageResizeOutcome& outcome = resized.value();
    CHECK(outcome.ok);
    CHECK(!outcome.skipped);
    CHECK_EQ(outcome.output_width, uint32_t(40));
    CHECK_EQ(outcome.output_height, uint32_t(20));
    CHECK_EQ(outcome.output, std::filesystem::path("/out/pic_resized.png"));
    CHECK_EQ(outcome.output_digest.size(), size_t(64));
    CHECK_EQ(outcome.input_digest.size(), size_t(64));

    // The original is byte-for-byte untouched.
    auto original = fixture.fs.open_read("/root/pic.png");
    CHECK(original.ok());
    auto original_bytes = original.value()->read_all();
    CHECK(original_bytes.ok());
    auto decoded_original = fixture.codec.decode(original_bytes.value(), ImageFormat::Png,
                                                 ImageLimits{});
    CHECK(decoded_original.ok());
    CHECK_EQ(decoded_original.value().width, uint32_t(100));

    auto output = fixture.fs.open_read("/out/pic_resized.png");
    CHECK(output.ok());
    auto output_bytes = output.value()->read_all();
    CHECK(output_bytes.ok());
    auto decoded_output = fixture.codec.decode(output_bytes.value(), ImageFormat::Png,
                                               ImageLimits{});
    CHECK(decoded_output.ok());
    CHECK_EQ(decoded_output.value().width, uint32_t(40));
    CHECK_EQ(decoded_output.value().height, uint32_t(20));

    auto leftover = fixture.fs.exists("/out/pic_resized.png.dupli-tmp");
    CHECK(leftover.ok() && !leftover.value());

    // Already-small images are reported as skipped, with no output file.
    ImageResizeRequest small;
    small.source = "/root/sub/small.bmp";
    small.output_directory = "/out";
    small.rule.max_width = 1920;
    small.rule.max_height = 1080;
    auto skipped = fixture.service.resize(small);
    CHECK(skipped.ok());
    CHECK(skipped.value().skipped);
    auto no_output = fixture.fs.exists("/out/small_resized.bmp");
    CHECK(no_output.ok() && !no_output.value());
    std::cout << "image: copy mode\n";
}

void test_collisions_and_limits() {
    Fixture fixture;
    fixture.fs.add_file("/out/pic_resized.png", "occupied");

    ImageResizeRequest request;
    request.source = "/root/pic.png";
    request.output_directory = "/out";
    request.rule.max_width = 50;
    request.rule.max_height = 50;
    auto resized = fixture.service.resize(request);
    CHECK(resized.ok());
    CHECK_EQ(resized.value().output, std::filesystem::path("/out/pic_resized_1.png"));

    // Oversized decoded pixel counts are refused before allocating.
    fixture.fs.add_file_bytes("/root/huge.png",
                              encode_rgba(100, 100, solid(100, 100, 9, 9, 9, 255)));
    ImageResizeRequest huge;
    huge.source = "/root/huge.png";
    huge.output_directory = "/out";
    huge.limits.max_pixels = 1000;
    auto refused = fixture.service.resize(huge);
    CHECK(refused.has_error());
    CHECK_EQ(refused.error().code, ErrorCode::Unsupported);

    // Animated/multi-page images are refused instead of being flattened.
    fixture.fs.add_file_bytes("/root/anim.gif",
                              encode_rgba(20, 20, solid(20, 20, 4, 4, 4, 255)));
    fixture.codec.animated = true;
    ImageResizeRequest animated;
    animated.source = "/root/anim.gif";
    animated.output_directory = "/out";
    auto refused_animation = fixture.service.resize(animated);
    CHECK(refused_animation.has_error());
    CHECK_EQ(refused_animation.error().code, ErrorCode::Unsupported);
    fixture.codec.animated = false;

    // A source that changed since it was previewed is refused as stale.
    ImageResizeRequest stale;
    stale.source = "/root/pic.png";
    stale.output_directory = "/out3";
    stale.rule.max_width = 20;
    stale.rule.max_height = 20;
    stale.expected_input_digest = std::string(64, '0');
    auto stale_result = fixture.service.resize(stale);
    CHECK(stale_result.has_error());
    CHECK_EQ(stale_result.error().code, ErrorCode::Stale);

    // A corrupt image is an error, never a silently skipped success.
    fixture.fs.add_file("/root/corrupt.png", "not an image");
    ImageResizeRequest corrupt;
    corrupt.source = "/root/corrupt.png";
    corrupt.output_directory = "/out";
    auto failed = fixture.service.resize(corrupt);
    CHECK(failed.has_error());

    // An encoder that cannot write the source format is refused rather than
    // silently converting the file.
    fixture.codec.allow_encode = false;
    ImageResizeRequest unsupported = request;
    unsupported.output_directory = "/out2";
    auto cannot = fixture.service.resize(unsupported);
    CHECK(cannot.has_error());
    CHECK_EQ(cannot.error().code, ErrorCode::Unsupported);
    std::cout << "image: collisions and limits\n";
}

void test_in_place_and_undo() {
    Fixture fixture;
    const auto original_bytes = encode_rgba(100, 50, solid(100, 50, 40, 80, 120, 255));

    ImageResizeRequest request;
    request.source = "/root/pic.png";
    request.mode = ImageOutputMode::ResizeInPlace;
    request.rule.mode = ImageResizeRule::Mode::Percent;
    request.rule.percent = 50.0;

    auto resized = fixture.service.resize(request);
    CHECK(resized.ok());
    const ImageResizeOutcome& outcome = resized.value();
    CHECK(!outcome.backup.empty());
    CHECK_EQ(outcome.output, std::filesystem::path("/root/pic.png"));

    auto backup_exists = fixture.fs.exists(outcome.backup);
    CHECK(backup_exists.ok() && backup_exists.value());

    auto current = fixture.fs.open_read("/root/pic.png");
    CHECK(current.ok());
    auto current_bytes = current.value()->read_all();
    CHECK(current_bytes.ok());
    CHECK_EQ(current_bytes.value().size(), size_t(16 + 50 * 25 * 4));

    auto undone = fixture.service.undo_in_place(outcome);
    CHECK(undone.ok());
    auto restored = fixture.fs.open_read("/root/pic.png");
    CHECK(restored.ok());
    auto restored_bytes = restored.value()->read_all();
    CHECK(restored_bytes.ok());
    CHECK(restored_bytes.value() == original_bytes);
    auto backup_gone = fixture.fs.exists(outcome.backup);
    CHECK(backup_gone.ok() && !backup_gone.value());

    // Undoing again is refused: there is no backup any more.
    auto again = fixture.service.undo_in_place(outcome);
    CHECK(again.has_error());
    std::cout << "image: in-place and undo\n";
}

} // namespace

int main() {
    test_target_size();
    test_bilinear_resize();
    test_discovery();
    test_copy_mode();
    test_collisions_and_limits();
    test_in_place_and_undo();
    std::cout << "All image resize tests passed\n";
    return 0;
}
