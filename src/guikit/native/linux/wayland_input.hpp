#pragma once

#include "guikit/backend/i_platform_window.hpp"
#include "guikit/core/types.hpp"
#include "keel/core.h"
#include "wayland_protocols.hpp"

namespace guikit {
class Context;
}

namespace guikit::native::wl_detail {

/// Host environment callbacks & live state references supplied by `WaylandPlatformWindow` to `WaylandInput`.
struct WaylandInputHost {
    const WaylandClientApi* wl{nullptr};
    const WaylandExtInterfaces* ext{nullptr};
    wl_display* display{nullptr};
    wl_compositor* compositor{nullptr};
    wl_shm* shm{nullptr};
    wl_surface* mainSurface{nullptr};
    wp_cursor_shape_manager_v1* cursorShapeMgr{nullptr};
    Context* ui{nullptr};
    const keel::f32* dpiScale{nullptr};
    const bool* customTitleBar{nullptr};

    void* userData{nullptr};
    WindowHitRegion (*hitTest)(void* userData, Vec2 logicalPos){nullptr};
    bool (*borderHitTest)(void* userData, wl_surface* surf, Vec2 localPos, WindowHitRegion& outRegion){nullptr};
    void (*startResize)(void* userData, keel::u32 serial, keel::u32 xdgEdge){nullptr};
    void (*setFocused)(void* userData, bool focused){nullptr};
};

/// Encapsulates Wayland `wl_seat` pointer, cursor shape / legacy `wl_cursor`, XKB keyboard, and client-side key repeat.
class WaylandInput {
public:
    WaylandInput() noexcept = default;
    ~WaylandInput() noexcept { Destroy(); }

    WaylandInput(const WaylandInput&) = delete;
    WaylandInput& operator=(const WaylandInput&) = delete;

    [[nodiscard]] static bool IsXkbRuntimeAvailable() noexcept;

    [[nodiscard]] bool Init(const WaylandInputHost& host) noexcept;
    void UpdateHost(const WaylandInputHost& host) noexcept { m_host = host; }
    void BindSeat(wl_seat* seat) noexcept;
    void Destroy() noexcept;

    void SetCursor(CursorType cursor) noexcept;
    void ClearResizeCursorOverride() noexcept;

    /// Fires client-side key repeat if due; returns true if a repeated key event was dispatched.
    [[nodiscard]] bool TickKeyRepeat(keel::u64 nowMs, Context& ui) noexcept;

    /// Returns milliseconds until next key repeat (or `-1` if no key is repeating).
    [[nodiscard]] keel::i32 NextRepeatTimeoutMs(keel::u64 nowMs) const noexcept;

    [[nodiscard]] wl_seat* GetSeat() const noexcept { return m_seat; }
    [[nodiscard]] keel::u32 GetLastPointerSerial() const noexcept { return m_lastPointerSerial; }
    [[nodiscard]] keel::u32 GetLastInputSerial() const noexcept { return m_lastInputSerial; }

    /// Maps a `WindowHitRegion` resize edge to an `xdg_toplevel` resize edge constant.
    [[nodiscard]] static bool MapHitRegionToResizeEdge(
        WindowHitRegion reg,
        keel::u32& outEdge,
        keel::u32& outWpShape,
        CursorType& outFallbackCursor
    ) noexcept;

private:
    void ApplyCurrentCursor() noexcept;
    void UpdateResizeCursorForRegion(WindowHitRegion reg, bool forceApply) noexcept;
    void UpdateMainSurfaceResizeCursor(bool forceApply) noexcept;
    void DispatchXkbKey(keel::u32 xkbKeycode, Context& ui) noexcept;
    void ReleasePointer() noexcept;
    void ReleaseKeyboard() noexcept;

    static void OnSeatCapabilities(void* data, wl_seat* seat, keel::u32 caps);
    static void OnSeatName(void*, wl_seat*, const char*);

