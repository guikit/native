#include "guikit/native.hpp"

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
        case NativePlatform::Emscripten:
#if defined(GUIKIT_NATIVE_HAS_EMSCRIPTEN) && GUIKIT_NATIVE_HAS_EMSCRIPTEN
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
#if defined(GUIKIT_NATIVE_HAS_EMSCRIPTEN) && GUIKIT_NATIVE_HAS_EMSCRIPTEN
    return "GuiKit Native Backend (Emscripten + WebGL 2.0)";
#elif defined(GUIKIT_NATIVE_HAS_WIN32) && GUIKIT_NATIVE_HAS_WIN32
    return "GuiKit Native Backend (Win32 + WGL + OpenGL 3.3 Core)";
#elif defined(GUIKIT_NATIVE_HAS_X11) && GUIKIT_NATIVE_HAS_X11
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

keel::UniquePtr<IPlatformWindow> CreateWin32PlatformWindow() {
#if defined(GUIKIT_NATIVE_HAS_WIN32) && GUIKIT_NATIVE_HAS_WIN32
    return keel::make_unique<Win32PlatformWindow>();
#else
    return nullptr;
#endif
}

keel::UniquePtr<IPlatformWindow> CreateEmscriptenPlatformWindow() {
#if defined(GUIKIT_NATIVE_HAS_EMSCRIPTEN) && GUIKIT_NATIVE_HAS_EMSCRIPTEN
    return keel::make_unique<EmscriptenPlatformWindow>();
#else
    return nullptr;
#endif
}

keel::UniquePtr<IPlatformWindow> CreateNativePlatformWindow(NativePlatform platform) {
    if (platform == NativePlatform::Emscripten) {
        return CreateEmscriptenPlatformWindow();
    }
    if (platform == NativePlatform::Win32) {
        return CreateWin32PlatformWindow();
    }
    if (platform == NativePlatform::X11) {
        return CreateX11PlatformWindow();
    }
    if (platform == NativePlatform::Auto) {
#if defined(GUIKIT_NATIVE_HAS_EMSCRIPTEN) && GUIKIT_NATIVE_HAS_EMSCRIPTEN
        if (EmscriptenPlatformWindow::IsRuntimeAvailable()) {
            return keel::make_unique<EmscriptenPlatformWindow>();
        }
#endif
#if defined(GUIKIT_NATIVE_HAS_WIN32) && GUIKIT_NATIVE_HAS_WIN32
        if (Win32PlatformWindow::IsRuntimeAvailable()) {
            return keel::make_unique<Win32PlatformWindow>();
        }
#endif
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
    auto win = CreateNativePlatformWindow(platform);
    if (!win) {
        return nullptr;
    }

    const bool useEs3 = (renderer == NativeRenderer::Auto || renderer == NativeRenderer::OpenGLES3);
#if defined(GUIKIT_NATIVE_HAS_WIN32) && GUIKIT_NATIVE_HAS_WIN32
    const GraphicsApi gfxApi = useEs3 ? GraphicsApi::OpenGL3 : GraphicsApi::None;
#else
    const GraphicsApi gfxApi = useEs3 ? GraphicsApi::OpenGLES3 : GraphicsApi::None;
#endif

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
    RegisterDefaultWindowBackendFactory(&DefaultNativeBackendFactory);
}

} // namespace guikit::native
