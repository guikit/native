#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/smart_ptr.h"

namespace guikit::native {

/// Native Windows Win32 + WGL OpenGL 3.3 Core `IPlatformWindow` implementation.
///
/// Zero external DLL dependencies (uses only stock system `user32`, `gdi32`, `opengl32`, `shell32`),
/// dynamically probes Per-Monitor DPI v2 and DWM dark titlebar APIs, supports smooth live-resize,
/// UTF-16 <-> UTF-8 text & system clipboard, and `MsgWaitForMultipleObjects` for 0% CPU idle sleep.
class Win32PlatformWindow final : public IPlatformWindow {
public:
    Win32PlatformWindow() noexcept;
    ~Win32PlatformWindow() override;

    Win32PlatformWindow(const Win32PlatformWindow&) = delete;
    Win32PlatformWindow& operator=(const Win32PlatformWindow&) = delete;

    /// Returns true on Windows runtimes where Win32 windowing and `opengl32.dll` are available.
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
    void SetTextInputActive(bool active, Rect caretRectPx) noexcept override;
    void OpenUrl(keel::StringRef url) const noexcept override;

    [[nodiscard]] keel::StringRef GetPlatformName() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
