#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/smart_ptr.h"

namespace guikit::native {

/// Native Linux Wayland + EGL OpenGL ES 3.0 `IPlatformWindow` implementation.
///
/// Dynamically loads `libwayland-client.so.0`, `libwayland-egl.so.1`, `libEGL.so.1`,
/// `libxkbcommon.so.0`, and optionally `libwayland-cursor.so.0` via `keel::DynLib`
/// (zero build-time or link-time Wayland/EGL/XKB dependencies).
///
/// Implements atomic `xdg-shell` configure/resize commits, `wp-fractional-scale-v1` +
/// `wp-viewporter` crisp HiDPI scaling, `wp-cursor-shape-v1` cursors, `zxdg-decoration-v1`
/// SSD/CSD negotiation, `wl_surface.frame` VSync pacing, XKB UTF-8 keyboard + repeat,
/// and `wl_data_device` clipboard.
class WaylandPlatformWindow final : public IPlatformWindow {
public:
    WaylandPlatformWindow() noexcept;
    ~WaylandPlatformWindow() override;

    WaylandPlatformWindow(const WaylandPlatformWindow&) = delete;
    WaylandPlatformWindow& operator=(const WaylandPlatformWindow&) = delete;

    /// Returns true if `WAYLAND_DISPLAY` is set and Wayland/EGL/XKB shared libraries are available.
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
    [[nodiscard]] bool ShouldRenderFrame() const noexcept override;
    [[nodiscard]] bool IsLiveResizeFrame() const noexcept override;

    [[nodiscard]] bool IsVsyncEnabled() const noexcept override;
    void SetVsyncEnabled(bool enabled) noexcept override;

    void SetCursor(CursorType cursor) noexcept override;
    void OpenUrl(keel::StringRef url) const noexcept override;

    void SetTitle(keel::StringRef title) noexcept override;
    void Minimize() noexcept override;
    void Maximize() noexcept override;
    void Restore() noexcept override;
    void RequestClose() noexcept override;
    [[nodiscard]] bool IsMaximized() const noexcept override;
    [[nodiscard]] bool IsFocused() const noexcept override;
    [[nodiscard]] bool SupportsCustomTitleBar() const noexcept override;
    [[nodiscard]] bool IsCustomTitleBar() const noexcept override;
    bool SetCustomTitleBar(bool enabled) noexcept override;
    void StartSystemMove() noexcept override;
    void StartSystemResize(WindowHitRegion edge) noexcept override;

    [[nodiscard]] bool SupportsFileDialogs() const noexcept override;
    FileDialogResult ShowFileDialog(const FileDialogOptions& opts) override;
    MessageBoxChoice ShowMessageBox(const MessageBoxOptions& opts) override;

    [[nodiscard]] keel::StringRef GetPlatformName() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
