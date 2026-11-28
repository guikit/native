#include "wayland_input.hpp"

#include "guikit/context.hpp"
#include "keel/math.h"
#include "keel/time.h"
#include "linux_keymap.hpp"

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace guikit::native::wl_detail {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

static constexpr keel::u32 kBtnLeft   = 0x110u; // BTN_LEFT
static constexpr keel::u32 kBtnRight  = 0x111u; // BTN_RIGHT
static constexpr keel::u32 kBtnMiddle = 0x112u; // BTN_MIDDLE

[[nodiscard]] keel::u32 MapCursorToWpShape(CursorType c) noexcept {
    switch (c) {
        case CursorType::Pointer:    return kWpCursorShapePointer;
        case CursorType::Text:       return kWpCursorShapeText;
        case CursorType::ResizeEW:   return kWpCursorShapeEwResize;
        case CursorType::ResizeNS:   return kWpCursorShapeNsResize;
        case CursorType::ResizeNWSE: return kWpCursorShapeNwseResize;
        case CursorType::ResizeNESW: return kWpCursorShapeNeswResize;
        case CursorType::Move:       return kWpCursorShapeMove;
        case CursorType::NotAllowed: return kWpCursorShapeNotAllowed;
        case CursorType::Default:
        default:                     return kWpCursorShapeDefault;
    }
}

[[nodiscard]] const char* MapCursorToLegacyName(CursorType c) noexcept {
    switch (c) {
        case CursorType::Pointer:    return "pointer";
        case CursorType::Text:       return "xterm";
        case CursorType::ResizeEW:   return "ew-resize";
        case CursorType::ResizeNS:   return "ns-resize";
        case CursorType::ResizeNWSE: return "nwse-resize";
        case CursorType::ResizeNESW: return "nesw-resize";
        case CursorType::Move:       return "grabbing";
        case CursorType::NotAllowed: return "not-allowed";
        case CursorType::Default:
        default:                     return "left_ptr";
    }
}

} // namespace

const wl_seat_listener WaylandInput::kSeatListener = {
    &WaylandInput::OnSeatCapabilities,
    &WaylandInput::OnSeatName
};

const wl_pointer_listener WaylandInput::kPointerListener = {
    &WaylandInput::OnPointerEnter,
    &WaylandInput::OnPointerLeave,
    &WaylandInput::OnPointerMotion,
    &WaylandInput::OnPointerButton,
    &WaylandInput::OnPointerAxis,
    &WaylandInput::OnPointerFrame,
    &WaylandInput::OnPointerAxisSource,
    &WaylandInput::OnPointerAxisStop,
    &WaylandInput::OnPointerAxisDiscrete,
    &WaylandInput::OnPointerAxisValue120,
    &WaylandInput::OnPointerAxisRelativeDirection
};

const wl_keyboard_listener WaylandInput::kKeyboardListener = {
    &WaylandInput::OnKeyboardKeymap,
    &WaylandInput::OnKeyboardEnter,
    &WaylandInput::OnKeyboardLeave,
    &WaylandInput::OnKeyboardKey,
    &WaylandInput::OnKeyboardModifiers,
    &WaylandInput::OnKeyboardRepeatInfo
};

bool WaylandInput::IsXkbRuntimeAvailable() noexcept {
    XkbApi probe{};
    return probe.Load();
}

