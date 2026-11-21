#pragma once

// ==============================================================================
// GuiKit Native Backend Module (X11 / Wayland / Win32 + OpenGL ES3 / GL3)
// Zero SDL3, Zero ThorVG — Built on guikit::core + optional guikit::text
// ==============================================================================

#include "guikit/core.hpp"
#include "guikit/native/gl_es3_canvas.hpp"
#include "guikit/native/x11_platform_window.hpp"

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

/// Creates an X11 + EGL `IPlatformWindow` instance (returns `nullptr` on non-X11 builds).
[[nodiscard]] keel::UniquePtr<IPlatformWindow> CreateX11PlatformWindow();

/// Creates a native `IPlatformWindow` matching `platform` (`Auto` probes runtime availability).
[[nodiscard]] keel::UniquePtr<IPlatformWindow> CreateNativePlatformWindow(
    NativePlatform platform = NativePlatform::Auto
);

/// Creates a `GlEs3Canvas` bound to the active GL/ES3 context resolved via `procLoader`.
[[nodiscard]] keel::UniquePtr<ICanvas> CreateGlEs3Canvas(
    GlProcAddressFn procLoader,
    bool loadDefaultFonts = true
);

/// Creates a ready-to-run `IWindowBackend` (`PlatformWindowBackend`) pairing a native
/// `IPlatformWindow` with `GlEs3Canvas` (or `NullCanvas`).
[[nodiscard]] keel::UniquePtr<IWindowBackend> CreateNativeWindowBackend(
    NativePlatform platform = NativePlatform::Auto,
    NativeRenderer renderer = NativeRenderer::Auto
);

/// Registers `CreateNativeWindowBackend()` as the default `guikit::MainWindow` backend factory.
void RegisterNativeWindowBackend() noexcept;

} // namespace guikit::native
