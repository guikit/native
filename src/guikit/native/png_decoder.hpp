#pragma once

#include "keel/array.h"
#include "keel/core.h"

namespace guikit::native::detail {

/// Fast header-only PNG dimension probe without decompressing IDAT payload.
[[nodiscard]] bool InspectPngDimensions(
    keel::ArrayRef<keel::u8> encodedBytes,
    keel::u32& outWidth,
    keel::u32& outHeight
) noexcept;

/// Self-contained 0BSD PNG decoder (RFC 1950 + RFC 1951 + RFC 2083).
/// Decodes PNG bytes into 32-bit RGBA8888 pixels (`(A << 24) | (B << 16) | (G << 8) | R`).
/// Thread-safe and zero external library dependencies.
[[nodiscard]] bool DecodePngFromMemory(
    keel::ArrayRef<keel::u8> encodedBytes,
    keel::u32& outWidth,
    keel::u32& outHeight,
    keel::Array<keel::u32>& outRgbaPixels
);

} // namespace guikit::native::detail
