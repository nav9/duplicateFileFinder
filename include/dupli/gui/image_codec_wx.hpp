#pragma once

#include "dupli/image_resize.hpp"

#include <map>
#include <string>
#include <vector>

namespace dupli {
namespace gui {

/// wxWidgets-backed encoder/decoder for the image resize workflow.
///
/// Why this lives in the GUI target: decoding and encoding need a real image
/// library, and the core is deliberately dependency-free. The core owns every
/// decision that matters for safety (sizing rules, limits, verification, backup,
/// no-overwrite publication); this class only converts bytes to pixels and back.
///
/// Known limitations, surfaced by the Resize Images tab rather than hidden:
///  * Animated/multi-frame formats (GIF, multi-page TIFF) are decoded as a single
///    frame. They are marked `animated` so the core skips them instead of quietly
///    flattening an animation.
///  * EXIF orientation is not applied and is not copied by the core's metadata
///    step (which preserves permissions and timestamps only). Resizing a camera
///    JPEG can therefore change how it is displayed.
///  * Only formats whose wxWidgets handlers are compiled in are listed as
///    supported; anything else is shown as unsupported.
class WxImageCodec : public dupli::IImageCodec {
public:
    WxImageCodec();

    std::vector<ImageFormat> supported_formats() const override;
    bool can_encode(ImageFormat format) const override;
    Result<RasterImage> decode(const std::vector<std::byte>& bytes, ImageFormat hint,
                               const ImageLimits& limits) override;
    Result<std::vector<std::byte>> encode(const RasterImage& image, ImageFormat format,
                                          int quality) override;
    std::string name() const override { return "wxWidgets image handlers"; }

private:
    std::vector<ImageFormat> m_supported;
    std::map<ImageFormat, long> m_wxTypes;
};

} // namespace gui
} // namespace dupli