bool WaylandInput::MapHitRegionToResizeEdge(
    WindowHitRegion reg,
    keel::u32& outEdge,
    keel::u32& outWpShape,
    CursorType& outFallbackCursor
) noexcept {
    switch (reg) {
        case WindowHitRegion::ResizeTopLeft:
            outEdge = kXdgResizeEdgeTopLeft;
            outWpShape = kWpCursorShapeNwResize;
            outFallbackCursor = CursorType::ResizeNWSE;
            return true;
        case WindowHitRegion::ResizeTop:
            outEdge = kXdgResizeEdgeTop;
            outWpShape = kWpCursorShapeNResize;
            outFallbackCursor = CursorType::ResizeNS;
            return true;
        case WindowHitRegion::ResizeTopRight:
            outEdge = kXdgResizeEdgeTopRight;
            outWpShape = kWpCursorShapeNeResize;
            outFallbackCursor = CursorType::ResizeNESW;
            return true;
        case WindowHitRegion::ResizeRight:
            outEdge = kXdgResizeEdgeRight;
            outWpShape = kWpCursorShapeEResize;
            outFallbackCursor = CursorType::ResizeEW;
            return true;
        case WindowHitRegion::ResizeBottomRight:
            outEdge = kXdgResizeEdgeBottomRight;
            outWpShape = kWpCursorShapeSeResize;
            outFallbackCursor = CursorType::ResizeNWSE;
            return true;
        case WindowHitRegion::ResizeBottom:
            outEdge = kXdgResizeEdgeBottom;
            outWpShape = kWpCursorShapeSResize;
            outFallbackCursor = CursorType::ResizeNS;
            return true;
        case WindowHitRegion::ResizeBottomLeft:
            outEdge = kXdgResizeEdgeBottomLeft;
            outWpShape = kWpCursorShapeSwResize;
            outFallbackCursor = CursorType::ResizeNESW;
            return true;
        case WindowHitRegion::ResizeLeft:
            outEdge = kXdgResizeEdgeLeft;
            outWpShape = kWpCursorShapeWResize;
            outFallbackCursor = CursorType::ResizeEW;
            return true;
        default:
            return false;
    }
}

bool WaylandInput::Init(const WaylandInputHost& host) noexcept {
    Destroy();
    m_host = host;
    if (!m_xkb.Load()) {
        return false;
    }
    m_xkbCtx = m_xkb.ContextNew(XkbApi::kContextNoFlags);
    return m_xkbCtx != nullptr;
}

void WaylandInput::BindSeat(wl_seat* seat) noexcept {
    if (!seat || !m_host.wl || m_seat == seat) return;
    m_seat = seat;
    m_host.wl->AddListener(m_seat, &kSeatListener, this);
}

void WaylandInput::ReleasePointer() noexcept {
    if (m_host.wl) {
        if (m_cursorShapeDev) {
            m_host.wl->MarshalDestroy(m_cursorShapeDev, kWpCursorShapeDevDestroy);
            m_cursorShapeDev = nullptr;
        }
        if (m_pointer) {
            if (m_host.wl->ProxyGetVersion(reinterpret_cast<wl_proxy*>(m_pointer)) >= 3u) {
                m_host.wl->MarshalDestroy(m_pointer, kWlPointerRelease);
            } else {
                m_host.wl->ProxyDestroy(reinterpret_cast<wl_proxy*>(m_pointer));
            }
            m_pointer = nullptr;
        }
    }
    m_pointer = nullptr;
    m_cursorShapeDev = nullptr;
    m_pointerOnSurface = false;
    m_pointerBorderSurface = nullptr;
    m_resizeCursorOverride = false;
}

void WaylandInput::ReleaseKeyboard() noexcept {
    if (m_host.wl && m_keyboard) {
        if (m_host.wl->ProxyGetVersion(reinterpret_cast<wl_proxy*>(m_keyboard)) >= 3u) {
            m_host.wl->MarshalDestroy(m_keyboard, kWlKeyboardRelease);
        } else {
            m_host.wl->ProxyDestroy(reinterpret_cast<wl_proxy*>(m_keyboard));
        }
    }
    m_keyboard = nullptr;
    m_repeatingKeycode = 0u;
    m_keyMods = {};
}

void WaylandInput::Destroy() noexcept {
    if (m_cursorTheme && m_wlCursor.ThemeDestroy) {
        m_wlCursor.ThemeDestroy(m_cursorTheme);
        m_cursorTheme = nullptr;
    }
    if (m_cursorSurface && m_host.wl) {
        m_host.wl->MarshalDestroy(m_cursorSurface, kWlSurfaceDestroy);
        m_cursorSurface = nullptr;
    }
    ReleaseKeyboard();
    ReleasePointer();
    if (m_seat && m_host.wl) {
        if (m_host.wl->ProxyGetVersion(reinterpret_cast<wl_proxy*>(m_seat)) >= 5u) {
            m_host.wl->MarshalDestroy(m_seat, kWlSeatRelease);
        } else {
            m_host.wl->ProxyDestroy(reinterpret_cast<wl_proxy*>(m_seat));
        }
    }
    m_seat = nullptr;

    if (m_xkbState && m_xkb.StateUnref) {
        m_xkb.StateUnref(m_xkbState);
        m_xkbState = nullptr;
    }
    if (m_xkbKeymap && m_xkb.KeymapUnref) {
        m_xkb.KeymapUnref(m_xkbKeymap);
        m_xkbKeymap = nullptr;
    }
    if (m_xkbCtx && m_xkb.ContextUnref) {
        m_xkb.ContextUnref(m_xkbCtx);
        m_xkbCtx = nullptr;
    }

    m_cursorThemeScale = 0;
    m_pendingWheelX = 0.0f;
    m_pendingWheelY = 0.0f;
    m_hasDiscreteWheelX = false;
    m_hasDiscreteWheelY = false;
    m_host = {};
}

