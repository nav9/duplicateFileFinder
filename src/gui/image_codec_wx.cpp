#include "dupli/gui/image_codec_wx.hpp"

#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>

#include <cstring>

namespace dupli {
namespace gui {

namespace {

struct HandlerEntry {
    ImageFormat format;
    long wx_type;
};

/// True when wxWidgets registered a handler for this format.
///
/// wxImage::FindHandler() is the only portable query, and wxWidgets marks it
/// deprecated without offering a replacement. The deprecation warning is
/// suppressed here (and only here) so it cannot hide a real warning elsewhere.
/// MIME type used by the non-deprecated SaveFile overload.
const char* mime_type(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png: return "image/png";
    case ImageFormat::Jpeg: return "image/jpeg";
    case ImageFormat::Bmp: return "image/bmp";
    case ImageFormat::Tiff: return "image/tiff";
    case ImageFormat::Gif: return "image/gif";
    case ImageFormat::Webp: return "image/webp";
    case ImageFormat::Unknown: break;
    }
    return "";
}

bool handler_registered(long wx_type) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    const bool found = wxImage::FindHandler(wx_type) != nullptr;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    return found;
}

/// Formats worth probing. A format is only reported when wxWidgets actually
/// registered a handler for it, so the UI never offers a format the build cannot
/// read or write.
const HandlerEntry kHandlers[] = {
    {ImageFormat::Png, wxBITMAP_TYPE_PNG},
    {ImageFormat::Jpeg, wxBITMAP_TYPE_JPEG},
    {ImageFormat::Bmp, wxBITMAP_TYPE_BMP},
    {ImageFormat::Gif, wxBITMAP_TYPE_GIF},
    {ImageFormat::Tiff, wxBITMAP_TYPE_TIFF},
    // WebP is intentionally absent: the supported wxWidgets baseline has no
    // WebP handler, so offering it would promise a format that cannot be read.
};

} // namespace

WxImageCodec::WxImageCodec() {
    for (const auto& entry : kHandlers) {
        // wxIMAGE_OPTION_* handlers are registered by wxInitAllImageHandlers()
        // in main(). FindHandler() reports whether a format is usable.
        if (!handler_registered(entry.wx_type)) continue;
        m_supported.push_back(entry.format);
        m_wxTypes[entry.format] = entry.wx_type;
    }
    // PNG is required for the thumbnail/preview path; without it every encode
    // would fail, so it is always offered as a fallback output format when the
    // handler exists.
    if (m_wxTypes.find(ImageFormat::Png) == m_wxTypes.end() &&
        handler_registered(wxBITMAP_TYPE_PNG)) {
        m_supported.push_back(ImageFormat::Png);
        m_wxTypes[ImageFormat::Png] = wxBITMAP_TYPE_PNG;
    }

    // wxWidgets installs the BMP handler as a built-in, so FindHandler() does not
    // list it even though wxImage can always read and write BMP.
    if (m_wxTypes.find(ImageFormat::Bmp) == m_wxTypes.end()) {
        m_supported.push_back(ImageFormat::Bmp);
        m_wxTypes[ImageFormat::Bmp] = wxBITMAP_TYPE_BMP;
    }
}

std::vector<ImageFormat> WxImageCodec::supported_formats() const { return m_supported; }

bool WxImageCodec::can_encode(ImageFormat format) const {
    // GIF encoding is lossy/quantising and would silently degrade an animation;
    // the core only offers this for single images, so it is not exposed.
    if (format == ImageFormat::Gif) return false;
    // BMP is a built-in wxWidgets handler and is not reported by FindHandler().
    if (format == ImageFormat::Bmp) return true;
    auto found = m_wxTypes.find(format);
    if (found == m_wxTypes.end()) return false;
    return handler_registered(found->second);
}

