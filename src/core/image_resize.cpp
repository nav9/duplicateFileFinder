#include "dupli/image_resize.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace dupli {

namespace {

constexpr uint32_t kMinDimension = 1;
constexpr uint32_t kMaxPercent = 10000;

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowercase(std::string text) {
    for (char& c : text) c = ascii_lower(c);
    return text;
}

/// Non-conflicting "<stem>_resized<ext>" name. The suffix loop is deterministic
/// so the same inputs always produce the same plan/output name.
std::filesystem::path unique_output_path(IFileSystem& fs, const std::filesystem::path& directory,
                                         const std::filesystem::path& source) {
    const std::string stem = source.stem().string();
    const std::string extension = source.extension().string();
    auto available = [&fs](const std::filesystem::path& candidate) {
        auto exists = fs.exists(candidate);
        return exists.ok() && !exists.value();
    };

    std::filesystem::path candidate = directory / (stem + "_resized" + extension);
    if (available(candidate)) return candidate;
    for (int suffix = 1; suffix < 100000; ++suffix) {
        candidate = directory / (stem + "_resized_" + std::to_string(suffix) + extension);
        if (available(candidate)) return candidate;
    }
    return candidate;
}

uint32_t scale_dimension(double value) {
    if (!(value > 0.0)) return kMinDimension;
    const double rounded = std::floor(value + 0.5);
    if (rounded < static_cast<double>(kMinDimension)) return kMinDimension;
    if (rounded > static_cast<double>(UINT32_MAX)) return UINT32_MAX;
    return static_cast<uint32_t>(rounded);
}

} // namespace

std::string to_string(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png: return "PNG";
    case ImageFormat::Jpeg: return "JPEG";
    case ImageFormat::Bmp: return "BMP";
    case ImageFormat::Gif: return "GIF";
    case ImageFormat::Tiff: return "TIFF";
    case ImageFormat::Webp: return "WebP";
    case ImageFormat::Unknown: return "unknown";
    }
    return "unknown";
}

ImageFormat image_format_from_path(const std::filesystem::path& path) {
    const std::string extension = lowercase(path.extension().string());
    if (extension == ".png") return ImageFormat::Png;
    if (extension == ".jpg" || extension == ".jpeg" || extension == ".jpe") {
        return ImageFormat::Jpeg;
    }
    if (extension == ".bmp") return ImageFormat::Bmp;
    if (extension == ".gif") return ImageFormat::Gif;
    if (extension == ".tif" || extension == ".tiff") return ImageFormat::Tiff;
    if (extension == ".webp") return ImageFormat::Webp;
    return ImageFormat::Unknown;
}

Result<ImageDimensions> compute_target_size(uint32_t width, uint32_t height,
                                            const ImageResizeRule& rule) {
    if (width == 0 || height == 0) {
        return Error{ErrorCode::InvalidArgument, "The image has no pixels", "", "compute_target"};
    }

    double scale = 1.0;
    if (rule.mode == ImageResizeRule::Mode::Percent) {
        if (!(rule.percent > 0.0) || rule.percent > static_cast<double>(kMaxPercent)) {
            return Error{ErrorCode::InvalidArgument,
                         "The percentage must be greater than 0 and at most 10000", "",
                         "compute_target"};
        }
        scale = rule.percent / 100.0;
    } else {
        if (rule.max_width == 0 && rule.max_height == 0) {
            return Error{ErrorCode::InvalidArgument,
                         "Set a maximum width, a maximum height, or both", "", "compute_target"};
        }
        const double width_scale =
            rule.max_width == 0 ? 1e9
                                : static_cast<double>(rule.max_width) / static_cast<double>(width);
        const double height_scale =
            rule.max_height == 0 ? 1e9
                                 : static_cast<double>(rule.max_height) / static_cast<double>(height);
        scale = std::min(width_scale, height_scale);
    }

    if (scale >= 1.0 && !rule.allow_upscale) {
        // No upscaling by default: it only adds bytes, never detail. Returning
        // the original size lets the caller skip the file explicitly.
        return ImageDimensions{width, height};
    }

    ImageDimensions target;
    target.width = scale_dimension(static_cast<double>(width) * scale);
    target.height = scale_dimension(static_cast<double>(height) * scale);
    if (target.width == 0) target.width = kMinDimension;
    if (target.height == 0) target.height = kMinDimension;
    return target;
}