    static void OnPointerEnter(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf, WlFixed sx, WlFixed sy);
    static void OnPointerLeave(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf);
    static void OnPointerMotion(void* data, wl_pointer*, keel::u32, WlFixed sx, WlFixed sy);
    static void OnPointerButton(void* data, wl_pointer*, keel::u32 serial, keel::u32, keel::u32 button, keel::u32 state);
    static void OnPointerAxis(void* data, wl_pointer*, keel::u32, keel::u32 axis, WlFixed value);
    static void OnPointerFrame(void* data, wl_pointer*);
    static void OnPointerAxisSource(void*, wl_pointer*, keel::u32);
    static void OnPointerAxisStop(void*, wl_pointer*, keel::u32, keel::u32);
    static void OnPointerAxisDiscrete(void* data, wl_pointer*, keel::u32 axis, keel::i32 discrete);
    static void OnPointerAxisValue120(void* data, wl_pointer*, keel::u32 axis, keel::i32 value120);
    static void OnPointerAxisRelativeDirection(void*, wl_pointer*, keel::u32, keel::u32);

    static void OnKeyboardKeymap(void* data, wl_keyboard*, keel::u32 format, keel::i32 fd, keel::u32 size);
    static void OnKeyboardEnter(void* data, wl_keyboard*, keel::u32 serial, wl_surface*, wl_array*);
    static void OnKeyboardLeave(void* data, wl_keyboard*, keel::u32 serial, wl_surface*);
    static void OnKeyboardKey(void* data, wl_keyboard*, keel::u32 serial, keel::u32, keel::u32 key, keel::u32 state);
    static void OnKeyboardModifiers(void* data, wl_keyboard*, keel::u32 serial, keel::u32 depressed, keel::u32 latched, keel::u32 locked, keel::u32 group);
    static void OnKeyboardRepeatInfo(void* data, wl_keyboard*, keel::i32 rate, keel::i32 delay);

    static const wl_seat_listener kSeatListener;
    static const wl_pointer_listener kPointerListener;
    static const wl_keyboard_listener kKeyboardListener;

    WaylandInputHost m_host{};
    WaylandCursorApi m_wlCursor{};
    XkbApi m_xkb{};

    wl_seat* m_seat{nullptr};
    wl_pointer* m_pointer{nullptr};
    wl_keyboard* m_keyboard{nullptr};
    wp_cursor_shape_device_v1* m_cursorShapeDev{nullptr};
    wl_surface* m_cursorSurface{nullptr};
    wl_cursor_theme* m_cursorTheme{nullptr};
    keel::i32 m_cursorThemeScale{0};

    xkb_context* m_xkbCtx{nullptr};
    xkb_keymap* m_xkbKeymap{nullptr};
    xkb_state* m_xkbState{nullptr};
    KeyModifiers m_keyMods{};
    keel::i32 m_repeatRate{25};
    keel::i32 m_repeatDelayMs{400};
    keel::u32 m_repeatingKeycode{0u};
    keel::u64 m_nextRepeatMs{0u};

    bool m_pointerOnSurface{false};
    wl_surface* m_pointerBorderSurface{nullptr};
    Vec2 m_pointerLogicalPos{0.0f, 0.0f};
    keel::u32 m_lastPointerEnterSerial{0u};
    keel::u32 m_lastPointerSerial{0u};
    keel::u32 m_lastInputSerial{0u};
    CursorType m_activeCursor{CursorType::Default};
    bool m_resizeCursorOverride{false};
    keel::u32 m_resizeOverrideShape{kWpCursorShapeDefault};
    CursorType m_resizeOverrideFallback{CursorType::Default};

    keel::f32 m_pendingWheelX{0.0f};
    keel::f32 m_pendingWheelY{0.0f};
    bool m_hasDiscreteWheelX{false};
    bool m_hasDiscreteWheelY{false};
};

} // namespace guikit::native::wl_detail
