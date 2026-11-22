#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "keel/smart_ptr.h"

namespace guikit::native {

/// Native WebAssembly Emscripten + HTML5 Canvas + WebGL 2.0 `IPlatformWindow` implementation.
///
/// Zero SDL3 and zero ThorVG dependencies: binds directly to `#canvas` via `<emscripten/html5.h>`
/// and `<emscripten/html5_webgl.h>`, creates an MSAA + Stencil WebGL 2.0 context for `GlEs3Canvas`,
/// handles HiDPI resize, mouse/touch/wheel/keyboard/UTF-8 input, browser clipboard, CSS cursors,
/// and reactive `requestAnimationFrame` idle skipping.
class EmscriptenPlatformWindow final : public IPlatformWindow {
public:
    EmscriptenPlatformWindow() noexcept;
    ~EmscriptenPlatformWindow() override;

    EmscriptenPlatformWindow(const EmscriptenPlatformWindow&) = delete;
    EmscriptenPlatformWindow& operator=(const EmscriptenPlatformWindow&) = delete;

    /// Returns true on Emscripten WebAssembly builds.
    [[nodiscard]] static bool IsRuntimeAvailable() noexcept;

    bool Create(const PlatformWindowDesc& desc, Context& ui, WindowHostCallbacks callbacks) override;
    void Destroy() noexcept override;

    bool PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) override;
    void WakeUp() noexcept override;

    [[nodiscard]] bool UsesMainLoopCallback() const noexcept override;
    void RunMainLoopCallback(keel::Function<bool()> stepFn) override;

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
    [[nodiscard]] keel::Optional<ThemeMode> QuerySystemTheme() const noexcept override;

    [[nodiscard]] keel::StringRef GetPlatformName() const noexcept override;

private:
    struct Impl;
    keel::UniquePtr<Impl> m_impl;
};

} // namespace guikit::native