RasterImage resize_bilinear(const RasterImage& source, uint32_t target_width,
                            uint32_t target_height) {
    RasterImage result;
    result.format = source.format;
    result.has_alpha = source.has_alpha;
    result.animated = false;
    result.width = target_width;
    result.height = target_height;
    if (!source.valid() || target_width == 0 || target_height == 0) return result;

    result.rgba.resize(static_cast<size_t>(target_width) * target_height * 4u);
    const double scale_x = static_cast<double>(source.width) / static_cast<double>(target_width);
    const double scale_y = static_cast<double>(source.height) / static_cast<double>(target_height);

    auto clamp_index = [](double value, uint32_t limit) {
        if (value < 0.0) return size_t{0};
        if (value > static_cast<double>(limit - 1)) return static_cast<size_t>(limit - 1);
        return static_cast<size_t>(value);
    };

    for (uint32_t y = 0; y < target_height; ++y) {
        // Sample at pixel centres; clamped edges avoid the darkened border that
        // naive sampling produces.
        const double source_y = (static_cast<double>(y) + 0.5) * scale_y - 0.5;
        const size_t y0 = clamp_index(std::floor(source_y), source.height);
        const size_t y1 = clamp_index(std::floor(source_y) + 1.0, source.height);
        const double fy = std::min(1.0, std::max(0.0, source_y - std::floor(source_y)));

        for (uint32_t x = 0; x < target_width; ++x) {
            const double source_x = (static_cast<double>(x) + 0.5) * scale_x - 0.5;
            const size_t x0 = clamp_index(std::floor(source_x), source.width);
            const size_t x1 = clamp_index(std::floor(source_x) + 1.0, source.width);
            const double fx = std::min(1.0, std::max(0.0, source_x - std::floor(source_x)));

            const auto* top_left = &source.rgba[(y0 * source.width + x0) * 4u];
            const auto* top_right = &source.rgba[(y0 * source.width + x1) * 4u];
            const auto* bottom_left = &source.rgba[(y1 * source.width + x0) * 4u];
            const auto* bottom_right = &source.rgba[(y1 * source.width + x1) * 4u];

            // Interpolate premultiplied colour and alpha, then un-premultiply.
            // This keeps transparent pixels from tinting their neighbours.
            double channels[4] = {0.0, 0.0, 0.0, 0.0};
            for (int channel = 0; channel < 4; ++channel) {
                const double top = top_left[channel] * (1.0 - fx) + top_right[channel] * fx;
                const double bottom = bottom_left[channel] * (1.0 - fx) + bottom_right[channel] * fx;
                channels[channel] = top * (1.0 - fy) + bottom * fy;
            }
            double alpha = channels[3];
            double premultiplied[3] = {0.0, 0.0, 0.0};
            for (int channel = 0; channel < 3; ++channel) {
                auto premultiplied_sample = [channel, fx, fy](const uint8_t* top_left_px,
                                                              const uint8_t* top_right_px,
                                                              const uint8_t* bottom_left_px,
                                                              const uint8_t* bottom_right_px) {
                    const auto premultiply = [](uint8_t value, uint8_t a) {
                        return static_cast<double>(value) * static_cast<double>(a) / 255.0;
                    };
                    const double top = premultiply(top_left_px[channel], top_left_px[3]) * (1.0 - fx) +
                                       premultiply(top_right_px[channel], top_right_px[3]) * fx;
                    const double bottom =
                        premultiply(bottom_left_px[channel], bottom_left_px[3]) * (1.0 - fx) +
                        premultiply(bottom_right_px[channel], bottom_right_px[3]) * fx;
                    return top * (1.0 - fy) + bottom * fy;
                };
                premultiplied[channel] = premultiplied_sample(top_left, top_right, bottom_left,
                                                              bottom_right);
            }

            uint8_t* out = &result.rgba[(static_cast<size_t>(y) * target_width + x) * 4u];
            for (int channel = 0; channel < 3; ++channel) {
                double value = alpha > 0.0001 ? premultiplied[channel] * 255.0 / alpha : 0.0;
                value = std::min(255.0, std::max(0.0, value));
                out[channel] = static_cast<uint8_t>(value + 0.5);
            }
            alpha = std::min(255.0, std::max(0.0, alpha));
            out[3] = static_cast<uint8_t>(alpha + 0.5);
        }
    }
    return result;
}