void WaylandInput::SetCursor(CursorType cursor) noexcept {
    if (cursor == m_activeCursor) return;
    m_activeCursor = cursor;
    if (!m_resizeCursorOverride) {
        ApplyCurrentCursor();
    }
}

void WaylandInput::ClearResizeCursorOverride() noexcept {
    if (m_resizeCursorOverride) {
        m_resizeCursorOverride = false;
        ApplyCurrentCursor();
    }
}

bool WaylandInput::TickKeyRepeat(keel::u64 nowMs, Context& ui) noexcept {
    if (m_repeatingKeycode == 0u || m_repeatRate <= 0 || nowMs < m_nextRepeatMs) {
        return false;
    }
    DispatchXkbKey(m_repeatingKeycode, ui);
    const keel::u64 intervalMs = keel::max<keel::u64>(10u, 1000u / keel::as<keel::u64>(m_repeatRate));
    m_nextRepeatMs = nowMs + intervalMs;
    return true;
}

keel::i32 WaylandInput::NextRepeatTimeoutMs(keel::u64 nowMs) const noexcept {
    if (m_repeatingKeycode == 0u || m_repeatRate <= 0) {
        return -1;
    }
    const keel::u64 rem = (m_nextRepeatMs > nowMs) ? (m_nextRepeatMs - nowMs) : 1u;
    return keel::as<keel::i32>(rem);
}

