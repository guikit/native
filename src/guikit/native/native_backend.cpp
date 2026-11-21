#include "guikit/native.hpp"

namespace guikit::native {

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
    return "GuiKit Native Backend (IPlatformWindow + OpenGL ES3 Scaffold)";
}

} // namespace guikit::native