Result<std::vector<std::byte>> ImageResizeService::read_all_bytes(
    const std::filesystem::path& path, uintmax_t max_bytes) const {
    auto size = m_fs.file_size(path);
    if (size.has_error()) return size.error();
    if (size.value() > max_bytes) {
        return Error{ErrorCode::Unsupported,
                     "The image is larger than the configured file-size limit",
                     path_to_utf8(path), "read_image"};
    }
    auto opened = m_fs.open_read(path);
    if (opened.has_error()) return opened.error();
    auto bytes = opened.value()->read_all();
    (void)opened.value()->close();
    if (bytes.has_error()) return bytes.error();
    if (bytes.value().size() > max_bytes) {
        return Error{ErrorCode::Unsupported,
                     "The image is larger than the configured file-size limit",
                     path_to_utf8(path), "read_image"};
    }
    return bytes;
}

Result<void> ImageResizeService::write_bytes(const std::filesystem::path& path,
                                             const std::vector<std::byte>& bytes) const {
    auto opened = m_fs.open_write(path);
    if (opened.has_error()) return opened.error();
    IWritableFile& file = *opened.value();

    size_t written = 0;
    while (written < bytes.size()) {
        auto chunk = file.write(bytes.data() + written, bytes.size() - written);
        if (chunk.has_error()) {
            (void)file.close();
            return chunk.error();
        }
        if (chunk.value() == 0) {
            (void)file.close();
            return Error{ErrorCode::IoError, "The write made no progress", path_to_utf8(path),
                         "write_image"};
        }
        written += chunk.value();
    }
    auto flushed = file.flush();
    if (flushed.has_error()) {
        (void)file.close();
        return flushed.error();
    }
    return file.close();
}

std::filesystem::path ImageResizeService::choose_output_path(
    const std::filesystem::path& directory, const std::filesystem::path& source) const {
    return unique_output_path(m_fs, directory, source);
}