void WaylandInput::ApplyCurrentCursor() noexcept {
    if (!m_host.wl || !m_pointer || (!m_pointerOnSurface && !m_pointerBorderSurface)) return;

    if (m_cursorShapeDev) {
        const keel::u32 shape = m_resizeCursorOverride
            ? m_resizeOverrideShape
            : MapCursorToWpShape(m_activeCursor);
        wl_argument args[2]{};
        args[0].u = m_lastPointerEnterSerial;
        args[1].u = shape;
        m_host.wl->MarshalVoid(m_cursorShapeDev, kWpCursorShapeDevSetShape, args);
        if (m_host.display) {
            (void)m_host.wl->DisplayFlush(m_host.display);
        }
        return;
    }

    // Legacy wl_cursor fallback when wp-cursor-shape-v1 is unavailable
    if (!m_host.shm || !m_host.compositor) return;
    if (!m_wlCursor.lib.is_open() && !m_wlCursor.Load()) return;
    const keel::f32 dpi = (m_host.dpiScale && *m_host.dpiScale > 0.1f) ? *m_host.dpiScale : 1.0f;
    const keel::i32 curScale = keel::max(1, keel::round_i32(dpi));
    if (!m_cursorTheme || m_cursorThemeScale != curScale) {
        if (m_cursorTheme) {
            m_wlCursor.ThemeDestroy(m_cursorTheme);
            m_cursorTheme = nullptr;
        }
        m_cursorTheme = m_wlCursor.ThemeLoad(nullptr, 24 * curScale, m_host.shm);
        m_cursorThemeScale = curScale;
    }
    if (!m_cursorTheme) return;
    if (!m_cursorSurface) {
        wl_argument createArg[1]{};
        m_cursorSurface = m_host.wl->MarshalNew<wl_surface>(
            m_host.compositor,
            kWlCompositorCreateSurface,
            m_host.wl->ifaceSurface,
            createArg
        );
    }
    if (!m_cursorSurface) return;

    const CursorType effCursor = m_resizeCursorOverride ? m_resizeOverrideFallback : m_activeCursor;
    wl_cursor* cur = m_wlCursor.ThemeGetCursor(m_cursorTheme, MapCursorToLegacyName(effCursor));
    if (!cur) cur = m_wlCursor.ThemeGetCursor(m_cursorTheme, "left_ptr");
    if (!cur || cur->image_count == 0u || !cur->images || !cur->images[0]) return;

    wl_cursor_image* img = cur->images[0];
    wl_buffer* buf = m_wlCursor.ImageGetBuffer(img);
    if (!buf) return;

    if (m_host.wl->ProxyGetVersion(reinterpret_cast<wl_proxy*>(m_cursorSurface)) >= 3u) {
        wl_argument sArg[1]{};
        sArg[0].i = curScale;
        m_host.wl->MarshalVoid(m_cursorSurface, kWlSurfaceSetBufferScale, sArg);
    }
    wl_argument attachArgs[3]{};
    attachArgs[0].o = reinterpret_cast<wl_object*>(buf);
    attachArgs[1].i = 0;
    attachArgs[2].i = 0;
    m_host.wl->MarshalVoid(m_cursorSurface, kWlSurfaceAttach, attachArgs);

    wl_argument dmgArgs[4]{};
    dmgArgs[0].i = 0;
    dmgArgs[1].i = 0;
    dmgArgs[2].i = keel::as<keel::i32>(img->width);
    dmgArgs[3].i = keel::as<keel::i32>(img->height);
    m_host.wl->MarshalVoid(m_cursorSurface, kWlSurfaceDamage, dmgArgs);
    m_host.wl->MarshalVoid(m_cursorSurface, kWlSurfaceCommit);

    wl_argument setCurArgs[4]{};
    setCurArgs[0].u = m_lastPointerEnterSerial;
    setCurArgs[1].o = reinterpret_cast<wl_object*>(m_cursorSurface);
    setCurArgs[2].i = keel::as<keel::i32>(img->hotspot_x) / curScale;
    setCurArgs[3].i = keel::as<keel::i32>(img->hotspot_y) / curScale;
    m_host.wl->MarshalVoid(m_pointer, kWlPointerSetCursor, setCurArgs);
    if (m_host.display) {
        (void)m_host.wl->DisplayFlush(m_host.display);
    }
}

void WaylandInput::UpdateResizeCursorForRegion(WindowHitRegion reg, bool forceApply) noexcept {
    keel::u32 edge = 0u;
    keel::u32 shape = kWpCursorShapeDefault;
    CursorType fallbackCur = CursorType::Default;
    if (MapHitRegionToResizeEdge(reg, edge, shape, fallbackCur)) {
        const bool changed = !m_resizeCursorOverride || m_resizeOverrideShape != shape;
        m_resizeCursorOverride = true;
        m_resizeOverrideShape = shape;
        m_resizeOverrideFallback = fallbackCur;
        if (changed || forceApply) {
            ApplyCurrentCursor();
        }
    } else if (m_resizeCursorOverride || forceApply) {
        m_resizeCursorOverride = false;
        ApplyCurrentCursor();
    }
}

void WaylandInput::UpdateMainSurfaceResizeCursor(bool forceApply) noexcept {
    const bool csd = (m_host.customTitleBar && *m_host.customTitleBar);
    if (csd && m_host.hitTest) {
        const WindowHitRegion reg = m_host.hitTest(m_host.userData, m_pointerLogicalPos);
        UpdateResizeCursorForRegion(reg, forceApply);
    } else if (m_resizeCursorOverride || forceApply) {
        m_resizeCursorOverride = false;
        ApplyCurrentCursor();
    }
}

void WaylandInput::DispatchXkbKey(keel::u32 xkbKeycode, Context& ui) noexcept {
    if (!m_xkbState) return;
    const keel::u32 sym = m_xkb.StateKeyGetOneSym(m_xkbState, xkbKeycode);
    const Key logicalKey = linux_detail::MapLinuxKeySym(sym);
    if (logicalKey != Key::None) {
        ui.OnKeyDown(logicalKey, m_keyMods);
    }
    if (!m_keyMods.ctrl && !m_keyMods.alt) {
        char utf8Buf[64]{};
        const keel::i32 len = m_xkb.StateKeyGetUtf8(m_xkbState, xkbKeycode, utf8Buf, sizeof(utf8Buf) - 1u);
        if (len > 0) {
            const auto first = keel::as<keel::u8>(utf8Buf[0]);
            if (first >= 0x20u && first != 0x7Fu) {
                ui.OnTextInput(keel::StringRef(utf8Buf, keel::as<keel::usize>(len)));
            }
        }
    }
}

