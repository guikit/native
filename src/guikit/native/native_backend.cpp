#include "guikit/native.hpp"

#include "guikit/backend/vector_backend.hpp"
#include "png_decoder.hpp"

namespace guikit::native {

namespace {

[[nodiscard]] keel::UniquePtr<IWindowBackend> DefaultNativeBackendFactory() {
    return CreateNativeWindowBackend(NativePlatform::Auto, NativeRenderer::Auto);
}

} // namespace

bool IsPlatformCompiled(NativePlatform platform) noexcept {
    switch (platform) {
        case NativePlatform::Auto:
        case NativePlatform::Headless:
            return true;
        case NativePlatform::X11:
#if defined(GUIKIT_NATIVE_HAS_X11) && GUIKIT_NATIVE_HAS_X11
            return true;
#else
            return false;
#endif
        case NativePlatform::Wayland:
#if defined(GUIKIT_NATIVE_HAS_WAYLAND) && GUIKIT_NATIVE_HAS_WAYLAND
            return true;
#else
            return false;
#endif
        case NativePlatform::Win32:
#if defined(GUIKIT_NATIVE_HAS_WIN32) && GUIKIT_NATIVE_HAS_WIN32
            return true;
#else
            return false;
#endif
    }
    return false;
}

bool IsRendererCompiled(NativeRenderer renderer) noexcept {
    switch (renderer) {
        case NativeRenderer::Auto:
        case NativeRenderer::Null:
            return true;
        case NativeRenderer::OpenGLES3:
#if defined(GUIKIT_NATIVE_HAS_ES3) && GUIKIT_NATIVE_HAS_ES3
            return true;
#else
            return false;
#endif
    }
    return false;
}

keel::StringRef GetBackendSummary() noexcept {
#if defined(GUIKIT_NATIVE_HAS_X11) && GUIKIT_NATIVE_HAS_X11
    return "GuiKit Native Backend (X11 + EGL + OpenGL ES 3.0)";
#else
    return "GuiKit Native Backend (OpenGL ES 3.0)";
#endif
}

keel::UniquePtr<IPlatformWindow> CreateX11PlatformWindow() {
#if defined(GUIKIT_NATIVE_HAS_X11) && GUIKIT_NATIVE_HAS_X11
    return keel::make_unique<X11PlatformWindow>();
#else
    return nullptr;
#endif
}

keel::UniquePtr<IPlatformWindow> CreateNativePlatformWindow(NativePlatform platform) {
    if (platform == NativePlatform::X11) {
        return CreateX11PlatformWindow();
    }
    if (platform == NativePlatform::Auto) {
#if defined(GUIKIT_NATIVE_HAS_X11) && GUIKIT_NATIVE_HAS_X11
        if (X11PlatformWindow::IsRuntimeAvailable()) {
            return keel::make_unique<X11PlatformWindow>();
        }
#endif
    }
    return nullptr;
}

keel::UniquePtr<ICanvas> CreateGlEs3Canvas(GlProcAddressFn procLoader, bool loadDefaultFonts) {
    auto canvas = keel::make_unique<GlEs3Canvas>(procLoader, loadDefaultFonts);
    if (!canvas->IsInitialized()) {
        return nullptr;
    }
    return canvas;
}

keel::UniquePtr<IWindowBackend> CreateNativeWindowBackend(
    NativePlatform platform,
    NativeRenderer renderer
) {
    RegisterDefaultImageDecoder(&detail::DecodePngFromMemory);

    auto win = CreateNativePlatformWindow(platform);
    if (!win) {
        return nullptr;
    }

    const bool useEs3 = (renderer == NativeRenderer::Auto || renderer == NativeRenderer::OpenGLES3);
    const GraphicsApi gfxApi = useEs3 ? GraphicsApi::OpenGLES3 : GraphicsApi::None;

    PlatformCanvasFactory factory = [useEs3](IPlatformWindow& w) -> keel::UniquePtr<ICanvas> {
        if (useEs3) {
            if (auto glCanvas = CreateGlEs3Canvas(w.GetGlProcAddressFn(), true)) {
                return glCanvas;
            }
        }
        return keel::make_unique<NullCanvas>();
    };

    return keel::make_unique<PlatformWindowBackend>(keel::move(win), keel::move(factory), gfxApi);
}

void RegisterNativeWindowBackend() noexcept {
    RegisterDefaultImageDecoder(&detail::DecodePngFromMemory);
    RegisterDefaultWindowBackendFactory(&DefaultNativeBackendFactory);
}

} // namespace guikit::native