Result<ImageDiscoveryResult> ImageResizeService::discover(const ScanOptions& options,
                                                          const ImageLimits& limits) const {
    if (options.roots.empty()) {
        return Error{ErrorCode::InvalidArgument, "No folders were selected", "", "discover_images"};
    }

    const std::vector<ImageFormat> formats = m_codec.supported_formats();
    auto format_supported = [&formats](ImageFormat format) {
        return std::find(formats.begin(), formats.end(), format) != formats.end();
    };

    Scanner scanner(m_fs);
    auto scanned = scanner.scan(options);
    if (scanned.has_error()) return scanned.error();

    ImageDiscoveryResult result;
    result.issues = scanned.value().issues;
    result.complete = scanned.value().complete;
    size_t unsupported = 0;
    size_t unreadable = 0;

    for (const auto& entry : scanned.value().entries) {
        if (!entry.is_file || entry.excluded || !entry.error.empty()) continue;
        const ImageFormat format = image_format_from_path(entry.path);
        if (format == ImageFormat::Unknown) continue; // not an image at all

        ImageCandidate candidate;
        candidate.path = entry.path;
        candidate.file_size = entry.size;
        candidate.format = format;
        candidate.supported = format_supported(format);
        if (!candidate.supported) {
            candidate.status = "Format not supported by the image codec";
            ++unsupported;
            result.candidates.push_back(std::move(candidate));
            continue;
        }

        // Probing decodes in memory to learn the dimensions. It is bounded by the
        // file-size and pixel limits enforced by the codec, and it always runs on
        // a worker thread.
        auto bytes = read_all_bytes(entry.path, limits.max_file_bytes);
        if (bytes.has_error()) {
            candidate.status = bytes.error().message;
            ++unreadable;
            result.candidates.push_back(std::move(candidate));
            continue;
        }
        auto decoded = m_codec.decode(bytes.value(), format, limits);
        if (decoded.has_error()) {
            candidate.status = decoded.error().message;
            ++unreadable;
            result.candidates.push_back(std::move(candidate));
            continue;
        }
        if (decoded.value().animated) {
            // Flattening an animation or multi-page document would lose frames,
            // so it is listed as unsupported instead of being silently reduced
            // to its first frame.
            candidate.width = decoded.value().width;
            candidate.height = decoded.value().height;
            candidate.supported = false;
            candidate.status = "Animated or multi-page; not resized";
            ++unsupported;
            result.candidates.push_back(std::move(candidate));
            continue;
        }
        candidate.width = decoded.value().width;
        candidate.height = decoded.value().height;
        candidate.status = "Ready";
        result.candidates.push_back(std::move(candidate));
    }

    std::sort(result.candidates.begin(), result.candidates.end(),
              [](const ImageCandidate& left, const ImageCandidate& right) {
                  return left.path.generic_string() < right.path.generic_string();
              });

    if (!result.complete) {
        result.warnings.push_back(
            "The folder scan was not complete; some images may not be listed.");
    }
    if (unsupported > 0) {
        result.warnings.push_back(std::to_string(unsupported) +
                                  " image(s) use a format the codec cannot read and will be skipped.");
    }
    if (unreadable > 0) {
        result.warnings.push_back(std::to_string(unreadable) +
                                  " image(s) could not be decoded and will be skipped.");
    }
    return result;
}