void WaylandInput::OnSeatCapabilities(void* data, wl_seat* seat, keel::u32 caps) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self || !self->m_host.wl) return;

    const bool hasPtr = (caps & kWlSeatCapPointer) != 0u;
    if (hasPtr && !self->m_pointer) {
        wl_argument arg[1]{};
        self->m_pointer = self->m_host.wl->MarshalNew<wl_pointer>(seat, kWlSeatGetPointer, self->m_host.wl->ifacePointer, arg);
        if (self->m_pointer) {
            self->m_host.wl->AddListener(self->m_pointer, &kPointerListener, self);
            if (self->m_host.cursorShapeMgr && self->m_host.ext && !self->m_cursorShapeDev) {
                wl_argument csArgs[2]{};
                csArgs[0].o = nullptr;
                csArgs[1].o = reinterpret_cast<wl_object*>(self->m_pointer);
                self->m_cursorShapeDev = self->m_host.wl->MarshalNew<wp_cursor_shape_device_v1>(
                    self->m_host.cursorShapeMgr,
                    kWpCursorShapeMgrGetPointer,
                    &self->m_host.ext->curShapeDevice,
                    csArgs
                );
            }
        }
    } else if (!hasPtr && self->m_pointer) {
        self->ReleasePointer();
    }

    const bool hasKbd = (caps & kWlSeatCapKeyboard) != 0u;
    if (hasKbd && !self->m_keyboard) {
        wl_argument arg[1]{};
        self->m_keyboard = self->m_host.wl->MarshalNew<wl_keyboard>(seat, kWlSeatGetKeyboard, self->m_host.wl->ifaceKeyboard, arg);
        if (self->m_keyboard) {
            self->m_host.wl->AddListener(self->m_keyboard, &kKeyboardListener, self);
        }
    } else if (!hasKbd && self->m_keyboard) {
        self->ReleaseKeyboard();
    }
}

void WaylandInput::OnSeatName(void*, wl_seat*, const char*) {}

void WaylandInput::OnPointerEnter(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf, WlFixed sx, WlFixed sy) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastPointerEnterSerial = serial;
    self->m_lastPointerSerial = serial;
    self->m_lastInputSerial = serial;
    self->m_pointerLogicalPos = {WlFixedToFloat(sx), WlFixedToFloat(sy)};

    if (surf == self->m_host.mainSurface) {
        self->m_pointerOnSurface = true;
        self->m_pointerBorderSurface = nullptr;
        self->UpdateMainSurfaceResizeCursor(/*forceApply=*/true);
        if (self->m_host.ui) {
            const keel::f32 dpi = (self->m_host.dpiScale && *self->m_host.dpiScale > 0.1f) ? *self->m_host.dpiScale : 1.0f;
            self->m_host.ui->OnMouseMove({self->m_pointerLogicalPos.x * dpi, self->m_pointerLogicalPos.y * dpi});
        }
        return;
    }

    WindowHitRegion borderReg = WindowHitRegion::Client;
    if (surf && self->m_host.borderHitTest &&
        self->m_host.borderHitTest(self->m_host.userData, surf, self->m_pointerLogicalPos, borderReg)) {
        self->m_pointerOnSurface = false;
        self->m_pointerBorderSurface = surf;
        self->UpdateResizeCursorForRegion(borderReg, /*forceApply=*/true);
        return;
    }

    self->m_pointerOnSurface = false;
    self->m_pointerBorderSurface = nullptr;
}

void WaylandInput::OnPointerLeave(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastPointerSerial = serial;
    if (surf == self->m_host.mainSurface) {
        self->m_pointerOnSurface = false;
        self->m_resizeCursorOverride = false;
        self->m_pendingWheelX = 0.0f;
        self->m_pendingWheelY = 0.0f;
        self->m_hasDiscreteWheelX = false;
        self->m_hasDiscreteWheelY = false;
    } else if (surf && surf == self->m_pointerBorderSurface) {
        self->m_pointerBorderSurface = nullptr;
        self->m_resizeCursorOverride = false;
    }
}

