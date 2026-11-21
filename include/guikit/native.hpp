#pragma once

// ==============================================================================
// GuiKit Native Backend Module (X11 / Wayland / Win32 + OpenGL ES3 / GL3)
// Zero SDL3, Zero ThorVG — Built on guikit::core + optional guikit::text
// ==============================================================================

#include "guikit/core.hpp"

namespace guikit::native {

/// Native windowing platform backends supported by `guikit::native`.
enum class NativePlatform : keel::u8 {
    Auto = 0,
    Headless = 1,
    X11 = 2,
    Wayland = 3,
    Win32 = 4
};

/// Native GPU/2D rendering backends supported by `guikit::native`.
enum class NativeRenderer : keel::u8 {
    Auto = 0,
    Null = 1,
    OpenGLES3 = 2
};

/// Returns true if the requested platform backend was compiled into `guikit::native`.
[[nodiscard]] bool IsPlatformCompiled(NativePlatform platform) noexcept;

/// Returns true if the requested renderer backend was compiled into `guikit::native`.
[[nodiscard]] bool IsRendererCompiled(NativeRenderer renderer) noexcept;

/// Returns a human-readable summary of the active native backend configuration.
[[nodiscard]] keel::StringRef GetBackendSummary() noexcept;

} // namespace guikit::native