Result<ImageResizeOutcome> ImageResizeService::resize(const ImageResizeRequest& request) const {
    ImageResizeOutcome outcome;
    outcome.source = request.source;

    if (request.source.empty()) {
        return Error{ErrorCode::InvalidArgument, "No image was given", "", "resize_image"};
    }
    if (request.mode == ImageOutputMode::CreateCopies && request.output_directory.empty()) {
        return Error{ErrorCode::InvalidArgument, "Choose an output folder for the copies", "",
                     "resize_image"};
    }
    if (request.quality < 1 || request.quality > 100) {
        return Error{ErrorCode::InvalidArgument, "Image quality must be between 1 and 100", "",
                     "resize_image"};
    }

    auto is_file = m_fs.is_regular_file(request.source);
    if (is_file.has_error() || !is_file.value()) {
        return Error{ErrorCode::NotFound, "The image is no longer a regular file",
                     path_to_utf8(request.source), "resize_image"};
    }
    auto is_link = m_fs.is_symlink(request.source);
    if (is_link.has_error() || is_link.value()) {
        return Error{ErrorCode::Unsupported, "Links are never resized", path_to_utf8(request.source),
                     "resize_image"};
    }
    auto bytes = read_all_bytes(request.source, request.limits.max_file_bytes);
    if (bytes.has_error()) return bytes.error();
    outcome.input_bytes = bytes.value().size();
    auto input_digest = m_hasher.hash_data(bytes.value());
    if (input_digest.has_error()) return input_digest.error();
    outcome.input_digest = input_digest.value().hex;
    if (!request.expected_input_digest.empty() &&
        request.expected_input_digest != outcome.input_digest) {
        return Error{ErrorCode::Stale,
                     "The image changed since it was previewed; resize it again to use the new "
                     "content",
                     path_to_utf8(request.source), "resize_image"};
    }

    const ImageFormat format = image_format_from_path(request.source);
    auto decoded = m_codec.decode(bytes.value(), format, request.limits);
    if (decoded.has_error()) return decoded.error();
    const RasterImage& source = decoded.value();
    if (!source.valid()) {
        return Error{ErrorCode::Unsupported, "The decoder returned an invalid image",
                     path_to_utf8(request.source), "resize_image"};
    }
    if (source.animated) {
        return Error{ErrorCode::Unsupported,
                     "Animated or multi-page images are not resized (frames would be lost)",
                     path_to_utf8(request.source), "resize_image"};
    }
    outcome.source_width = source.width;
    outcome.source_height = source.height;

    auto target = compute_target_size(source.width, source.height, request.rule);
    if (target.has_error()) return target.error();
    outcome.output_width = target.value().width;
    outcome.output_height = target.value().height;

    if (target.value().width == source.width && target.value().height == source.height) {
        outcome.ok = true;
        outcome.skipped = true;
        outcome.message =
            "The image already satisfies the size rule, so no resized file was created.";
        outcome.output = request.mode == ImageOutputMode::CreateCopies ? std::filesystem::path()
                                                                      : request.source;
        return outcome;
    }

    const ImageFormat output_format = format;
    if (!m_codec.can_encode(output_format)) {
        return Error{ErrorCode::Unsupported,
                     "The codec cannot write " + to_string(output_format) +
                         " files; this image was skipped rather than saved in a different format",
                     path_to_utf8(request.source), "resize_image"};
    }

    const RasterImage resized =
        resize_bilinear(source, target.value().width, target.value().height);
    if (!resized.valid()) {
        return Error{ErrorCode::Unknown, "Resizing produced an invalid image", "", "resize_image"};
    }

    auto encoded = m_codec.encode(resized, output_format, request.quality);
    if (encoded.has_error()) return encoded.error();
    if (encoded.value().empty()) {
        return Error{ErrorCode::IoError, "The encoder produced no data", "", "resize_image"};
    }

    std::filesystem::path output;
    std::filesystem::path backup;
    if (request.mode == ImageOutputMode::CreateCopies) {
        auto directory = m_fs.is_directory(request.output_directory);
        if (directory.has_error()) return directory.error();
        if (!directory.value()) {
            auto created = m_fs.create_directory(request.output_directory);
            if (created.has_error()) return created.error();
        }
        output = choose_output_path(request.output_directory, request.source);
    } else {
        output = request.source;
        backup = request.source.string() + ".dupli-backup";
        if (m_fs.exists(backup).ok() && m_fs.exists(backup).value()) {
            for (int suffix = 1; suffix < 100000; ++suffix) {
                backup = request.source.string() + ".dupli-backup-" + std::to_string(suffix);
                if (!(m_fs.exists(backup).ok() && m_fs.exists(backup).value())) break;
            }
        }
    }

    const std::filesystem::path temporary = output.string() + ".dupli-tmp";
    auto temporary_exists = m_fs.exists(temporary);
    if (temporary_exists.has_error()) return temporary_exists.error();
    if (temporary_exists.value()) {
        return Error{ErrorCode::Conflict,
                     "A temporary file from an earlier attempt is present; remove it first",
                     path_to_utf8(temporary), "resize_image"};
    }

    auto written = write_bytes(temporary, encoded.value());
    if (written.has_error()) {
        (void)m_fs.remove(temporary);
        return written.error();
    }

    // Verify the encoded file by decoding it again: an encoder that silently
    // produced the wrong dimensions must never be published as a success.
    auto verify_bytes = read_all_bytes(temporary, request.limits.max_file_bytes);
    if (verify_bytes.has_error()) {
        (void)m_fs.remove(temporary);
        return verify_bytes.error();
    }
    auto verified = m_codec.decode(verify_bytes.value(), output_format, request.limits);
    if (verified.has_error() || verified.value().width != target.value().width ||
        verified.value().height != target.value().height) {
        (void)m_fs.remove(temporary);
        return Error{ErrorCode::IoError,
                     "The encoded image did not verify at the expected dimensions",
                     path_to_utf8(temporary), "resize_image"};
    }

    if (request.preserve_metadata) {
        auto metadata = m_fs.copy_metadata(request.source, temporary);
        if (metadata.has_error()) {
            outcome.message +=
                "Metadata could not be copied (" + metadata.error().message + "). ";
        }
    }

    if (request.mode == ImageOutputMode::CreateCopies) {
        auto published = m_fs.rename(temporary, output);
        if (published.has_error()) {
            (void)m_fs.remove(temporary);
            return published.error();
        }
    } else {
        // Move the original aside first: the new content is only published once
        // a revert path exists. If publication fails, the backup is restored.
        auto moved = m_fs.rename(request.source, backup);
        if (moved.has_error()) {
            (void)m_fs.remove(temporary);
            return moved.error();
        }
        auto published = m_fs.rename(temporary, request.source);
        if (published.has_error()) {
            (void)m_fs.rename(backup, request.source);
            (void)m_fs.remove(temporary);
            return Error{ErrorCode::IoError,
                         "Could not replace the original; it was restored from the backup",
                         path_to_utf8(request.source), "resize_image"};
        }
        outcome.backup = backup;
    }

    outcome.ok = true;
    outcome.output = output;
    outcome.output_bytes = encoded.value().size();
    auto output_digest = m_hasher.hash_data(encoded.value());
    if (output_digest.ok()) outcome.output_digest = output_digest.value().hex;
    if (request.mode == ImageOutputMode::ResizeInPlace) {
        outcome.message +=
            "The original is kept as \"" + path_to_utf8(backup) +
            "\" so the change can be undone.";
    } else {
        outcome.message += "Original untouched; resized copy written to the output folder.";
    }
    return outcome;
}