void WaylandInput::OnPointerMotion(void* data, wl_pointer*, keel::u32, WlFixed sx, WlFixed sy) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_pointerLogicalPos = {WlFixedToFloat(sx), WlFixedToFloat(sy)};

    if (self->m_pointerBorderSurface) {
        WindowHitRegion borderReg = WindowHitRegion::Client;
        if (self->m_host.borderHitTest &&
            self->m_host.borderHitTest(self->m_host.userData, self->m_pointerBorderSurface, self->m_pointerLogicalPos, borderReg)) {
            self->UpdateResizeCursorForRegion(borderReg, /*forceApply=*/false);
        }
        return;
    }

    if (!self->m_pointerOnSurface) return;
    self->UpdateMainSurfaceResizeCursor(/*forceApply=*/false);

    if (self->m_host.ui) {
        const keel::f32 dpi = (self->m_host.dpiScale && *self->m_host.dpiScale > 0.1f) ? *self->m_host.dpiScale : 1.0f;
        self->m_host.ui->OnMouseMove({self->m_pointerLogicalPos.x * dpi, self->m_pointerLogicalPos.y * dpi});
    }
}

void WaylandInput::OnPointerButton(void* data, wl_pointer*, keel::u32 serial, keel::u32, keel::u32 button, keel::u32 state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;

    if (self->m_pointerBorderSurface) {
        self->m_lastPointerSerial = serial;
        self->m_lastInputSerial = serial;
        if (button == kBtnLeft && state == kWlPointerButtonPressed &&
            self->m_host.borderHitTest && self->m_host.startResize) {
            WindowHitRegion borderReg = WindowHitRegion::Client;
            if (self->m_host.borderHitTest(self->m_host.userData, self->m_pointerBorderSurface, self->m_pointerLogicalPos, borderReg)) {
                keel::u32 edge = 0u;
                keel::u32 shape = 0u;
                CursorType fallbackCur = CursorType::Default;
                if (MapHitRegionToResizeEdge(borderReg, edge, shape, fallbackCur)) {
                    self->m_host.startResize(self->m_host.userData, serial, edge);
                }
            }
        }
        return;
    }

    if (!self->m_pointerOnSurface) return;
    self->m_lastPointerSerial = serial;
    self->m_lastInputSerial = serial;

    const bool csd = (self->m_host.customTitleBar && *self->m_host.customTitleBar);
    if (button == kBtnLeft && state == kWlPointerButtonPressed && csd && self->m_host.hitTest && self->m_host.startResize) {
        const WindowHitRegion reg = self->m_host.hitTest(self->m_host.userData, self->m_pointerLogicalPos);
        keel::u32 edge = 0u;
        keel::u32 shape = 0u;
        CursorType fallbackCur = CursorType::Default;
        if (MapHitRegionToResizeEdge(reg, edge, shape, fallbackCur)) {
            self->m_host.startResize(self->m_host.userData, serial, edge);
            return;
        }
    }

    if (!self->m_host.ui) return;
    const keel::f32 dpi = (self->m_host.dpiScale && *self->m_host.dpiScale > 0.1f) ? *self->m_host.dpiScale : 1.0f;
    self->m_host.ui->OnMouseMove({self->m_pointerLogicalPos.x * dpi, self->m_pointerLogicalPos.y * dpi});

    keel::i32 uiBtn = 0;
    if (button == kBtnLeft) uiBtn = 1;
    else if (button == kBtnMiddle) uiBtn = 2;
    else if (button == kBtnRight) uiBtn = 3;
    if (uiBtn == 0) return;

    if (state == kWlPointerButtonPressed) {
        self->m_host.ui->OnMouseDown(uiBtn);
    } else if (state == kWlPointerButtonReleased) {
        self->m_host.ui->OnMouseUp(uiBtn);
    }
}

