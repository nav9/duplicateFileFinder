#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"
#include "dupli/scanner.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace dupli {

// Image resizing, split so that the parts that need real care are testable
// without an image library:
//
//  * RasterImage + resize_bilinear() + compute_target_size() are pure, fully
//    deterministic and unit-tested (including alpha handling).
//  * IImageCodec is the only component that knows a specific decoder/encoder.
//    The GUI supplies a wxWidgets-backed codec; tests supply a fake one.
//  * ImageResizeService enforces the safety rules: file-size and decoded-pixel
//    limits before decoding, aspect ratio and no-upscale by default, temporary
//    output verified before publication, never overwriting, and an explicit
//    backup when resizing in place.

enum class ImageFormat { Unknown, Png, Jpeg, Bmp, Gif, Tiff, Webp };

std::string to_string(ImageFormat format);
ImageFormat image_format_from_path(const std::filesystem::path& path);

struct ImageLimits {
    /// Refuse to read a source larger than this; protects against loading a disk
    /// image because a file was renamed to .png.
    uintmax_t max_file_bytes = 64ull * 1024ull * 1024ull;
    /// Refuse to decode more pixels than this; protects against decompression
    /// bombs that are small on disk but huge in memory.
    uint32_t max_pixels = 40u * 1000u * 1000u;
    uint32_t max_dimension = 20000;
};

struct RasterImage {
    uint32_t width = 0;
    uint32_t height = 0;
    ImageFormat format = ImageFormat::Unknown;
    bool has_alpha = false;
    bool animated = false;
    /// Row-major RGBA, 4 bytes per pixel.
    std::vector<uint8_t> rgba;

    bool valid() const {
        return width > 0 && height > 0 &&
               rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
    uint64_t pixel_count() const {
        return static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    }
};

/// The only component allowed to know an image library. Implementations must
/// honour the limits *before* allocating decoded buffers.
class IImageCodec {
public:
    virtual ~IImageCodec() = default;

    /// Formats this codec can read and write. The UI lists only these.
    virtual std::vector<ImageFormat> supported_formats() const = 0;
    virtual bool can_encode(ImageFormat format) const = 0;

    /// Decode complete file bytes. `hint` is derived from the file extension and
    /// is only a hint: the codec must validate the actual content.
    virtual Result<RasterImage> decode(const std::vector<std::byte>& bytes, ImageFormat hint,
                                       const ImageLimits& limits) = 0;

    /// Encode to the requested format and return the encoded bytes. `quality` is
    /// used by lossy formats and ignored by lossless ones.
    virtual Result<std::vector<std::byte>> encode(const RasterImage& image, ImageFormat format,
                                                  int quality) = 0;

    /// Diagnostic name, e.g. "wxWidgets image handlers".
    virtual std::string name() const = 0;
};

struct ImageDimensions {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImageResizeRule {
    enum class Mode { MaxDimensions, Percent };
    Mode mode = Mode::MaxDimensions;
    uint32_t max_width = 1920;
    uint32_t max_height = 1080;
    double percent = 50.0;
    /// Off by default: enlarging an image adds bytes but no detail, and the
    /// original is usually the better copy.
    bool allow_upscale = false;
};

/// Pure aspect-ratio arithmetic with explicit validation. Never returns a zero
/// dimension; refuses a rule that cannot mean anything.
Result<ImageDimensions> compute_target_size(uint32_t width, uint32_t height,
                                            const ImageResizeRule& rule);

/// Bilinear resampling in premultiplied alpha space so transparent pixels do not
/// bleed their (meaningless) colour into visible neighbours. Edges use clamped
/// sampling, so no border darkening appears.
RasterImage resize_bilinear(const RasterImage& source, uint32_t target_width,
                            uint32_t target_height);

enum class ImageOutputMode {
    /// Default: originals are untouched; resized files are new copies.
    CreateCopies,
    /// Advanced and destructive: the original is replaced after a verified
    /// backup is made, so an undo remains possible.
    ResizeInPlace
};

struct ImageResizeRequest {
    std::filesystem::path source;
    std::filesystem::path output_directory; // required for CreateCopies
    ImageResizeRule rule;
    ImageOutputMode mode = ImageOutputMode::CreateCopies;
    ImageLimits limits;
    int quality = 90; // lossy formats only; clamped to 1..100
    bool preserve_metadata = true;
    /// Digest of the source as it was when it was previewed. When non-empty it is
    /// re-checked before encoding: a file that changed in between is refused as
    /// stale instead of silently resizing different content.
    std::string expected_input_digest;
};

struct ImageResizeOutcome {
    bool ok = false;
    /// True when nothing needed to be written (the image already satisfies the
    /// rule). This is a success, but the UI must not claim a file was created.
    bool skipped = false;
    std::filesystem::path source;
    std::filesystem::path output;
    /// Backup of the original for ResizeInPlace; empty for copies.
    std::filesystem::path backup;
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    uintmax_t input_bytes = 0;
    uintmax_t output_bytes = 0;
    std::string input_digest;
    std::string output_digest;
    std::string message;
};

struct ImageCandidate {
    std::filesystem::path path;
    uintmax_t file_size = 0;
    ImageFormat format = ImageFormat::Unknown;
    bool supported = false;
    /// Dimensions are filled in only when the codec could probe the file within
    /// the limits; 0 means "not known yet" rather than "zero-sized".
    uint32_t width = 0;
    uint32_t height = 0;
    std::string status;
};

struct ImageDiscoveryResult {
    std::vector<ImageCandidate> candidates;
    std::vector<ScanIssue> issues;
    std::vector<std::string> warnings;
    /// False when any folder or file could not be inspected.
    bool complete = true;
};

class ImageResizeService {
public:
    ImageResizeService(IFileSystem& fs, IImageCodec& codec, IHasher& hasher)
        : m_fs(fs), m_codec(codec), m_hasher(hasher) {}

    /// Recursively list image files below the selected roots using the link-safe
    /// scanner. Only formats the codec can actually read are reported as
    /// supported; everything else is listed with an explanation.
    Result<ImageDiscoveryResult> discover(const ScanOptions& options,
                                          const ImageLimits& limits) const;

    /// Resize one image. Writes to a sibling temporary file, verifies that the
    /// encoded result decodes to the expected dimensions, then publishes with a
    /// no-overwrite rename. In place mode the original is first renamed to a
    /// backup, so the operation can be undone.
    Result<ImageResizeOutcome> resize(const ImageResizeRequest& request) const;

    /// Reverse an in-place resize: verifies the current file still matches the
    /// recorded output digest, then restores the backup. Never overwrites an
    /// unrelated file.
    Result<void> undo_in_place(const ImageResizeOutcome& outcome) const;

    /// Default non-conflicting output path: "<name>_resized.<ext>" in `directory`.
    std::filesystem::path choose_output_path(const std::filesystem::path& directory,
                                             const std::filesystem::path& source) const;

private:
    Result<std::vector<std::byte>> read_all_bytes(const std::filesystem::path& path,
                                                  uintmax_t max_bytes) const;
    Result<void> write_bytes(const std::filesystem::path& path,
                             const std::vector<std::byte>& bytes) const;

    IFileSystem& m_fs;
    IImageCodec& m_codec;
    IHasher& m_hasher;
};

} // namespace dupli