Result<void> ImageResizeService::undo_in_place(const ImageResizeOutcome& outcome) const {
    if (outcome.backup.empty() || outcome.source.empty()) {
        return Error{ErrorCode::InvalidArgument,
                     "This resize has no recorded backup, so it cannot be undone", "",
                     "undo_resize"};
    }
    auto source_exists = m_fs.exists(outcome.source);
    if (source_exists.has_error()) return source_exists.error();
    if (!source_exists.value()) {
        return Error{ErrorCode::NotFound, "The resized file is missing", path_to_utf8(outcome.source),
                     "undo_resize"};
    }
    auto backup_exists = m_fs.exists(outcome.backup);
    if (backup_exists.has_error()) return backup_exists.error();
    if (!backup_exists.value()) {
        return Error{ErrorCode::NotFound, "The backup of the original is missing",
                     path_to_utf8(outcome.backup), "undo_resize"};
    }

    auto current_bytes =
        read_all_bytes(outcome.source, std::numeric_limits<uintmax_t>::max());
    if (current_bytes.has_error()) return current_bytes.error();
    auto current_digest = m_hasher.hash_data(current_bytes.value());
    if (current_digest.has_error()) return current_digest.error();
    if (!outcome.output_digest.empty() && current_digest.value().hex != outcome.output_digest) {
        return Error{ErrorCode::Stale,
                     "The resized file changed since it was produced; undo was refused", 
                     path_to_utf8(outcome.source), "undo_resize"};
    }

    const std::filesystem::path previous = outcome.source.string() + ".dupli-undone";
    auto displaced = m_fs.rename(outcome.source, previous);
    if (displaced.has_error()) return displaced.error();
    auto restored = m_fs.rename(outcome.backup, outcome.source);
    if (restored.has_error()) {
        (void)m_fs.rename(previous, outcome.source);
        return restored.error();
    }
    (void)m_fs.remove(previous);
    return Result<void>();
}

} // namespace dupli
