#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/core.h"
#include "keel/dynlib.h"

namespace guikit::native::linux_detail {

/// Shared dynamic EGL loader & context owner for native Linux platform windows (Wayland & X11).
///
/// Loads `libEGL.so.1` at runtime via `keel::DynLib` with zero link-time EGL dependencies and
/// exposes `EGL_NATIVE_VISUAL_ID` so X11 can match window visuals before creating the window surface.
class LinuxEglContext {
public:
    static constexpr keel::u32 kPlatformX11Khr     = 0x31D5u;
    static constexpr keel::u32 kPlatformWaylandKhr = 0x31D8u;

    LinuxEglContext() noexcept = default;
    ~LinuxEglContext() noexcept;

    LinuxEglContext(const LinuxEglContext&) = delete;
    LinuxEglContext& operator=(const LinuxEglContext&) = delete;

    /// Returns true if `libEGL.so.1` (or `libEGL.so`) can be opened at runtime.
    [[nodiscard]] static bool IsRuntimeAvailable() noexcept;

    /// Initializes the EGL display for `platformEnum` (`kPlatformWaylandKhr` or `kPlatformX11Khr`)
    /// and selects an `EGLConfig` matching `requestedApi` and `alphaBits` (0 for opaque windows).
    [[nodiscard]] bool InitDisplayAndConfig(
        keel::u32 platformEnum,
        void* nativeDisplay,
        GraphicsApi requestedApi,
        keel::i32 alphaBits = 0
    ) noexcept;

    /// Creates the EGL window surface and context, binds them current, and applies `swapInterval`.
    [[nodiscard]] bool CreateSurfaceAndContext(
        keel::usize nativeWindow,
        keel::i32 swapInterval
    ) noexcept;

    /// Releases context, surface, and EGL display cleanly.
    void Destroy() noexcept;

    [[nodiscard]] bool MakeCurrent() noexcept;
    void SwapBuffers() noexcept;
    void SetSwapInterval(keel::i32 interval) noexcept;

    /// Queries the current backbuffer surface dimensions via `eglQuerySurface(EGL_WIDTH/HEIGHT)`.
    [[nodiscard]] bool QuerySurfaceSize(keel::i32& outWidth, keel::i32& outHeight) const noexcept;

    /// Returns the `EGL_NATIVE_VISUAL_ID` of the selected `EGLConfig` (0 if unavailable).
    [[nodiscard]] keel::i32 GetNativeVisualId() const noexcept { return m_nativeVisualId; }

    [[nodiscard]] bool IsReady() const noexcept {
        return m_eglDisplay != nullptr && m_eglSurface != nullptr && m_eglContext != nullptr;
    }

    [[nodiscard]] GlProcAddressFn GetProcAddressFn() const noexcept;

private:
    using EGLDisplay = void*;
    using EGLConfig = void*;
    using EGLContext = void*;
    using EGLSurface = void*;
    using EGLNativeDisplayType = void*;
    using EGLNativeWindowType = keel::usize;
    using EGLBoolean = keel::u32;
    using EGLint = keel::i32;
    using EGLenum = keel::u32;
    using EglProcAddressRaw = void*(*)(const char*);

    using PfnGetDisplay = EGLDisplay(*)(EGLNativeDisplayType);
    using PfnGetPlatformDisplay = EGLDisplay(*)(EGLenum, void*, const keel::isize*);
    using PfnGetPlatformDisplayExt = EGLDisplay(*)(EGLenum, void*, const EGLint*);
    using PfnInitialize = EGLBoolean(*)(EGLDisplay, EGLint*, EGLint*);
    using PfnTerminate = EGLBoolean(*)(EGLDisplay);
    using PfnBindApi = EGLBoolean(*)(EGLenum);
    using PfnChooseConfig = EGLBoolean(*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    using PfnGetConfigAttrib = EGLBoolean(*)(EGLDisplay, EGLConfig, EGLint, EGLint*);
    using PfnCreateWindowSurface = EGLSurface(*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
    using PfnDestroySurface = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnCreateContext = EGLContext(*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using PfnDestroyContext = EGLBoolean(*)(EGLDisplay, EGLContext);
    using PfnMakeCurrent = EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using PfnSwapBuffers = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnSwapInterval = EGLBoolean(*)(EGLDisplay, EGLint);
    using PfnQuerySurface = EGLBoolean(*)(EGLDisplay, EGLSurface, EGLint, EGLint*);
    using PfnGetProcAddress = EglProcAddressRaw(*)(const char*);

    [[nodiscard]] bool LoadLibrary() noexcept;
    [[nodiscard]] bool ChooseConfig(EGLint renderableBit, EGLint alphaBits, EGLConfig& outCfg) noexcept;

    keel::DynLib m_lib{};
    PfnGetDisplay m_getDisplay{nullptr};
    PfnGetPlatformDisplay m_getPlatformDisplay{nullptr};
    PfnGetPlatformDisplayExt m_getPlatformDisplayExt{nullptr};
    PfnInitialize m_initialize{nullptr};
    PfnTerminate m_terminate{nullptr};
    PfnBindApi m_bindApi{nullptr};
    PfnChooseConfig m_chooseConfig{nullptr};
    PfnGetConfigAttrib m_getConfigAttrib{nullptr};
    PfnCreateWindowSurface m_createWindowSurface{nullptr};
    PfnDestroySurface m_destroySurface{nullptr};
    PfnCreateContext m_createContext{nullptr};
    PfnDestroyContext m_destroyContext{nullptr};
    PfnMakeCurrent m_makeCurrent{nullptr};
    PfnSwapBuffers m_swapBuffers{nullptr};
    PfnSwapInterval m_swapInterval{nullptr};
    PfnQuerySurface m_querySurface{nullptr};
    PfnGetProcAddress m_getProcAddress{nullptr};

    EGLDisplay m_eglDisplay{nullptr};
    EGLConfig m_eglConfig{nullptr};
    EGLSurface m_eglSurface{nullptr};
    EGLContext m_eglContext{nullptr};
    keel::i32 m_nativeVisualId{0};
    bool m_boundEs{true};
};

} // namespace guikit::native::linux_detail