void WaylandInput::OnPointerAxis(void* data, wl_pointer*, keel::u32, keel::u32 axis, WlFixed value) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self || !self->m_pointerOnSurface) return;
    const keel::f32 v = WlFixedToFloat(value);
    if (axis == kWlPointerAxisVertical) {
        if (!self->m_hasDiscreteWheelY) self->m_pendingWheelY -= v / 15.0f;
    } else if (axis == kWlPointerAxisHorizontal) {
        if (!self->m_hasDiscreteWheelX) self->m_pendingWheelX -= v / 15.0f;
    }
}

void WaylandInput::OnPointerFrame(void* data, wl_pointer*) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    if (self->m_host.ui && (keel::abs(self->m_pendingWheelX) > 1e-4f || keel::abs(self->m_pendingWheelY) > 1e-4f)) {
        self->m_host.ui->OnMouseWheel(self->m_pendingWheelX, self->m_pendingWheelY);
    }
    self->m_pendingWheelX = 0.0f;
    self->m_pendingWheelY = 0.0f;
    self->m_hasDiscreteWheelX = false;
    self->m_hasDiscreteWheelY = false;
}

void WaylandInput::OnPointerAxisSource(void*, wl_pointer*, keel::u32) {}
void WaylandInput::OnPointerAxisStop(void*, wl_pointer*, keel::u32, keel::u32) {}

void WaylandInput::OnPointerAxisDiscrete(void* data, wl_pointer*, keel::u32 axis, keel::i32 discrete) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self || !self->m_pointerOnSurface) return;
    if (axis == kWlPointerAxisVertical) {
        if (!self->m_hasDiscreteWheelY) {
            self->m_pendingWheelY = 0.0f;
            self->m_hasDiscreteWheelY = true;
        }
        self->m_pendingWheelY -= keel::as<keel::f32>(discrete);
    } else if (axis == kWlPointerAxisHorizontal) {
        if (!self->m_hasDiscreteWheelX) {
            self->m_pendingWheelX = 0.0f;
            self->m_hasDiscreteWheelX = true;
        }
        self->m_pendingWheelX -= keel::as<keel::f32>(discrete);
    }
}

void WaylandInput::OnPointerAxisValue120(void* data, wl_pointer*, keel::u32 axis, keel::i32 value120) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self || !self->m_pointerOnSurface) return;
    if (axis == kWlPointerAxisVertical) {
        if (!self->m_hasDiscreteWheelY) {
            self->m_pendingWheelY = 0.0f;
            self->m_hasDiscreteWheelY = true;
        }
        self->m_pendingWheelY -= keel::as<keel::f32>(value120) / 120.0f;
    } else if (axis == kWlPointerAxisHorizontal) {
        if (!self->m_hasDiscreteWheelX) {
            self->m_pendingWheelX = 0.0f;
            self->m_hasDiscreteWheelX = true;
        }
        self->m_pendingWheelX -= keel::as<keel::f32>(value120) / 120.0f;
    }
}

void WaylandInput::OnPointerAxisRelativeDirection(void*, wl_pointer*, keel::u32, keel::u32) {}

void WaylandInput::OnKeyboardKeymap(void* data, wl_keyboard*, keel::u32 format, keel::i32 fd, keel::u32 size) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self || format != kWlKeyboardKeymapXkbV1 || fd < 0 || size == 0u || !self->m_xkbCtx) {
        if (fd >= 0) ::close(fd);
        return;
    }
    void* mapped = ::mmap(nullptr, keel::as<keel::usize>(size), PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED || !mapped) return;

    xkb_keymap* km = self->m_xkb.KeymapNewFromString(
        self->m_xkbCtx,
        static_cast<const char*>(mapped),
        XkbApi::kKeymapFormatTextV1,
        XkbApi::kKeymapCompileNoFlags
    );
    (void)::munmap(mapped, keel::as<keel::usize>(size));
    if (!km) return;

    xkb_state* st = self->m_xkb.StateNew(km);
    if (!st) {
        self->m_xkb.KeymapUnref(km);
        return;
    }
    if (self->m_xkbState) self->m_xkb.StateUnref(self->m_xkbState);
    if (self->m_xkbKeymap) self->m_xkb.KeymapUnref(self->m_xkbKeymap);
    self->m_xkbKeymap = km;
    self->m_xkbState = st;
}

