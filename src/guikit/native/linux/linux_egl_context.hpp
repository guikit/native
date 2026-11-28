#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/core.h"

namespace guikit::native::linux_detail {

/// Shared dynamic EGL context owner for native Linux platform windows (Wayland & X11).
///
/// Backed by a process-wide cached `libEGL.so.1` dispatch table loaded via `keel::DynLib`
/// (zero link-time EGL dependencies and safe across multi-window lifetimes).
class LinuxEglContext {
public:
    static constexpr keel::u32 kPlatformX11Khr     = 0x31D5u;
    static constexpr keel::u32 kPlatformWaylandKhr = 0x31D8u;

    LinuxEglContext() noexcept = default;
    ~LinuxEglContext() noexcept;

    LinuxEglContext(const LinuxEglContext&) = delete;
    LinuxEglContext& operator=(const LinuxEglContext&) = delete;

    /// Returns true if `libEGL.so.1` (or `libEGL.so`) can be loaded at runtime.
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

    /// Releases context, surface, and EGL display cleanly without unloading the shared EGL library.
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
    using EGLint = keel::i32;

    [[nodiscard]] bool ChooseConfig(EGLint renderableBit, EGLint alphaBits, EGLConfig& outCfg) noexcept;

    EGLDisplay m_eglDisplay{nullptr};
    EGLConfig m_eglConfig{nullptr};
    EGLSurface m_eglSurface{nullptr};
    EGLContext m_eglContext{nullptr};
    keel::i32 m_nativeVisualId{0};
    bool m_boundEs{true};
};

} // namespace guikit::native::linux_detail