Result<RasterImage> WxImageCodec::decode(const std::vector<std::byte>& bytes, ImageFormat hint,
                                        const ImageLimits& limits) {
    if (bytes.empty()) {
        return Error{ErrorCode::Unsupported, "The image file is empty", "", "wx_decode"};
    }
    if (bytes.size() > limits.max_file_bytes) {
        return Error{ErrorCode::Unsupported, "The image exceeds the file-size limit", "",
                     "wx_decode"};
    }

    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxImage image;
    // wxImage::LoadFile never throws; it returns false for unreadable or
    // unsupported data. A failure is reported as a typed error, never as an
    // empty image that could be resized into a blank file.
    if (!image.LoadFile(stream, wxBITMAP_TYPE_ANY)) {
        return Error{ErrorCode::Unsupported, "The image could not be decoded (corrupt or "
                                             "unsupported format)",
                     "", "wx_decode"};
    }
    if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0) {
        return Error{ErrorCode::Unsupported, "The decoder produced no pixels", "", "wx_decode"};
    }

    const uint32_t width = static_cast<uint32_t>(image.GetWidth());
    const uint32_t height = static_cast<uint32_t>(image.GetHeight());
    // Check dimensions and pixel budget after decode but before allocating our
    // own RGBA buffer; wxImage has already decoded here, so these limits are also
    // enforced by the caller's file-size cap.
    if (width > limits.max_dimension || height > limits.max_dimension) {
        return Error{ErrorCode::Unsupported, "The image dimensions exceed the configured limit",
                     "", "wx_decode"};
    }
    if (static_cast<uint64_t>(width) * height > limits.max_pixels) {
        return Error{ErrorCode::Unsupported, "The image pixel count exceeds the configured limit",
                     "", "wx_decode"};
    }

    RasterImage result;
    result.width = width;
    result.height = height;
    result.format = hint;
    result.has_alpha = image.HasAlpha();
    // GIF and TIFF can hold multiple frames/pages. wxImage returns only one, so
    // the image is flagged (not silently flattened) and the core skips it.
    result.animated = hint == ImageFormat::Gif || hint == ImageFormat::Tiff;
    result.rgba.resize(static_cast<size_t>(width) * height * 4u);

    const unsigned char* rgb = image.GetData();
    const unsigned char* alpha = image.GetAlpha();
    for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
        result.rgba[pixel * 4 + 0] = rgb[pixel * 3 + 0];
        result.rgba[pixel * 4 + 1] = rgb[pixel * 3 + 1];
        result.rgba[pixel * 4 + 2] = rgb[pixel * 3 + 2];
        result.rgba[pixel * 4 + 3] = alpha ? alpha[pixel] : 255;
    }
    return result;
}

Result<std::vector<std::byte>> WxImageCodec::encode(const RasterImage& image, ImageFormat format,
                                                    int quality) {
    if (!image.valid()) {
        return Error{ErrorCode::InvalidArgument, "The image to encode is invalid", "", "wx_encode"};
    }
    auto found = m_wxTypes.find(format);
    if (found == m_wxTypes.end() || !can_encode(format)) {
        return Error{ErrorCode::Unsupported, "No encoder is available for this format", "",
                     "wx_encode"};
    }

    wxImage output(static_cast<int>(image.width), static_cast<int>(image.height), false);
    if (!output.IsOk()) {
        return Error{ErrorCode::IoError, "Could not allocate the output image", "", "wx_encode"};
    }
    unsigned char* rgb = output.GetData();
    const bool has_alpha = image.has_alpha;
    for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
        rgb[pixel * 3 + 0] = image.rgba[pixel * 4 + 0];
        rgb[pixel * 3 + 1] = image.rgba[pixel * 4 + 1];
        rgb[pixel * 3 + 2] = image.rgba[pixel * 4 + 2];
    }
    if (has_alpha) {
        output.InitAlpha();
        unsigned char* alpha = output.GetAlpha();
        for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
            alpha[pixel] = image.rgba[pixel * 4 + 3];
        }
    }
    output.SetOption(wxIMAGE_OPTION_QUALITY, quality);

    wxMemoryOutputStream stream;
    if (!output.SaveFile(stream, wxString::FromAscii(mime_type(format)))) {
        return Error{ErrorCode::IoError, "The image could not be encoded", "", "wx_encode"};
    }
    const size_t size = stream.GetSize();
    std::vector<std::byte> bytes(size);
    if (size > 0) {
        stream.CopyTo(bytes.data(), size);
    }
    if (bytes.empty()) {
        return Error{ErrorCode::IoError, "The encoder produced no data", "", "wx_encode"};
    }
    return bytes;
}

} // namespace gui
} // namespace dupli