void WaylandInput::OnKeyboardEnter(void* data, wl_keyboard*, keel::u32 serial, wl_surface*, wl_array*) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastInputSerial = serial;
    if (self->m_host.setFocused) {
        self->m_host.setFocused(self->m_host.userData, true);
    }
    if (self->m_host.ui) {
        self->m_host.ui->InvalidateAll();
        self->m_host.ui->RequestRepaint(2);
    }
}

void WaylandInput::OnKeyboardLeave(void* data, wl_keyboard*, keel::u32 serial, wl_surface*) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastInputSerial = serial;
    self->m_repeatingKeycode = 0u;
    self->m_keyMods = {};
    if (self->m_host.ui) {
        self->m_host.ui->SetKeyModifiers({});
        self->m_host.ui->InvalidateAll();
        self->m_host.ui->RequestRepaint(2);
    }
}

void WaylandInput::OnKeyboardKey(void* data, wl_keyboard*, keel::u32 serial, keel::u32, keel::u32 key, keel::u32 state) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastInputSerial = serial;
    const keel::u32 xkbKeycode = key + 8u;
    if (state == kWlKeyboardKeyPressed) {
        if (self->m_host.ui) {
            self->DispatchXkbKey(xkbKeycode, *self->m_host.ui);
        }
        if (self->m_repeatRate > 0 && self->m_xkbKeymap &&
            (!self->m_xkb.KeymapKeyRepeats || self->m_xkb.KeymapKeyRepeats(self->m_xkbKeymap, xkbKeycode) != 0)) {
            self->m_repeatingKeycode = xkbKeycode;
            self->m_nextRepeatMs = keel::monotonic_ms() + keel::as<keel::u64>(keel::max(100, self->m_repeatDelayMs));
        }
    } else if (state == kWlKeyboardKeyReleased) {
        if (self->m_repeatingKeycode == xkbKeycode) {
            self->m_repeatingKeycode = 0u;
        }
    }
}

void WaylandInput::OnKeyboardModifiers(
    void* data,
    wl_keyboard*,
    keel::u32 serial,
    keel::u32 depressed,
    keel::u32 latched,
    keel::u32 locked,
    keel::u32 group
) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_lastInputSerial = serial;
    if (self->m_xkbState) {
        (void)self->m_xkb.StateUpdateMask(self->m_xkbState, depressed, latched, locked, 0u, 0u, group);
        self->m_keyMods.shift = self->m_xkb.StateModNameIsActive(self->m_xkbState, "Shift", XkbApi::kStateModsEffective) > 0;
        self->m_keyMods.ctrl = self->m_xkb.StateModNameIsActive(self->m_xkbState, "Control", XkbApi::kStateModsEffective) > 0;
        self->m_keyMods.alt = self->m_xkb.StateModNameIsActive(self->m_xkbState, "Mod1", XkbApi::kStateModsEffective) > 0;
        if (self->m_host.ui) {
            self->m_host.ui->SetKeyModifiers(self->m_keyMods);
        }
    }
}

void WaylandInput::OnKeyboardRepeatInfo(void* data, wl_keyboard*, keel::i32 rate, keel::i32 delay) {
    auto* self = static_cast<WaylandInput*>(data);
    if (!self) return;
    self->m_repeatRate = keel::max(0, rate);
    self->m_repeatDelayMs = keel::max(50, delay);
}

#else

bool WaylandInput::IsXkbRuntimeAvailable() noexcept { return false; }
bool WaylandInput::MapHitRegionToResizeEdge(WindowHitRegion, keel::u32&, keel::u32&, CursorType&) noexcept { return false; }
bool WaylandInput::Init(const WaylandInputHost&) noexcept { return false; }
void WaylandInput::BindSeat(wl_seat*) noexcept {}
void WaylandInput::Destroy() noexcept {}
void WaylandInput::SetCursor(CursorType) noexcept {}
void WaylandInput::ClearResizeCursorOverride() noexcept {}
bool WaylandInput::TickKeyRepeat(keel::u64, Context&) noexcept { return false; }
keel::i32 WaylandInput::NextRepeatTimeoutMs(keel::u64) const noexcept { return -1; }

#endif

} // namespace guikit::native::wl_detail
