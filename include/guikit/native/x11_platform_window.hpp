#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/smart_ptr.h"

namespace guikit::native {

/// Native Linux X11 + EGL OpenGL ES 3.0 `IPlatformWindow` implementation.
///
/// Dynamically loads `libX11.so.6` and `libEGL.so.1` via `keel::DynLib` (zero static X11/EGL link
/// dependencies) and uses `poll()` + `eventfd` for 0% CPU reactive idle waiting.
class X11PlatformWindow final : public IPlatformWindow {
public:
    X11PlatformWindow() noexcept;
    ~X11PlatformWindow() override;

    X11PlatformWindow(const X11PlatformWindow&) = delete;
    X11PlatformWindow& operator=(const X11PlatformWindow&) = delete;

    /// Returns true if `libX11.so.6` and `libEGL.so.1` can be loaded and an X11 display is reachable.
    [[nodiscard]] static bool IsRuntimeAvailable() noexcept;

    bool Create(const PlatformWindowDesc& desc, Context& ui, WindowHostCallbacks callbacks) override;
    void Destroy() noexcept override;

    bool PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) override;
    void WakeUp() noexcept override;

    bool MakeContextCurrent() noexcept override;
    void SwapBuffers() noexcept override;
    [[nodiscard]] GlProcAddressFn GetGlProcAddressFn() const noexcept override;

    [[nodiscard]] keel::i32 GetFramebufferWidth() const noexcept override;
    [[nodiscard]] keel::i32 GetFramebufferHeight() const noexcept override;
    [[nodiscard]] keel::f32 GetDpiScale() const noexcept override;
    [[nodiscard]] keel::f32 GetDisplayPixelsPerMm() const noexcept override;
    [[nodiscard]] bool IsMinimized() const noexcept override;
    [[nodiscard]] bool IsLiveResizeFrame() const noexcept override;

    [[nodiscard]] bool IsVsyncEnabled() const noexcept override;
    void SetVsyncEnabled(bool enabled) noexcept override;

    void SetCursor(CursorType cursor) noexcept override;
    void OpenUrl(keel::StringRef url) const noexcept override;

    [[nodiscard]] keel::StringRef GetPlatformName() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
