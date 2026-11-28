#include "guikit/native/wayland_platform_window.hpp"

#include "guikit/context.hpp"
#include "keel/array.h"
#include "keel/env.h"
#include "keel/format.h"
#include "keel/math.h"
#include "keel/memory.h"
#include "keel/string.h"
#include "keel/time.h"
#include "linux/linux_desktop_services.hpp"
#include "linux/linux_egl_context.hpp"
#include "linux/wayland_protocols.hpp"

#if defined(__linux__) && !defined(__ANDROID__)
#define GUIKIT_NATIVE_COMPILE_WAYLAND 1
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#define GUIKIT_NATIVE_COMPILE_WAYLAND 0
#endif

namespace guikit::native {

#if GUIKIT_NATIVE_COMPILE_WAYLAND

namespace {

using namespace wl_detail;

static constexpr keel::u32 kBtnLeft   = 0x110u; // BTN_LEFT
static constexpr keel::u32 kBtnRight  = 0x111u; // BTN_RIGHT
static constexpr keel::u32 kBtnMiddle = 0x112u; // BTN_MIDDLE

static constexpr keel::u64 kFrameCallbackWatchdogMs = 120u;

[[nodiscard]] Key MapXkbKeySym(keel::u32 sym) noexcept {
    switch (sym) {
        case 0xFF08u: return Key::Backspace; // XKB_KEY_BackSpace
        case 0xFFFFu:
        case 0xFF9Fu: return Key::Delete;    // Delete / KP_Delete
        case 0xFF0Du:
        case 0xFF8Du: return Key::Enter;     // Return / KP_Enter
        case 0xFF1Bu: return Key::Escape;    // Escape
        case 0xFF09u:
        case 0xFE20u: return Key::Tab;       // Tab / ISO_Left_Tab
        case 0x0020u: return Key::Space;
        case 0xFF51u:
        case 0xFF96u: return Key::Left;
        case 0xFF53u:
        case 0xFF98u: return Key::Right;
        case 0xFF50u:
        case 0xFF95u: return Key::Home;
        case 0xFF57u:
        case 0xFF9Cu: return Key::End;
        case 0xFF52u:
        case 0xFF97u: return Key::Up;
        case 0xFF54u:
        case 0xFF99u: return Key::Down;
        case 0xFF55u:
        case 0xFF9Au: return Key::PageUp;
        case 0xFF56u:
        case 0xFF9Bu: return Key::PageDown;
        case 'a': case 'A': return Key::A;
        case 'c': case 'C': return Key::C;
        case 'd': case 'D': return Key::D;
        case 'f': case 'F': return Key::F;
        case 'g': case 'G': return Key::G;
        case 'h': case 'H': return Key::H;
        case 'k': case 'K': return Key::K;
        case 'v': case 'V': return Key::V;
        case 'x': case 'X': return Key::X;
        case 'y': case 'Y': return Key::Y;
        case 'z': case 'Z': return Key::Z;
        case '/': case 0xFFAFu: return Key::Slash;
        case '+': case '=': case 0xFFABu: return Key::Plus;
        case '-': case 0xFFADu: return Key::Minus;
        case '0': case 0xFFB0u: return Key::Zero;
        case 0xFFC0u: return Key::F3;
        case 0xFFC2u: return Key::F5;
        case 0xFFC6u: return Key::F9;
        case 0xFFC7u: return Key::F10;
        case 0xFFC8u: return Key::F11;
        default: return Key::None;
    }
}

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

[[nodiscard]] bool MapHitRegionToWaylandResize(
    WindowHitRegion reg,
    keel::u32& outEdge,
    keel::u32& outShape,
    CursorType& outFallbackCursor
) noexcept {
    switch (reg) {
        case WindowHitRegion::ResizeTopLeft:
            outEdge = kXdgResizeEdgeTopLeft;
            outShape = kWpCursorShapeNwResize;
            outFallbackCursor = CursorType::ResizeNWSE;
            return true;
        case WindowHitRegion::ResizeTop:
            outEdge = kXdgResizeEdgeTop;
            outShape = kWpCursorShapeNResize;
            outFallbackCursor = CursorType::ResizeNS;
            return true;
        case WindowHitRegion::ResizeTopRight:
            outEdge = kXdgResizeEdgeTopRight;
            outShape = kWpCursorShapeNeResize;
            outFallbackCursor = CursorType::ResizeNESW;
            return true;
        case WindowHitRegion::ResizeRight:
            outEdge = kXdgResizeEdgeRight;
            outShape = kWpCursorShapeEResize;
            outFallbackCursor = CursorType::ResizeEW;
            return true;
        case WindowHitRegion::ResizeBottomRight:
            outEdge = kXdgResizeEdgeBottomRight;
            outShape = kWpCursorShapeSeResize;
            outFallbackCursor = CursorType::ResizeNWSE;
            return true;
        case WindowHitRegion::ResizeBottom:
            outEdge = kXdgResizeEdgeBottom;
            outShape = kWpCursorShapeSResize;
            outFallbackCursor = CursorType::ResizeNS;
            return true;
        case WindowHitRegion::ResizeBottomLeft:
            outEdge = kXdgResizeEdgeBottomLeft;
            outShape = kWpCursorShapeSwResize;
            outFallbackCursor = CursorType::ResizeNESW;
            return true;
        case WindowHitRegion::ResizeLeft:
            outEdge = kXdgResizeEdgeLeft;
            outShape = kWpCursorShapeWResize;
            outFallbackCursor = CursorType::ResizeEW;
            return true;
        default:
            return false;
    }
}

struct OutputInfo {
    wl_output* proxy{nullptr};
    keel::u32 globalName{0u};
    keel::i32 scale{1};
    keel::i32 physicalWidthMm{0};
    keel::i32 modeWidthPx{0};
    bool surfaceEntered{false};
};

struct DataOfferEntry {
    wl_data_offer* offer{nullptr};
    bool hasUtf8Mime{false};
    bool hasUtf8String{false};
    bool hasPlainText{false};
};

} // namespace

struct WaylandPlatformWindow::Impl {
    WaylandClientApi wl{};
    WaylandEglApi wlEgl{};
    WaylandCursorApi wlCursor{};
    XkbApi xkb{};
    WaylandExtInterfaces ext{};
    linux_detail::LinuxEglContext egl{};

    wl_display* display{nullptr};
    wl_registry* registry{nullptr};
    wl_compositor* compositor{nullptr};
    wl_shm* shm{nullptr};
    wl_seat* seat{nullptr};
    wl_pointer* pointer{nullptr};
    wl_keyboard* keyboard{nullptr};
    wl_data_device_manager* dataDeviceMgr{nullptr};
    wl_data_device* dataDevice{nullptr};
    wl_data_source* ourDataSource{nullptr};
    wl_data_offer* selectionOffer{nullptr};
    keel::String selectionMime{};
    keel::Array<DataOfferEntry> pendingOffers{};
    keel::Array<OutputInfo> outputs{};

    xdg_wm_base* xdgWmBase{nullptr};
    zxdg_decoration_manager_v1* decoMgr{nullptr};
    wp_viewporter* viewporter{nullptr};
    wp_fractional_scale_manager_v1* fracScaleMgr{nullptr};
    wp_cursor_shape_manager_v1* cursorShapeMgr{nullptr};

    wl_surface* surface{nullptr};
    xdg_surface* xdgSurface{nullptr};
    xdg_toplevel* xdgToplevel{nullptr};
    zxdg_toplevel_decoration_v1* toplevelDeco{nullptr};
    wp_viewport* viewport{nullptr};
    wp_fractional_scale_v1* fracScale{nullptr};
    wp_cursor_shape_device_v1* cursorShapeDev{nullptr};
    wl_egl_window* eglWindow{nullptr};

    // Legacy cursor fallback when wp_cursor_shape_manager_v1 is unavailable
    wl_surface* cursorSurface{nullptr};
    wl_cursor_theme* cursorTheme{nullptr};
    keel::i32 cursorThemeScale{0};

    // Frame callback VSync pacing
    wl_callback* frameCallback{nullptr};
    bool frameCallbackPending{false};
    keel::u64 frameSubmittedMs{0u};

    // XKB keyboard state & client-side key repeat
    xkb_context* xkbCtx{nullptr};
    xkb_keymap* xkbKeymap{nullptr};
    xkb_state* xkbState{nullptr};
    KeyModifiers keyMods{};
    keel::i32 repeatRate{25};
    keel::i32 repeatDelayMs{400};
    keel::u32 repeatingKeycode{0u};
    keel::u64 nextRepeatMs{0u};

    // Surface geometry, scale & atomic configure state
    keel::i32 logicalWidth{1380};
    keel::i32 logicalHeight{800};
    keel::i32 fbWidth{1380};
    keel::i32 fbHeight{800};
    keel::i32 minWidth{320};
    keel::i32 minHeight{240};
    bool resizable{true};
    bool borderless{false};
    bool highDpi{true};
    keel::u32 fractionalScale120{0u};
    keel::i32 preferredBufferScale{1};
    keel::f32 dpiScale{1.0f};
    keel::f32 pixelsPerMm{96.0f / 25.4f};
    bool scaleDirty{false};

    bool hasPendingConfigure{false};
    bool initialConfigureDone{false};
    keel::u32 pendingConfigureSerial{0u};
    keel::i32 pendingLogicalW{0};
    keel::i32 pendingLogicalH{0};
    bool pendingMaximized{false};
    bool pendingFullscreen{false};
    bool pendingResizing{false};
    bool pendingFocused{true};
    bool pendingSuspended{false};

    // Window state
    bool vsync{true};
    bool customTitleBar{false};
    bool compositorSupportsSsd{false};
    bool maximized{false};
    bool fullscreen{false};
    bool resizing{false};
    bool focused{true};
    bool suspended{false};
    bool closeRequested{false};
    bool alive{false};

    // Pointer & cursor state
    bool pointerOnSurface{false};
    Vec2 pointerLogicalPos{0.0f, 0.0f};
    keel::u32 lastPointerEnterSerial{0u};
    keel::u32 lastPointerSerial{0u};
    keel::u32 lastInputSerial{0u};
    CursorType activeCursor{CursorType::Default};
    bool resizeCursorOverride{false};
    keel::u32 resizeOverrideShape{kWpCursorShapeDefault};
    CursorType resizeOverrideFallback{CursorType::Default};

    // Pending wheel accumulation flushed on wl_pointer.frame
    keel::f32 pendingWheelX{0.0f};
    keel::f32 pendingWheelY{0.0f};
    bool hasDiscreteWheelX{false};
    bool hasDiscreteWheelY{false};

    keel::i32 wakeFd{-1};
    Context* activeUi{nullptr};
    WindowHostCallbacks callbacks{};
    keel::String clipboardText{};
    bool clipboardOwnedLocally{false};

    // --- Listeners (static C callbacks) ---
    static const wl_registry_listener kRegistryListener;
    static const wl_callback_listener kFrameCallbackListener;
    static const wl_surface_listener kSurfaceListener;
    static const wl_seat_listener kSeatListener;
    static const wl_pointer_listener kPointerListener;
    static const wl_keyboard_listener kKeyboardListener;
    static const wl_output_listener kOutputListener;
    static const wl_data_device_listener kDataDeviceListener;
    static const wl_data_source_listener kDataSourceListener;
    static const wl_data_offer_listener kDataOfferListener;
    static const xdg_wm_base_listener kXdgWmBaseListener;
    static const xdg_surface_listener kXdgSurfaceListener;
    static const xdg_toplevel_listener kXdgToplevelListener;
    static const zxdg_toplevel_decoration_v1_listener kDecoListener;
    static const wp_fractional_scale_v1_listener kFracScaleListener;

    void DestroyOffer(wl_data_offer* offer) noexcept {
        if (!offer) return;
        wl.MarshalDestroy(offer, kWlDataOfferDestroy);
    }

    void ClearPendingOffers() noexcept {
        for (const DataOfferEntry& e : pendingOffers) {
            if (e.offer && e.offer != selectionOffer) {
                DestroyOffer(e.offer);
            }
        }
        pendingOffers.clear();
    }

    void DestroyAll() noexcept {
        alive = false;
        activeUi = nullptr;

        if (frameCallback) {
            wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(frameCallback));
            frameCallback = nullptr;
        }
        frameCallbackPending = false;

        egl.Destroy();
        if (eglWindow && wlEgl.Destroy) {
            wlEgl.Destroy(eglWindow);
            eglWindow = nullptr;
        }

        if (cursorTheme && wlCursor.ThemeDestroy) {
            wlCursor.ThemeDestroy(cursorTheme);
            cursorTheme = nullptr;
        }
        if (cursorSurface) {
            wl.MarshalDestroy(cursorSurface, kWlSurfaceDestroy);
            cursorSurface = nullptr;
        }
        if (cursorShapeDev) {
            wl.MarshalDestroy(cursorShapeDev, kWpCursorShapeDevDestroy);
            cursorShapeDev = nullptr;
        }
        if (fracScale) {
            wl.MarshalDestroy(fracScale, kWpFracScaleDestroy);
            fracScale = nullptr;
        }
        if (viewport) {
            wl.MarshalDestroy(viewport, kWpViewportDestroy);
            viewport = nullptr;
        }
        if (toplevelDeco) {
            wl.MarshalDestroy(toplevelDeco, kZxdgToplevelDecoDestroy);
            toplevelDeco = nullptr;
        }
        if (xdgToplevel) {
            wl.MarshalDestroy(xdgToplevel, kXdgToplevelDestroy);
            xdgToplevel = nullptr;
        }
        if (xdgSurface) {
            wl.MarshalDestroy(xdgSurface, kXdgSurfaceDestroy);
            xdgSurface = nullptr;
        }
        if (surface) {
            wl.MarshalDestroy(surface, kWlSurfaceDestroy);
            surface = nullptr;
        }

        ClearPendingOffers();
        if (selectionOffer) {
            DestroyOffer(selectionOffer);
            selectionOffer = nullptr;
        }
        if (ourDataSource) {
            wl.MarshalDestroy(ourDataSource, kWlDataSourceDestroy);
            ourDataSource = nullptr;
        }
        if (dataDevice) {
            if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(dataDevice)) >= 2u) {
                wl.MarshalDestroy(dataDevice, kWlDataDeviceRelease);
            } else {
                wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(dataDevice));
            }
            dataDevice = nullptr;
        }
        if (keyboard) {
            if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(keyboard)) >= 3u) {
                wl.MarshalDestroy(keyboard, kWlKeyboardRelease);
            } else {
                wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(keyboard));
            }
            keyboard = nullptr;
        }
        if (pointer) {
            if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(pointer)) >= 3u) {
                wl.MarshalDestroy(pointer, kWlPointerRelease);
            } else {
                wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(pointer));
            }
            pointer = nullptr;
        }
        if (seat) {
            if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(seat)) >= 5u) {
                wl.MarshalDestroy(seat, kWlSeatRelease);
            } else {
                wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(seat));
            }
            seat = nullptr;
        }
        for (OutputInfo& out : outputs) {
            if (out.proxy) {
                if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(out.proxy)) >= 3u) {
                    wl.MarshalDestroy(out.proxy, kWlOutputRelease);
                } else {
                    wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(out.proxy));
                }
                out.proxy = nullptr;
            }
        }
        outputs.clear();

        if (cursorShapeMgr) {
            wl.MarshalDestroy(cursorShapeMgr, kWpCursorShapeMgrDestroy);
            cursorShapeMgr = nullptr;
        }
        if (fracScaleMgr) {
            wl.MarshalDestroy(fracScaleMgr, kWpFracScaleMgrDestroy);
            fracScaleMgr = nullptr;
        }
        if (viewporter) {
            wl.MarshalDestroy(viewporter, kWpViewporterDestroy);
            viewporter = nullptr;
        }
        if (decoMgr) {
            wl.MarshalDestroy(decoMgr, kZxdgDecoMgrDestroy);
            decoMgr = nullptr;
        }
        if (xdgWmBase) {
            wl.MarshalDestroy(xdgWmBase, kXdgWmBaseDestroy);
            xdgWmBase = nullptr;
        }
        if (dataDeviceMgr) {
            wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(dataDeviceMgr));
            dataDeviceMgr = nullptr;
        }
        if (shm) {
            wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(shm));
            shm = nullptr;
        }
        if (compositor) {
            wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(compositor));
            compositor = nullptr;
        }
        if (registry) {
            wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(registry));
            registry = nullptr;
        }
        if (display && wl.DisplayDisconnect) {
            wl.DisplayDisconnect(display);
            display = nullptr;
        }

        if (xkbState && xkb.StateUnref) {
            xkb.StateUnref(xkbState);
            xkbState = nullptr;
        }
        if (xkbKeymap && xkb.KeymapUnref) {
            xkb.KeymapUnref(xkbKeymap);
            xkbKeymap = nullptr;
        }
        if (xkbCtx && xkb.ContextUnref) {
            xkb.ContextUnref(xkbCtx);
            xkbCtx = nullptr;
        }

        if (wakeFd >= 0) {
            ::close(wakeFd);
            wakeFd = -1;
        }
        callbacks = {};
    }

    void RefreshPixelsPerMm() noexcept {
        for (const OutputInfo& out : outputs) {
            if (out.surfaceEntered && out.modeWidthPx > 320 && out.physicalWidthMm > 50) {
                pixelsPerMm = keel::as<keel::f32>(out.modeWidthPx) / keel::as<keel::f32>(out.physicalWidthMm);
                return;
            }
        }
        for (const OutputInfo& out : outputs) {
            if (out.modeWidthPx > 320 && out.physicalWidthMm > 50) {
                pixelsPerMm = keel::as<keel::f32>(out.modeWidthPx) / keel::as<keel::f32>(out.physicalWidthMm);
                return;
            }
        }
        pixelsPerMm = (96.0f * dpiScale) / 25.4f;
    }

    void RefreshFallbackBufferScale() noexcept {
        if (!highDpi) {
            preferredBufferScale = 1;
            return;
        }
        keel::i32 best = 1;
        bool anyEntered = false;
        for (const OutputInfo& out : outputs) {
            if (out.surfaceEntered) {
                anyEntered = true;
                if (out.scale > best) best = out.scale;
            }
        }
        if (!anyEntered) {
            for (const OutputInfo& out : outputs) {
                if (out.scale > best) best = out.scale;
            }
        }
        if (best != preferredBufferScale) {
            preferredBufferScale = keel::max(1, best);
            scaleDirty = true;
        }
        RefreshPixelsPerMm();
    }

    void ComputeTargetSurfaceMetrics(keel::i32 logW, keel::i32 logH, keel::i32& outFbW, keel::i32& outFbH, keel::f32& outDpi) const noexcept {
        if (highDpi && fractionalScale120 > 0u && viewport != nullptr) {
            outDpi = keel::clamp(keel::as<keel::f32>(fractionalScale120) / 120.0f, 0.5f, 4.0f);
            outFbW = keel::max<keel::i32>(1, keel::as<keel::i32>((keel::as<keel::u64>(logW) * fractionalScale120 + 60u) / 120u));
            outFbH = keel::max<keel::i32>(1, keel::as<keel::i32>((keel::as<keel::u64>(logH) * fractionalScale120 + 60u) / 120u));
        } else {
            const keel::i32 intScale = highDpi ? keel::max(1, preferredBufferScale) : 1;
            outDpi = keel::as<keel::f32>(intScale);
            outFbW = keel::max<keel::i32>(1, logW * intScale);
            outFbH = keel::max<keel::i32>(1, logH * intScale);
        }
    }

    void UpdateOpaqueRegion(keel::i32 logW, keel::i32 logH) noexcept {
        if (!compositor || !surface) return;
        wl_argument createArgs[1]{};
        wl_region* reg = wl.MarshalNew<wl_region>(compositor, kWlCompositorCreateRegion, wl.ifaceRegion, createArgs);
        if (!reg) return;
        wl_argument addArgs[4]{};
        addArgs[0].i = 0;
        addArgs[1].i = 0;
        addArgs[2].i = logW;
        addArgs[3].i = logH;
        wl.MarshalVoid(reg, kWlRegionAdd, addArgs);

        wl_argument setArgs[1]{};
        setArgs[0].o = reinterpret_cast<wl_object*>(reg);
        wl.MarshalVoid(surface, kWlSurfaceSetOpaqueRegion, setArgs);
        wl.MarshalDestroy(reg, kWlRegionDestroy);
    }

    /// Atomically applies pending xdg_surface.configure + fractional/integer scale before rendering the frame.
    /// Does NOT call wl_surface_commit: eglSwapBuffers() commits the new buffer + ack_configure + viewport together!
    void ApplyPendingSurfaceState(Context* ui, bool duringInit) noexcept {
        const bool hadConfigure = hasPendingConfigure;
        const keel::u32 ackSerial = pendingConfigureSerial;

        if (hadConfigure) {
            if (pendingLogicalW > 0 && pendingLogicalH > 0) {
                logicalWidth = keel::max(pendingLogicalW, 64);
                logicalHeight = keel::max(pendingLogicalH, 64);
            }
            const bool stateChanged =
                (maximized != pendingMaximized) ||
                (fullscreen != pendingFullscreen) ||
                (focused != pendingFocused);
            maximized = pendingMaximized;
            fullscreen = pendingFullscreen;
            resizing = pendingResizing;
            focused = pendingFocused;
            suspended = pendingSuspended;
            hasPendingConfigure = false;
            if (stateChanged && ui) {
                ui->InvalidateAll();
                ui->RequestRepaint(2);
            }
        }

        keel::i32 targetFbW = fbWidth;
        keel::i32 targetFbH = fbHeight;
        keel::f32 targetDpi = dpiScale;
        ComputeTargetSurfaceMetrics(logicalWidth, logicalHeight, targetFbW, targetFbH, targetDpi);

        const bool sizeChanged = (targetFbW != fbWidth || targetFbH != fbHeight);
        const bool dpiChanged = (keel::abs(targetDpi - dpiScale) > 0.001f);
        fbWidth = targetFbW;
        fbHeight = targetFbH;
        dpiScale = targetDpi;
        scaleDirty = false;
        RefreshPixelsPerMm();

        if (surface) {
            if (highDpi && fractionalScale120 > 0u && viewport) {
                // wp-viewporter requires buffer_scale == 1 when destination viewport is set
                if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(surface)) >= 3u) {
                    wl_argument scaleArg[1]{};
                    scaleArg[0].i = 1;
                    wl.MarshalVoid(surface, kWlSurfaceSetBufferScale, scaleArg);
                }
                wl_argument dstArgs[2]{};
                dstArgs[0].i = logicalWidth;
                dstArgs[1].i = logicalHeight;
                wl.MarshalVoid(viewport, kWpViewportSetDestination, dstArgs);
            } else if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(surface)) >= 3u) {
                wl_argument scaleArg[1]{};
                scaleArg[0].i = highDpi ? keel::max(1, preferredBufferScale) : 1;
                wl.MarshalVoid(surface, kWlSurfaceSetBufferScale, scaleArg);
            }
            UpdateOpaqueRegion(logicalWidth, logicalHeight);
        }

        if (xdgSurface) {
            wl_argument geomArgs[4]{};
            geomArgs[0].i = 0;
            geomArgs[1].i = 0;
            geomArgs[2].i = logicalWidth;
            geomArgs[3].i = logicalHeight;
            wl.MarshalVoid(xdgSurface, kXdgSurfaceSetWindowGeometry, geomArgs);
        }

        if (eglWindow && wlEgl.Resize && (sizeChanged || dpiChanged || hadConfigure)) {
            wlEgl.Resize(eglWindow, fbWidth, fbHeight, 0, 0);
        }

        if (hadConfigure && xdgSurface) {
            wl_argument ackArg[1]{};
            ackArg[0].u = ackSerial;
            wl.MarshalVoid(xdgSurface, kXdgSurfaceAckConfigure, ackArg);
        }

        if (!duringInit && (sizeChanged || dpiChanged || hadConfigure)) {
            if (sizeChanged && callbacks.onResize) {
                callbacks.onResize(fbWidth, fbHeight);
            }
            if (ui) {
                ui->InvalidateAll();
            }
        }
    }

    void ApplyCurrentCursor() noexcept {
        if (!pointer || !pointerOnSurface) return;

        if (cursorShapeDev) {
            const keel::u32 shape = resizeCursorOverride
                ? resizeOverrideShape
                : MapCursorToWpShape(activeCursor);
            wl_argument args[2]{};
            args[0].u = lastPointerEnterSerial;
            args[1].u = shape;
            wl.MarshalVoid(cursorShapeDev, kWpCursorShapeDevSetShape, args);
            return;
        }

        // Legacy wl_cursor fallback for compositors without wp-cursor-shape-v1
        if (!shm || !compositor) return;
        if (!wlCursor.lib.is_open() && !wlCursor.Load()) return;
        const keel::i32 curScale = keel::max(1, keel::round_i32(dpiScale));
        if (!cursorTheme || cursorThemeScale != curScale) {
            if (cursorTheme) {
                wlCursor.ThemeDestroy(cursorTheme);
                cursorTheme = nullptr;
            }
            cursorTheme = wlCursor.ThemeLoad(nullptr, 24 * curScale, shm);
            cursorThemeScale = curScale;
        }
        if (!cursorTheme) return;
        if (!cursorSurface) {
            wl_argument createArg[1]{};
            cursorSurface = wl.MarshalNew<wl_surface>(compositor, kWlCompositorCreateSurface, wl.ifaceSurface, createArg);
        }
        if (!cursorSurface) return;

        const CursorType effCursor = resizeCursorOverride ? resizeOverrideFallback : activeCursor;
        wl_cursor* cur = wlCursor.ThemeGetCursor(cursorTheme, MapCursorToLegacyName(effCursor));
        if (!cur) cur = wlCursor.ThemeGetCursor(cursorTheme, "left_ptr");
        if (!cur || cur->image_count == 0u || !cur->images || !cur->images[0]) return;

        wl_cursor_image* img = cur->images[0];
        wl_buffer* buf = wlCursor.ImageGetBuffer(img);
        if (!buf) return;

        if (wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(cursorSurface)) >= 3u) {
            wl_argument sArg[1]{};
            sArg[0].i = curScale;
            wl.MarshalVoid(cursorSurface, kWlSurfaceSetBufferScale, sArg);
        }
        wl_argument attachArgs[3]{};
        attachArgs[0].o = reinterpret_cast<wl_object*>(buf);
        attachArgs[1].i = 0;
        attachArgs[2].i = 0;
        wl.MarshalVoid(cursorSurface, kWlSurfaceAttach, attachArgs);
        wl_argument dmgArgs[4]{};
        dmgArgs[0].i = 0;
        dmgArgs[1].i = 0;
        dmgArgs[2].i = keel::as<keel::i32>(img->width);
        dmgArgs[3].i = keel::as<keel::i32>(img->height);
        wl.MarshalVoid(cursorSurface, kWlSurfaceDamage, dmgArgs);
        wl.MarshalVoid(cursorSurface, kWlSurfaceCommit);

        wl_argument setCurArgs[4]{};
        setCurArgs[0].u = lastPointerEnterSerial;
        setCurArgs[1].o = reinterpret_cast<wl_object*>(cursorSurface);
        setCurArgs[2].i = keel::as<keel::i32>(img->hotspot_x) / curScale;
        setCurArgs[3].i = keel::as<keel::i32>(img->hotspot_y) / curScale;
        wl.MarshalVoid(pointer, kWlPointerSetCursor, setCurArgs);
    }

    void DispatchXkbKey(keel::u32 xkbKeycode, Context& ui) noexcept {
        if (!xkbState) return;
        const keel::u32 sym = xkb.StateKeyGetOneSym(xkbState, xkbKeycode);
        const Key logicalKey = MapXkbKeySym(sym);
        if (logicalKey != Key::None) {
            ui.OnKeyDown(logicalKey, keyMods);
        }
        if (!keyMods.ctrl && !keyMods.alt) {
            char utf8Buf[64]{};
            const keel::i32 len = xkb.StateKeyGetUtf8(xkbState, xkbKeycode, utf8Buf, sizeof(utf8Buf) - 1u);
            if (len > 0) {
                const auto first = keel::as<keel::u8>(utf8Buf[0]);
                if (first >= 0x20u && first != 0x7Fu) {
                    ui.OnTextInput(keel::StringRef(utf8Buf, keel::as<keel::usize>(len)));
                }
            }
        }
    }

    void SetSystemClipboard(keel::StringRef text) {
        clipboardText = keel::String::from(text);
        clipboardOwnedLocally = true;
        if (!dataDeviceMgr || !dataDevice || lastInputSerial == 0u) {
            return;
        }
        if (ourDataSource) {
            wl.MarshalDestroy(ourDataSource, kWlDataSourceDestroy);
            ourDataSource = nullptr;
        }
        wl_argument createArg[1]{};
        ourDataSource = wl.MarshalNew<wl_data_source>(
            dataDeviceMgr,
            kWlDataDevMgrCreateSource,
            wl.ifaceDataSource,
            createArg
        );
        if (!ourDataSource) return;

        wl.AddListener(ourDataSource, &kDataSourceListener, this);
        static constexpr const char* kMimes[3] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};
        for (const char* mime : kMimes) {
            wl_argument offerArg[1]{};
            offerArg[0].s = mime;
            wl.MarshalVoid(ourDataSource, kWlDataSourceOffer, offerArg);
        }

        wl_argument selArgs[2]{};
        selArgs[0].o = reinterpret_cast<wl_object*>(ourDataSource);
        selArgs[1].u = lastInputSerial;
        wl.MarshalVoid(dataDevice, kWlDataDeviceSetSelection, selArgs);
        (void)wl.DisplayFlush(display);
    }

    [[nodiscard]] keel::String GetSystemClipboard() {
        if (ourDataSource != nullptr || clipboardOwnedLocally) {
            return clipboardText.clone();
        }
        if (!selectionOffer || selectionMime.empty() || !display) {
            return {};
        }

        int pipeFds[2]{-1, -1}; // keel-lint: ignore
        if (::pipe(pipeFds) != 0) {
            return {};
        }
        const int flags = ::fcntl(pipeFds[0], F_GETFL, 0); // keel-lint: ignore
        if (flags >= 0) {
            (void)::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
        }

        wl_argument recvArgs[2]{};
        recvArgs[0].s = selectionMime.c_str();
        recvArgs[1].h = pipeFds[1];
        wl.MarshalVoid(selectionOffer, kWlDataOfferReceive, recvArgs);
        ::close(pipeFds[1]);
        (void)wl.DisplayFlush(display);

        keel::String result{};
        const keel::u64 deadlineMs = keel::monotonic_ms() + 150u;
        char buf[1024]{};
        for (;;) {
            const keel::u64 now = keel::monotonic_ms();
            if (now >= deadlineMs) break;
            struct pollfd pfd{};
            pfd.fd = pipeFds[0];
            pfd.events = POLLIN;
            const keel::i32 rc = ::poll(&pfd, 1, keel::as<keel::i32>(deadlineMs - now));
            if (rc <= 0) break;
            const keel::isize n = ::read(pipeFds[0], buf, sizeof(buf));
            if (n > 0) {
                result.append(keel::StringRef(buf, keel::as<keel::usize>(n)));
                if (result.size() >= 1024u * 1024u) break;
            } else if (n == 0) {
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }
        ::close(pipeFds[0]);
        return result;
    }

    // --- Static Wayland callback handlers ---

    static void OnRegistryGlobal(void* data, wl_registry* reg, keel::u32 name, const char* iface, keel::u32 version) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !iface) return;
        const keel::StringRef s(iface);

        auto bindGlobal = [&](const wl_interface* wlIface, keel::u32 maxVer) -> void* {
            const keel::u32 v = keel::min(version, maxVer);
            wl_argument args[4]{};
            args[0].u = name;
            args[1].s = wlIface->name;
            args[2].u = v;
            args[3].o = nullptr;
            return self->wl.Marshal(reg, kWlRegistryBind, wlIface, v, 0u, args);
        };

        if (s == "wl_compositor" && !self->compositor) {
            self->compositor = static_cast<wl_compositor*>(bindGlobal(self->wl.ifaceCompositor, 6u));
        } else if (s == "wl_shm" && !self->shm && self->wl.ifaceShm) {
            self->shm = static_cast<wl_shm*>(bindGlobal(self->wl.ifaceShm, 1u));
        } else if (s == "wl_seat" && !self->seat) {
            self->seat = static_cast<wl_seat*>(bindGlobal(self->wl.ifaceSeat, 8u));
            if (self->seat) {
                self->wl.AddListener(self->seat, &kSeatListener, self);
            }
        } else if (s == "wl_output") {
            auto* out = static_cast<wl_output*>(bindGlobal(self->wl.ifaceOutput, 4u));
            if (out) {
                OutputInfo info{};
                info.proxy = out;
                info.globalName = name;
                self->outputs.push_back(info);
                self->wl.AddListener(out, &kOutputListener, self);
            }
        } else if (s == "wl_data_device_manager" && !self->dataDeviceMgr && self->wl.ifaceDataDeviceManager) {
            self->dataDeviceMgr = static_cast<wl_data_device_manager*>(bindGlobal(self->wl.ifaceDataDeviceManager, 3u));
        } else if (s == "xdg_wm_base" && !self->xdgWmBase) {
            self->xdgWmBase = static_cast<xdg_wm_base*>(bindGlobal(&self->ext.xdgWmBase, 6u));
            if (self->xdgWmBase) {
                self->wl.AddListener(self->xdgWmBase, &kXdgWmBaseListener, self);
            }
        } else if (s == "zxdg_decoration_manager_v1" && !self->decoMgr) {
            self->decoMgr = static_cast<zxdg_decoration_manager_v1*>(bindGlobal(&self->ext.decoManager, 1u));
        } else if (s == "wp_viewporter" && !self->viewporter) {
            self->viewporter = static_cast<wp_viewporter*>(bindGlobal(&self->ext.viewporter, 1u));
        } else if (s == "wp_fractional_scale_manager_v1" && !self->fracScaleMgr) {
            self->fracScaleMgr = static_cast<wp_fractional_scale_manager_v1*>(bindGlobal(&self->ext.fracScaleManager, 1u));
        } else if (s == "wp_cursor_shape_manager_v1" && !self->cursorShapeMgr) {
            self->cursorShapeMgr = static_cast<wp_cursor_shape_manager_v1*>(bindGlobal(&self->ext.curShapeManager, 1u));
        }
    }

    static void OnRegistryGlobalRemove(void* data, wl_registry*, keel::u32 name) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        for (keel::usize i = 0u; i < self->outputs.size(); ++i) {
            if (self->outputs[i].globalName == name) {
                if (self->outputs[i].proxy) {
                    if (self->wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(self->outputs[i].proxy)) >= 3u) {
                        self->wl.MarshalDestroy(self->outputs[i].proxy, kWlOutputRelease);
                    } else {
                        self->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(self->outputs[i].proxy));
                    }
                }
                self->outputs.erase(i);
                self->RefreshFallbackBufferScale();
                break;
            }
        }
    }

    static void OnFrameCallbackDone(void* data, wl_callback* cb, keel::u32) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (cb == self->frameCallback) {
            self->frameCallback = nullptr;
            self->frameCallbackPending = false;
        }
        if (cb) {
            self->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(cb));
        }
    }

    static void OnSurfaceEnter(void* data, wl_surface*, wl_output* output) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        for (OutputInfo& o : self->outputs) {
            if (o.proxy == output) {
                o.surfaceEntered = true;
                break;
            }
        }
        self->RefreshFallbackBufferScale();
    }

    static void OnSurfaceLeave(void* data, wl_surface*, wl_output* output) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        for (OutputInfo& o : self->outputs) {
            if (o.proxy == output) {
                o.surfaceEntered = false;
                break;
            }
        }
        self->RefreshFallbackBufferScale();
    }

    static void OnSurfacePreferredBufferScale(void* data, wl_surface*, keel::i32 factor) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->highDpi || factor <= 0) return;
        if (factor != self->preferredBufferScale) {
            self->preferredBufferScale = factor;
            self->scaleDirty = true;
        }
    }

    static void OnSurfacePreferredBufferTransform(void*, wl_surface*, keel::u32) {}

    static void OnSeatCapabilities(void* data, wl_seat* seat, keel::u32 caps) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;

        const bool hasPtr = (caps & kWlSeatCapPointer) != 0u;
        if (hasPtr && !self->pointer) {
            wl_argument arg[1]{};
            self->pointer = self->wl.MarshalNew<wl_pointer>(seat, kWlSeatGetPointer, self->wl.ifacePointer, arg);
            if (self->pointer) {
                self->wl.AddListener(self->pointer, &kPointerListener, self);
                if (self->cursorShapeMgr && !self->cursorShapeDev) {
                    wl_argument csArgs[2]{};
                    csArgs[0].o = nullptr;
                    csArgs[1].o = reinterpret_cast<wl_object*>(self->pointer);
                    self->cursorShapeDev = self->wl.MarshalNew<wp_cursor_shape_device_v1>(
                        self->cursorShapeMgr,
                        kWpCursorShapeMgrGetPointer,
                        &self->ext.curShapeDevice,
                        csArgs
                    );
                }
            }
        } else if (!hasPtr && self->pointer) {
            if (self->cursorShapeDev) {
                self->wl.MarshalDestroy(self->cursorShapeDev, kWpCursorShapeDevDestroy);
                self->cursorShapeDev = nullptr;
            }
            if (self->wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(self->pointer)) >= 3u) {
                self->wl.MarshalDestroy(self->pointer, kWlPointerRelease);
            } else {
                self->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(self->pointer));
            }
            self->pointer = nullptr;
        }

        const bool hasKbd = (caps & kWlSeatCapKeyboard) != 0u;
        if (hasKbd && !self->keyboard) {
            wl_argument arg[1]{};
            self->keyboard = self->wl.MarshalNew<wl_keyboard>(seat, kWlSeatGetKeyboard, self->wl.ifaceKeyboard, arg);
            if (self->keyboard) {
                self->wl.AddListener(self->keyboard, &kKeyboardListener, self);
            }
        } else if (!hasKbd && self->keyboard) {
            if (self->wl.ProxyGetVersion(reinterpret_cast<wl_proxy*>(self->keyboard)) >= 3u) {
                self->wl.MarshalDestroy(self->keyboard, kWlKeyboardRelease);
            } else {
                self->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(self->keyboard));
            }
            self->keyboard = nullptr;
        }
    }

    static void OnSeatName(void*, wl_seat*, const char*) {}

    static void OnPointerEnter(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf, WlFixed sx, WlFixed sy) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastPointerEnterSerial = serial;
        self->lastPointerSerial = serial;
        self->lastInputSerial = serial;
        self->pointerOnSurface = (surf == self->surface);
        if (!self->pointerOnSurface) return;
        self->pointerLogicalPos = {WlFixedToFloat(sx), WlFixedToFloat(sy)};
        self->ApplyCurrentCursor();
        if (self->activeUi) {
            self->activeUi->OnMouseMove({self->pointerLogicalPos.x * self->dpiScale, self->pointerLogicalPos.y * self->dpiScale});
        }
    }

    static void OnPointerLeave(void* data, wl_pointer*, keel::u32 serial, wl_surface* surf) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastPointerSerial = serial;
        if (surf == self->surface) {
            self->pointerOnSurface = false;
            self->resizeCursorOverride = false;
        }
    }

    static void OnPointerMotion(void* data, wl_pointer*, keel::u32, WlFixed sx, WlFixed sy) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->pointerOnSurface) return;
        self->pointerLogicalPos = {WlFixedToFloat(sx), WlFixedToFloat(sy)};
        if (self->customTitleBar && self->callbacks.onHitTest) {
            const WindowHitRegion reg = self->callbacks.onHitTest(self->pointerLogicalPos);
            keel::u32 edge = 0u;
            keel::u32 shape = kWpCursorShapeDefault;
            CursorType fallbackCur = CursorType::Default;
            if (MapHitRegionToWaylandResize(reg, edge, shape, fallbackCur)) {
                const bool changed = !self->resizeCursorOverride || self->resizeOverrideShape != shape;
                self->resizeCursorOverride = true;
                self->resizeOverrideShape = shape;
                self->resizeOverrideFallback = fallbackCur;
                if (changed) {
                    self->ApplyCurrentCursor();
                }
            } else if (self->resizeCursorOverride) {
                self->resizeCursorOverride = false;
                self->ApplyCurrentCursor();
            }
        }
        if (self->activeUi) {
            self->activeUi->OnMouseMove({self->pointerLogicalPos.x * self->dpiScale, self->pointerLogicalPos.y * self->dpiScale});
        }
    }

    static void OnPointerButton(void* data, wl_pointer*, keel::u32 serial, keel::u32, keel::u32 button, keel::u32 state) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->pointerOnSurface) return;
        self->lastPointerSerial = serial;
        self->lastInputSerial = serial;

        if (button == kBtnLeft && state == kWlPointerButtonPressed && self->customTitleBar && self->callbacks.onHitTest) {
            const WindowHitRegion reg = self->callbacks.onHitTest(self->pointerLogicalPos);
            keel::u32 edge = 0u;
            keel::u32 shape = 0u;
            CursorType fallbackCur = CursorType::Default;
            if (MapHitRegionToWaylandResize(reg, edge, shape, fallbackCur)) {
                if (self->xdgToplevel && self->seat) {
                    wl_argument args[3]{};
                    args[0].o = reinterpret_cast<wl_object*>(self->seat);
                    args[1].u = serial;
                    args[2].u = edge;
                    self->wl.MarshalVoid(self->xdgToplevel, kXdgToplevelResize, args);
                    (void)self->wl.DisplayFlush(self->display);
                }
                return;
            }
        }

        if (!self->activeUi) return;
        self->activeUi->OnMouseMove({self->pointerLogicalPos.x * self->dpiScale, self->pointerLogicalPos.y * self->dpiScale});
        keel::i32 uiBtn = 0;
        if (button == kBtnLeft) uiBtn = 1;
        else if (button == kBtnMiddle) uiBtn = 2;
        else if (button == kBtnRight) uiBtn = 3;
        if (uiBtn == 0) return;
        if (state == kWlPointerButtonPressed) {
            self->activeUi->OnMouseDown(uiBtn);
        } else if (state == kWlPointerButtonReleased) {
            self->activeUi->OnMouseUp(uiBtn);
        }
    }

    static void OnPointerAxis(void* data, wl_pointer*, keel::u32, keel::u32 axis, WlFixed value) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->pointerOnSurface) return;
        const keel::f32 v = WlFixedToFloat(value);
        if (axis == kWlPointerAxisVertical) {
            if (!self->hasDiscreteWheelY) {
                self->pendingWheelY -= v / 15.0f;
            }
        } else if (axis == kWlPointerAxisHorizontal) {
            if (!self->hasDiscreteWheelX) {
                self->pendingWheelX -= v / 15.0f;
            }
        }
    }

    static void OnPointerFrame(void* data, wl_pointer*) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (self->activeUi && (keel::abs(self->pendingWheelX) > 1e-4f || keel::abs(self->pendingWheelY) > 1e-4f)) {
            self->activeUi->OnMouseWheel(self->pendingWheelX, self->pendingWheelY);
        }
        self->pendingWheelX = 0.0f;
        self->pendingWheelY = 0.0f;
        self->hasDiscreteWheelX = false;
        self->hasDiscreteWheelY = false;
    }

    static void OnPointerAxisSource(void*, wl_pointer*, keel::u32) {}
    static void OnPointerAxisStop(void*, wl_pointer*, keel::u32, keel::u32) {}

    static void OnPointerAxisDiscrete(void* data, wl_pointer*, keel::u32 axis, keel::i32 discrete) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->pointerOnSurface) return;
        if (axis == kWlPointerAxisVertical) {
            if (!self->hasDiscreteWheelY) {
                self->pendingWheelY = 0.0f;
                self->hasDiscreteWheelY = true;
            }
            self->pendingWheelY -= keel::as<keel::f32>(discrete);
        } else if (axis == kWlPointerAxisHorizontal) {
            if (!self->hasDiscreteWheelX) {
                self->pendingWheelX = 0.0f;
                self->hasDiscreteWheelX = true;
            }
            self->pendingWheelX -= keel::as<keel::f32>(discrete);
        }
    }

    static void OnPointerAxisValue120(void* data, wl_pointer*, keel::u32 axis, keel::i32 value120) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->pointerOnSurface) return;
        if (axis == kWlPointerAxisVertical) {
            if (!self->hasDiscreteWheelY) {
                self->pendingWheelY = 0.0f;
                self->hasDiscreteWheelY = true;
            }
            self->pendingWheelY -= keel::as<keel::f32>(value120) / 120.0f;
        } else if (axis == kWlPointerAxisHorizontal) {
            if (!self->hasDiscreteWheelX) {
                self->pendingWheelX = 0.0f;
                self->hasDiscreteWheelX = true;
            }
            self->pendingWheelX -= keel::as<keel::f32>(value120) / 120.0f;
        }
    }

    static void OnPointerAxisRelativeDirection(void*, wl_pointer*, keel::u32, keel::u32) {}

    static void OnKeyboardKeymap(void* data, wl_keyboard*, keel::u32 format, keel::i32 fd, keel::u32 size) {
        auto* self = static_cast<Impl*>(data);
        if (!self) {
            if (fd >= 0) ::close(fd);
            return;
        }
        if (format != kWlKeyboardKeymapXkbV1 || fd < 0 || size == 0u || !self->xkbCtx) {
            if (fd >= 0) ::close(fd);
            return;
        }
        void* mapped = ::mmap(nullptr, keel::as<keel::usize>(size), PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED || !mapped) {
            return;
        }

        xkb_keymap* km = self->xkb.KeymapNewFromString(
            self->xkbCtx,
            static_cast<const char*>(mapped),
            XkbApi::kKeymapFormatTextV1,
            XkbApi::kKeymapCompileNoFlags
        );
        (void)::munmap(mapped, keel::as<keel::usize>(size));
        if (!km) return;

        xkb_state* st = self->xkb.StateNew(km);
        if (!st) {
            self->xkb.KeymapUnref(km);
            return;
        }
        if (self->xkbState) self->xkb.StateUnref(self->xkbState);
        if (self->xkbKeymap) self->xkb.KeymapUnref(self->xkbKeymap);
        self->xkbKeymap = km;
        self->xkbState = st;
    }

    static void OnKeyboardEnter(void* data, wl_keyboard*, keel::u32 serial, wl_surface*, wl_array*) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastInputSerial = serial;
        self->focused = true;
        if (self->activeUi) {
            self->activeUi->InvalidateAll();
            self->activeUi->RequestRepaint(2);
        }
    }

    static void OnKeyboardLeave(void* data, wl_keyboard*, keel::u32 serial, wl_surface*) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastInputSerial = serial;
        self->repeatingKeycode = 0u;
        self->keyMods = {};
        if (self->activeUi) {
            self->activeUi->SetKeyModifiers({});
            self->activeUi->InvalidateAll();
            self->activeUi->RequestRepaint(2);
        }
    }

    static void OnKeyboardKey(void* data, wl_keyboard*, keel::u32 serial, keel::u32, keel::u32 key, keel::u32 state) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastInputSerial = serial;
        const keel::u32 xkbKeycode = key + 8u;
        if (state == kWlKeyboardKeyPressed) {
            if (self->activeUi) {
                self->DispatchXkbKey(xkbKeycode, *self->activeUi);
            }
            if (self->repeatRate > 0 && self->xkbKeymap &&
                (!self->xkb.KeymapKeyRepeats || self->xkb.KeymapKeyRepeats(self->xkbKeymap, xkbKeycode) != 0)) {
                self->repeatingKeycode = xkbKeycode;
                self->nextRepeatMs = keel::monotonic_ms() + keel::as<keel::u64>(keel::max(100, self->repeatDelayMs));
            }
        } else if (state == kWlKeyboardKeyReleased) {
            if (self->repeatingKeycode == xkbKeycode) {
                self->repeatingKeycode = 0u;
            }
        }
    }

    static void OnKeyboardModifiers(
        void* data,
        wl_keyboard*,
        keel::u32 serial,
        keel::u32 depressed,
        keel::u32 latched,
        keel::u32 locked,
        keel::u32 group
    ) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->lastInputSerial = serial;
        if (self->xkbState) {
            (void)self->xkb.StateUpdateMask(self->xkbState, depressed, latched, locked, 0u, 0u, group);
            self->keyMods.shift = self->xkb.StateModNameIsActive(self->xkbState, "Shift", XkbApi::kStateModsEffective) > 0;
            self->keyMods.ctrl = self->xkb.StateModNameIsActive(self->xkbState, "Control", XkbApi::kStateModsEffective) > 0;
            self->keyMods.alt = self->xkb.StateModNameIsActive(self->xkbState, "Mod1", XkbApi::kStateModsEffective) > 0;
            if (self->activeUi) {
                self->activeUi->SetKeyModifiers(self->keyMods);
            }
        }
    }

    static void OnKeyboardRepeatInfo(void* data, wl_keyboard*, keel::i32 rate, keel::i32 delay) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->repeatRate = keel::max(0, rate);
        self->repeatDelayMs = keel::max(50, delay);
    }

    static void OnOutputGeometry(
        void* data,
        wl_output* output,
        keel::i32,
        keel::i32,
        keel::i32 physicalWidth,
        keel::i32,
        keel::i32,
        const char*,
        const char*,
        keel::i32
    ) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        for (OutputInfo& o : self->outputs) {
            if (o.proxy == output) {
                o.physicalWidthMm = physicalWidth;
                break;
            }
        }
    }

    static void OnOutputMode(void* data, wl_output* output, keel::u32 flags, keel::i32 width, keel::i32, keel::i32) {
        auto* self = static_cast<Impl*>(data);
        if (!self || (flags & kWlOutputModeCurrent) == 0u) return;
        for (OutputInfo& o : self->outputs) {
            if (o.proxy == output) {
                o.modeWidthPx = width;
                break;
            }
        }
    }

    static void OnOutputDone(void* data, wl_output*) {
        auto* self = static_cast<Impl*>(data);
        if (self) {
            self->RefreshFallbackBufferScale();
        }
    }

    static void OnOutputScale(void* data, wl_output* output, keel::i32 factor) {
        auto* self = static_cast<Impl*>(data);
        if (!self || factor <= 0) return;
        for (OutputInfo& o : self->outputs) {
            if (o.proxy == output) {
                o.scale = factor;
                break;
            }
        }
        self->RefreshFallbackBufferScale();
    }

    static void OnOutputName(void*, wl_output*, const char*) {}
    static void OnOutputDescription(void*, wl_output*, const char*) {}

    static void OnDataDeviceOffer(void* data, wl_data_device*, wl_data_offer* offer) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !offer) return;
        DataOfferEntry entry{};
        entry.offer = offer;
        self->pendingOffers.push_back(entry);
        self->wl.AddListener(offer, &kDataOfferListener, self);
    }

    static void OnDataDeviceEnter(void*, wl_data_device*, keel::u32, wl_surface*, WlFixed, WlFixed, wl_data_offer*) {}
    static void OnDataDeviceLeave(void*, wl_data_device*) {}
    static void OnDataDeviceMotion(void*, wl_data_device*, keel::u32, WlFixed, WlFixed) {}
    static void OnDataDeviceDrop(void*, wl_data_device*) {}

    static void OnDataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (self->selectionOffer && self->selectionOffer != offer) {
            self->DestroyOffer(self->selectionOffer);
        }
        self->selectionOffer = offer;
        self->selectionMime.clear();

        if (offer) {
            if (!self->ourDataSource) {
                self->clipboardOwnedLocally = false;
            }
            for (keel::usize i = 0u; i < self->pendingOffers.size(); ++i) {
                if (self->pendingOffers[i].offer == offer) {
                    const DataOfferEntry& e = self->pendingOffers[i];
                    if (e.hasUtf8Mime) self->selectionMime = keel::String("text/plain;charset=utf-8");
                    else if (e.hasUtf8String) self->selectionMime = keel::String("UTF8_STRING");
                    else if (e.hasPlainText) self->selectionMime = keel::String("text/plain");
                    self->pendingOffers.erase(i);
                    break;
                }
            }
        }
        self->ClearPendingOffers();
    }

    static void OnDataSourceTarget(void*, wl_data_source*, const char*) {}

    static void OnDataSourceSend(void* data, wl_data_source*, const char*, keel::i32 fd) {
        auto* self = static_cast<Impl*>(data);
        if (fd < 0) return;
        if (self && !self->clipboardText.empty()) {
            const char* ptr = self->clipboardText.c_str();
            keel::usize remaining = self->clipboardText.size();
            while (remaining > 0u) {
                const keel::isize written = ::write(fd, ptr, remaining);
                if (written <= 0) {
                    if (written < 0 && errno == EINTR) continue;
                    break;
                }
                ptr += written;
                remaining -= keel::as<keel::usize>(written);
            }
        }
        ::close(fd);
    }

    static void OnDataSourceCancelled(void* data, wl_data_source* src) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (src == self->ourDataSource) {
            self->wl.MarshalDestroy(self->ourDataSource, kWlDataSourceDestroy);
            self->ourDataSource = nullptr;
            self->clipboardOwnedLocally = false;
        }
    }

    static void OnDataSourceDndDrop(void*, wl_data_source*) {}
    static void OnDataSourceDndFinished(void*, wl_data_source*) {}
    static void OnDataSourceAction(void*, wl_data_source*, keel::u32) {}

    static void OnDataOfferMime(void* data, wl_data_offer* offer, const char* mime) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !offer || !mime) return;
        const keel::StringRef m(mime);
        for (DataOfferEntry& e : self->pendingOffers) {
            if (e.offer == offer) {
                if (m == "text/plain;charset=utf-8") e.hasUtf8Mime = true;
                else if (m == "UTF8_STRING") e.hasUtf8String = true;
                else if (m == "text/plain") e.hasPlainText = true;
                break;
            }
        }
    }

    static void OnDataOfferSourceActions(void*, wl_data_offer*, keel::u32) {}
    static void OnDataOfferAction(void*, wl_data_offer*, keel::u32) {}

    static void OnXdgWmBasePing(void* data, xdg_wm_base* base, keel::u32 serial) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !base) return;
        wl_argument arg[1]{};
        arg[0].u = serial;
        self->wl.MarshalVoid(base, kXdgWmBasePong, arg);
    }

    static void OnXdgSurfaceConfigure(void* data, xdg_surface*, keel::u32 serial) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->pendingConfigureSerial = serial;
        self->hasPendingConfigure = true;
        self->initialConfigureDone = true;
    }

    static void OnXdgToplevelConfigure(void* data, xdg_toplevel*, keel::i32 w, keel::i32 h, wl_array* states) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (w > 0 && h > 0) {
            self->pendingLogicalW = w;
            self->pendingLogicalH = h;
        }
        bool isMax = false;
        bool isFs = false;
        bool isResizing = false;
        bool isActivated = false;
        bool isSuspended = false;
        if (states && states->data && states->size >= sizeof(keel::u32)) {
            const auto* vals = static_cast<const keel::u32*>(states->data);
            const keel::usize count = states->size / sizeof(keel::u32);
            for (keel::usize i = 0u; i < count; ++i) {
                switch (vals[i]) {
                    case kXdgToplevelStateMaximized:  isMax = true; break;
                    case kXdgToplevelStateFullscreen: isFs = true; break;
                    case kXdgToplevelStateResizing:   isResizing = true; break;
                    case kXdgToplevelStateActivated:  isActivated = true; break;
                    case kXdgToplevelStateSuspended:  isSuspended = true; break;
                    default: break;
                }
            }
        }
        self->pendingMaximized = isMax;
        self->pendingFullscreen = isFs;
        self->pendingResizing = isResizing;
        self->pendingFocused = isActivated;
        self->pendingSuspended = isSuspended;
    }

    static void OnXdgToplevelClose(void* data, xdg_toplevel*) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        if (!self->callbacks.onCloseRequested || self->callbacks.onCloseRequested()) {
            self->closeRequested = true;
        }
    }

    static void OnXdgToplevelConfigureBounds(void*, xdg_toplevel*, keel::i32, keel::i32) {}
    static void OnXdgToplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}

    static void OnDecoConfigure(void* data, zxdg_toplevel_decoration_v1*, keel::u32 mode) {
        auto* self = static_cast<Impl*>(data);
        if (!self) return;
        self->compositorSupportsSsd = true;
        if (!self->borderless && !self->fullscreen) {
            const bool wantCsd = (mode == kZxdgDecoModeClientSide);
            if (self->customTitleBar != wantCsd) {
                self->customTitleBar = wantCsd;
                if (self->activeUi) {
                    self->activeUi->InvalidateAll();
                    self->activeUi->RequestRepaint(2);
                }
            }
        }
    }

    static void OnFractionalScalePreferred(void* data, wp_fractional_scale_v1*, keel::u32 scale120) {
        auto* self = static_cast<Impl*>(data);
        if (!self || !self->highDpi || scale120 == 0u) return;
        if (self->fractionalScale120 != scale120) {
            self->fractionalScale120 = scale120;
            self->scaleDirty = true;
        }
    }
};

const wl_registry_listener WaylandPlatformWindow::Impl::kRegistryListener = {
    &Impl::OnRegistryGlobal,
    &Impl::OnRegistryGlobalRemove
};
const wl_callback_listener WaylandPlatformWindow::Impl::kFrameCallbackListener = {
    &Impl::OnFrameCallbackDone
};
const wl_surface_listener WaylandPlatformWindow::Impl::kSurfaceListener = {
    &Impl::OnSurfaceEnter,
    &Impl::OnSurfaceLeave,
    &Impl::OnSurfacePreferredBufferScale,
    &Impl::OnSurfacePreferredBufferTransform
};
const wl_seat_listener WaylandPlatformWindow::Impl::kSeatListener = {
    &Impl::OnSeatCapabilities,
    &Impl::OnSeatName
};
const wl_pointer_listener WaylandPlatformWindow::Impl::kPointerListener = {
    &Impl::OnPointerEnter,
    &Impl::OnPointerLeave,
    &Impl::OnPointerMotion,
    &Impl::OnPointerButton,
    &Impl::OnPointerAxis,
    &Impl::OnPointerFrame,
    &Impl::OnPointerAxisSource,
    &Impl::OnPointerAxisStop,
    &Impl::OnPointerAxisDiscrete,
    &Impl::OnPointerAxisValue120,
    &Impl::OnPointerAxisRelativeDirection
};
const wl_keyboard_listener WaylandPlatformWindow::Impl::kKeyboardListener = {
    &Impl::OnKeyboardKeymap,
    &Impl::OnKeyboardEnter,
    &Impl::OnKeyboardLeave,
    &Impl::OnKeyboardKey,
    &Impl::OnKeyboardModifiers,
    &Impl::OnKeyboardRepeatInfo
};
const wl_output_listener WaylandPlatformWindow::Impl::kOutputListener = {
    &Impl::OnOutputGeometry,
    &Impl::OnOutputMode,
    &Impl::OnOutputDone,
    &Impl::OnOutputScale,
    &Impl::OnOutputName,
    &Impl::OnOutputDescription
};
const wl_data_device_listener WaylandPlatformWindow::Impl::kDataDeviceListener = {
    &Impl::OnDataDeviceOffer,
    &Impl::OnDataDeviceEnter,
    &Impl::OnDataDeviceLeave,
    &Impl::OnDataDeviceMotion,
    &Impl::OnDataDeviceDrop,
    &Impl::OnDataDeviceSelection
};
const wl_data_source_listener WaylandPlatformWindow::Impl::kDataSourceListener = {
    &Impl::OnDataSourceTarget,
    &Impl::OnDataSourceSend,
    &Impl::OnDataSourceCancelled,
    &Impl::OnDataSourceDndDrop,
    &Impl::OnDataSourceDndFinished,
    &Impl::OnDataSourceAction
};
const wl_data_offer_listener WaylandPlatformWindow::Impl::kDataOfferListener = {
    &Impl::OnDataOfferMime,
    &Impl::OnDataOfferSourceActions,
    &Impl::OnDataOfferAction
};
const xdg_wm_base_listener WaylandPlatformWindow::Impl::kXdgWmBaseListener = {
    &Impl::OnXdgWmBasePing
};
const xdg_surface_listener WaylandPlatformWindow::Impl::kXdgSurfaceListener = {
    &Impl::OnXdgSurfaceConfigure
};
const xdg_toplevel_listener WaylandPlatformWindow::Impl::kXdgToplevelListener = {
    &Impl::OnXdgToplevelConfigure,
    &Impl::OnXdgToplevelClose,
    &Impl::OnXdgToplevelConfigureBounds,
    &Impl::OnXdgToplevelWmCapabilities
};
const zxdg_toplevel_decoration_v1_listener WaylandPlatformWindow::Impl::kDecoListener = {
    &Impl::OnDecoConfigure
};
const wp_fractional_scale_v1_listener WaylandPlatformWindow::Impl::kFracScaleListener = {
    &Impl::OnFractionalScalePreferred
};

WaylandPlatformWindow::WaylandPlatformWindow() noexcept
    : m_impl(keel::make_unique<Impl>()) {
}

WaylandPlatformWindow::~WaylandPlatformWindow() {
    Destroy();
}

bool WaylandPlatformWindow::IsRuntimeAvailable() noexcept {
    const auto wlDisp = keel::env_get("WAYLAND_DISPLAY");
    if (!wlDisp.has_value() || wlDisp->empty()) {
        return false;
    }
    WaylandClientApi wl;
    WaylandEglApi wlEgl;
    XkbApi xkb;
    return wl.Load() && wlEgl.Load() && xkb.Load() && linux_detail::LinuxEglContext::IsRuntimeAvailable();
}

bool WaylandPlatformWindow::Create(
    const PlatformWindowDesc& desc,
    Context& ui,
    WindowHostCallbacks callbacks
) {
    if (!m_impl) return false;
    m_impl->DestroyAll();

    if (!m_impl->wl.Load() || !m_impl->wlEgl.Load() || !m_impl->xkb.Load()) {
        keel::eprintln("[WaylandPlatformWindow] Failed to load Wayland/EGL/XKB shared libraries");
        return false;
    }
    m_impl->ext.Init(m_impl->wl);

    m_impl->xkbCtx = m_impl->xkb.ContextNew(XkbApi::kContextNoFlags);
    m_impl->display = m_impl->wl.DisplayConnect(nullptr);
    if (!m_impl->display) {
        keel::eprintln("[WaylandPlatformWindow] wl_display_connect(nullptr) failed");
        m_impl->DestroyAll();
        return false;
    }

    m_impl->logicalWidth = keel::max(desc.width, 64);
    m_impl->logicalHeight = keel::max(desc.height, 64);
    m_impl->fbWidth = m_impl->logicalWidth;
    m_impl->fbHeight = m_impl->logicalHeight;
    m_impl->minWidth = keel::max(desc.minWidth, 64);
    m_impl->minHeight = keel::max(desc.minHeight, 64);
    m_impl->resizable = desc.resizable;
    m_impl->borderless = desc.borderless;
    m_impl->highDpi = desc.highDpi;
    m_impl->vsync = desc.vsync;
    m_impl->customTitleBar = desc.customTitleBar && !desc.fullscreen;
    m_impl->maximized = desc.maximized;
    m_impl->fullscreen = desc.fullscreen;
    m_impl->closeRequested = false;
    m_impl->callbacks = keel::move(callbacks);
    m_impl->activeUi = &ui;

    // 1. Discover compositor globals
    wl_argument regArg[1]{};
    m_impl->registry = m_impl->wl.MarshalNew<wl_registry>(
        m_impl->display,
        kWlDisplayGetRegistry,
        m_impl->wl.ifaceRegistry,
        regArg,
        1u
    );
    if (!m_impl->registry) {
        m_impl->DestroyAll();
        return false;
    }
    m_impl->wl.AddListener(m_impl->registry, &Impl::kRegistryListener, m_impl.get());
    (void)m_impl->wl.DisplayRoundtrip(m_impl->display);
    // Second roundtrip receives wl_seat capabilities and wl_output geometry/mode/scale
    (void)m_impl->wl.DisplayRoundtrip(m_impl->display);

    if (!m_impl->compositor || !m_impl->xdgWmBase) {
        keel::eprintln("[WaylandPlatformWindow] Wayland compositor missing wl_compositor or xdg_wm_base");
        m_impl->DestroyAll();
        return false;
    }

    if (m_impl->dataDeviceMgr && m_impl->seat && m_impl->wl.ifaceDataDevice) {
        wl_argument ddArgs[2]{};
        ddArgs[0].o = nullptr;
        ddArgs[1].o = reinterpret_cast<wl_object*>(m_impl->seat);
        m_impl->dataDevice = m_impl->wl.MarshalNew<wl_data_device>(
            m_impl->dataDeviceMgr,
            kWlDataDevMgrGetDataDevice,
            m_impl->wl.ifaceDataDevice,
            ddArgs
        );
        if (m_impl->dataDevice) {
            m_impl->wl.AddListener(m_impl->dataDevice, &Impl::kDataDeviceListener, m_impl.get());
        }
    }

    // 2. Create wl_surface + xdg_surface + xdg_toplevel + optional HiDPI & decoration protocols
    wl_argument surfArg[1]{};
    m_impl->surface = m_impl->wl.MarshalNew<wl_surface>(
        m_impl->compositor,
        kWlCompositorCreateSurface,
        m_impl->wl.ifaceSurface,
        surfArg
    );
    if (!m_impl->surface) {
        m_impl->DestroyAll();
        return false;
    }
    m_impl->wl.AddListener(m_impl->surface, &Impl::kSurfaceListener, m_impl.get());

    if (m_impl->highDpi && m_impl->viewporter && m_impl->fracScaleMgr) {
        wl_argument vpArgs[2]{};
        vpArgs[0].o = nullptr;
        vpArgs[1].o = reinterpret_cast<wl_object*>(m_impl->surface);
        m_impl->viewport = m_impl->wl.MarshalNew<wp_viewport>(
            m_impl->viewporter,
            kWpViewporterGetViewport,
            &m_impl->ext.viewport,
            vpArgs
        );

        wl_argument fsArgs[2]{};
        fsArgs[0].o = nullptr;
        fsArgs[1].o = reinterpret_cast<wl_object*>(m_impl->surface);
        m_impl->fracScale = m_impl->wl.MarshalNew<wp_fractional_scale_v1>(
            m_impl->fracScaleMgr,
            kWpFracScaleMgrGetScale,
            &m_impl->ext.fracScale,
            fsArgs
        );
        if (m_impl->fracScale) {
            m_impl->wl.AddListener(m_impl->fracScale, &Impl::kFracScaleListener, m_impl.get());
        }
    }

    wl_argument xsArgs[2]{};
    xsArgs[0].o = nullptr;
    xsArgs[1].o = reinterpret_cast<wl_object*>(m_impl->surface);
    m_impl->xdgSurface = m_impl->wl.MarshalNew<xdg_surface>(
        m_impl->xdgWmBase,
        kXdgWmBaseGetXdgSurface,
        &m_impl->ext.xdgSurface,
        xsArgs
    );
    if (!m_impl->xdgSurface) {
        m_impl->DestroyAll();
        return false;
    }
    m_impl->wl.AddListener(m_impl->xdgSurface, &Impl::kXdgSurfaceListener, m_impl.get());

    wl_argument topArg[1]{};
    m_impl->xdgToplevel = m_impl->wl.MarshalNew<xdg_toplevel>(
        m_impl->xdgSurface,
        kXdgSurfaceGetToplevel,
        &m_impl->ext.xdgToplevel,
        topArg
    );
    if (!m_impl->xdgToplevel) {
        m_impl->DestroyAll();
        return false;
    }
    m_impl->wl.AddListener(m_impl->xdgToplevel, &Impl::kXdgToplevelListener, m_impl.get());

    // Configure title, app_id, size constraints, and decorations
    const keel::String titleStr = keel::String::from(desc.title.as_ref());
    {
        wl_argument tArg[1]{};
        tArg[0].s = titleStr.c_str();
        m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetTitle, tArg);

        wl_argument idArg[1]{};
        idArg[0].s = "io.guikit.native";
        m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetAppId, idArg);

        wl_argument minArgs[2]{};
        minArgs[0].i = m_impl->minWidth;
        minArgs[1].i = m_impl->minHeight;
        m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetMinSize, minArgs);

        if (!m_impl->resizable) {
            wl_argument maxArgs[2]{};
            maxArgs[0].i = m_impl->logicalWidth;
            maxArgs[1].i = m_impl->logicalHeight;
            m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetMaxSize, maxArgs);
        }
    }

    if (m_impl->decoMgr) {
        wl_argument dArgs[2]{};
        dArgs[0].o = nullptr;
        dArgs[1].o = reinterpret_cast<wl_object*>(m_impl->xdgToplevel);
        m_impl->toplevelDeco = m_impl->wl.MarshalNew<zxdg_toplevel_decoration_v1>(
            m_impl->decoMgr,
            kZxdgDecoMgrGetToplevelDeco,
            &m_impl->ext.decoToplevel,
            dArgs
        );
        if (m_impl->toplevelDeco) {
            m_impl->compositorSupportsSsd = true;
            m_impl->wl.AddListener(m_impl->toplevelDeco, &Impl::kDecoListener, m_impl.get());
            wl_argument mArg[1]{};
            mArg[0].u = (m_impl->customTitleBar || m_impl->borderless)
                ? kZxdgDecoModeClientSide
                : kZxdgDecoModeServerSide;
            m_impl->wl.MarshalVoid(m_impl->toplevelDeco, kZxdgToplevelDecoSetMode, mArg);
        }
    } else if (!m_impl->borderless && !m_impl->fullscreen) {
        // Compositor (e.g. GNOME Mutter) is CSD-only: enable GuiKit HeaderBar automatically if hosted by MainWindow
        m_impl->customTitleBar = true;
    }

    if (desc.fullscreen) {
        wl_argument fsArg[1]{};
        fsArg[0].o = nullptr;
        m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetFullscreen, fsArg);
    } else if (desc.maximized) {
        m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetMaximized);
    }

    // 3. Initial bufferless commit to receive first xdg_surface.configure & preferred fractional scale
    m_impl->wl.MarshalVoid(m_impl->surface, kWlSurfaceCommit);
    (void)m_impl->wl.DisplayRoundtrip(m_impl->display);
    if (!m_impl->initialConfigureDone) {
        (void)m_impl->wl.DisplayRoundtrip(m_impl->display);
    }

    // Apply initial configure & HiDPI scale before creating the EGL window surface
    m_impl->ApplyPendingSurfaceState(&ui, /*duringInit=*/true);

    // 4. Create wl_egl_window + EGL context (swapInterval = 0; VSync is paced via wl_surface.frame callbacks)
    if (desc.graphicsApi != GraphicsApi::None) {
        if (!m_impl->egl.InitDisplayAndConfig(
                linux_detail::LinuxEglContext::kPlatformWaylandKhr,
                m_impl->display,
                desc.graphicsApi,
                /*alphaBits=*/0
            )) {
            m_impl->DestroyAll();
            return false;
        }
        m_impl->eglWindow = m_impl->wlEgl.Create(m_impl->surface, m_impl->fbWidth, m_impl->fbHeight);
        if (!m_impl->eglWindow) {
            keel::eprintln("[WaylandPlatformWindow] wl_egl_window_create() failed");
            m_impl->DestroyAll();
            return false;
        }
        if (!m_impl->egl.CreateSurfaceAndContext(reinterpret_cast<keel::usize>(m_impl->eglWindow), /*swapInterval=*/0)) {
            m_impl->DestroyAll();
            return false;
        }
    }

    m_impl->wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    Impl* rawImpl = m_impl.get();
    ui.SetClipboardCallbacks(
        [rawImpl]() -> keel::String { return rawImpl->GetSystemClipboard(); },
        [rawImpl](keel::StringRef txt) { rawImpl->SetSystemClipboard(txt); }
    );

    m_impl->alive = true;
    return true;
}

void WaylandPlatformWindow::Destroy() noexcept {
    if (m_impl) {
        m_impl->DestroyAll();
    }
}

bool WaylandPlatformWindow::PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) {
    if (!m_impl || !m_impl->alive || !m_impl->display) {
        return false;
    }
    m_impl->activeUi = &ui;

    if (m_impl->wl.DisplayGetError && m_impl->wl.DisplayGetError(m_impl->display) != 0) {
        m_impl->closeRequested = true;
        return false;
    }

    bool hadEvents = false;
    if (m_impl->wl.DisplayDispatchPending(m_impl->display) > 0) {
        hadEvents = true;
    }
    (void)m_impl->wl.DisplayFlush(m_impl->display);

    const keel::i32 wlFd = m_impl->wl.DisplayGetFd(m_impl->display);

    // Non-blocking drain of any already-buffered socket events
    while (m_impl->wl.DisplayPrepareRead(m_impl->display) != 0) {
        if (m_impl->wl.DisplayDispatchPending(m_impl->display) > 0) {
            hadEvents = true;
        }
    }
    (void)m_impl->wl.DisplayFlush(m_impl->display);
    {
        struct pollfd pfd{};
        pfd.fd = wlFd;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN) != 0) {
            if (m_impl->wl.DisplayReadEvents(m_impl->display) == 0) {
                if (m_impl->wl.DisplayDispatchPending(m_impl->display) > 0) {
                    hadEvents = true;
                }
            } else {
                m_impl->closeRequested = true;
                return false;
            }
        } else {
            m_impl->wl.DisplayCancelRead(m_impl->display);
        }
    }

    if (m_impl->closeRequested) {
        return false;
    }

    // Client-side keyboard auto-repeat
    const keel::u64 nowMs = keel::monotonic_ms();
    if (m_impl->repeatingKeycode != 0u && m_impl->repeatRate > 0 && nowMs >= m_impl->nextRepeatMs) {
        m_impl->DispatchXkbKey(m_impl->repeatingKeycode, ui);
        const keel::u64 intervalMs = keel::max<keel::u64>(10u, 1000u / keel::as<keel::u64>(m_impl->repeatRate));
        m_impl->nextRepeatMs = nowMs + intervalMs;
        hadEvents = true;
    }

    // Check if frame callback watchdog expired (e.g. compositor paused callbacks while occluded)
    const bool frameWaitTimedOut = m_impl->frameCallbackPending &&
        (nowMs >= m_impl->frameSubmittedMs + kFrameCallbackWatchdogMs);
    if (frameWaitTimedOut) {
        m_impl->frameCallbackPending = false;
        if (m_impl->frameCallback) {
            m_impl->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(m_impl->frameCallback));
            m_impl->frameCallback = nullptr;
        }
    }

    const bool waitingForVBlank = m_impl->vsync && m_impl->frameCallbackPending;
    const bool hasWorkPending = ui.NeedsRepaint() || m_impl->hasPendingConfigure || m_impl->scaleDirty;
    const bool shouldSleep =
        sleepTimeoutMs != 0 &&
        (waitingForVBlank ||
         (!hadEvents && (m_impl->suspended || (reactiveMode && !hasWorkPending))));

    if (shouldSleep) {
        keel::i32 timeout = (sleepTimeoutMs < 0) ? -1 : sleepTimeoutMs;
        if (waitingForVBlank) {
            const keel::u64 rem = (m_impl->frameSubmittedMs + kFrameCallbackWatchdogMs > nowMs)
                ? (m_impl->frameSubmittedMs + kFrameCallbackWatchdogMs - nowMs)
                : 1u;
            const keel::i32 remMs = keel::as<keel::i32>(rem);
            timeout = (timeout < 0) ? remMs : keel::min(timeout, remMs);
        }
        if (m_impl->repeatingKeycode != 0u && m_impl->repeatRate > 0) {
            const keel::u64 rem = (m_impl->nextRepeatMs > nowMs) ? (m_impl->nextRepeatMs - nowMs) : 1u;
            const keel::i32 remMs = keel::as<keel::i32>(rem);
            timeout = (timeout < 0) ? remMs : keel::min(timeout, remMs);
        }

        while (m_impl->wl.DisplayPrepareRead(m_impl->display) != 0) {
            (void)m_impl->wl.DisplayDispatchPending(m_impl->display);
        }
        (void)m_impl->wl.DisplayFlush(m_impl->display);

        struct pollfd fds[2]{};
        fds[0].fd = wlFd;
        fds[0].events = POLLIN;
        nfds_t nfds = 1;
        if (m_impl->wakeFd >= 0) {
            fds[1].fd = m_impl->wakeFd;
            fds[1].events = POLLIN;
            nfds = 2;
        }

        const keel::i32 ready = ::poll(fds, nfds, timeout);
        if (ready > 0) {
            if ((fds[0].revents & (POLLERR | POLLHUP)) != 0) {
                m_impl->wl.DisplayCancelRead(m_impl->display);
                m_impl->closeRequested = true;
                return false;
            }
            if ((fds[0].revents & POLLIN) != 0) {
                if (m_impl->wl.DisplayReadEvents(m_impl->display) == 0) {
                    (void)m_impl->wl.DisplayDispatchPending(m_impl->display);
                } else {
                    m_impl->closeRequested = true;
                    return false;
                }
            } else {
                m_impl->wl.DisplayCancelRead(m_impl->display);
            }
            if (nfds == 2 && (fds[1].revents & POLLIN) != 0) {
                keel::u64 drained = 0u;
                const keel::isize ignored = ::read(m_impl->wakeFd, &drained, sizeof(drained));
                (void)ignored;
            }
        } else {
            m_impl->wl.DisplayCancelRead(m_impl->display);
        }
    }

    if (m_impl->closeRequested) {
        return false;
    }

    // Apply coalesced xdg_surface.configure / HiDPI scale right before rendering the next VBlank frame
    const bool readyToRender = !m_impl->vsync || !m_impl->frameCallbackPending;
    if (readyToRender && (m_impl->hasPendingConfigure || m_impl->scaleDirty)) {
        m_impl->ApplyPendingSurfaceState(&ui, /*duringInit=*/false);
    }

    return !m_impl->closeRequested;
}

void WaylandPlatformWindow::WakeUp() noexcept {
    if (m_impl && m_impl->wakeFd >= 0) {
        const keel::u64 one = 1u;
        const keel::isize ignored = ::write(m_impl->wakeFd, &one, sizeof(one));
        (void)ignored;
    }
}

bool WaylandPlatformWindow::MakeContextCurrent() noexcept {
    return m_impl ? m_impl->egl.MakeCurrent() : true;
}

void WaylandPlatformWindow::SwapBuffers() noexcept {
    if (!m_impl || !m_impl->surface) return;

    // Attach wl_surface.frame callback BEFORE eglSwapBuffers() commits the new buffer
    if (m_impl->vsync && !m_impl->frameCallbackPending) {
        if (m_impl->frameCallback) {
            m_impl->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(m_impl->frameCallback));
            m_impl->frameCallback = nullptr;
        }
        wl_argument arg[1]{};
        m_impl->frameCallback = m_impl->wl.MarshalNew<wl_callback>(
            m_impl->surface,
            kWlSurfaceFrame,
            m_impl->wl.ifaceCallback,
            arg
        );
        if (m_impl->frameCallback) {
            m_impl->wl.AddListener(m_impl->frameCallback, &Impl::kFrameCallbackListener, m_impl.get());
            m_impl->frameCallbackPending = true;
            m_impl->frameSubmittedMs = keel::monotonic_ms();
        }
    }

    m_impl->egl.SwapBuffers();
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
}

GlProcAddressFn WaylandPlatformWindow::GetGlProcAddressFn() const noexcept {
    return m_impl ? m_impl->egl.GetProcAddressFn() : nullptr;
}

keel::i32 WaylandPlatformWindow::GetFramebufferWidth() const noexcept {
    return m_impl ? m_impl->fbWidth : 0;
}

keel::i32 WaylandPlatformWindow::GetFramebufferHeight() const noexcept {
    return m_impl ? m_impl->fbHeight : 0;
}

keel::f32 WaylandPlatformWindow::GetDpiScale() const noexcept {
    return m_impl ? m_impl->dpiScale : 1.0f;
}

keel::f32 WaylandPlatformWindow::GetDisplayPixelsPerMm() const noexcept {
    return m_impl ? m_impl->pixelsPerMm : (96.0f / 25.4f);
}

bool WaylandPlatformWindow::IsMinimized() const noexcept {
    return m_impl ? m_impl->suspended : false;
}

bool WaylandPlatformWindow::ShouldRenderFrame() const noexcept {
    if (!m_impl || !m_impl->alive || m_impl->suspended) {
        return false;
    }
    if (m_impl->vsync && m_impl->frameCallbackPending) {
        return false;
    }
    return true;
}

bool WaylandPlatformWindow::IsLiveResizeFrame() const noexcept {
    return m_impl ? m_impl->resizing : false;
}

bool WaylandPlatformWindow::IsVsyncEnabled() const noexcept {
    return m_impl ? m_impl->vsync : true;
}

void WaylandPlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    if (!m_impl) return;
    m_impl->vsync = enabled;
    if (!enabled) {
        m_impl->frameCallbackPending = false;
        if (m_impl->frameCallback) {
            m_impl->wl.ProxyDestroy(reinterpret_cast<wl_proxy*>(m_impl->frameCallback));
            m_impl->frameCallback = nullptr;
        }
    }
}

void WaylandPlatformWindow::SetCursor(CursorType cursor) noexcept {
    if (!m_impl || cursor == m_impl->activeCursor) return;
    m_impl->activeCursor = cursor;
    if (!m_impl->resizeCursorOverride) {
        m_impl->ApplyCurrentCursor();
    }
}

void WaylandPlatformWindow::OpenUrl(keel::StringRef url) const noexcept {
    linux_detail::OpenLinuxUrl(url);
}

void WaylandPlatformWindow::SetTitle(keel::StringRef title) noexcept {
    if (!m_impl || !m_impl->xdgToplevel) return;
    const keel::String titleStr = keel::String::from(title);
    wl_argument arg[1]{};
    arg[0].s = titleStr.c_str();
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetTitle, arg);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
}

void WaylandPlatformWindow::Minimize() noexcept {
    if (!m_impl || !m_impl->xdgToplevel) return;
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetMinimized);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
}

void WaylandPlatformWindow::Maximize() noexcept {
    if (!m_impl || !m_impl->xdgToplevel) return;
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelSetMaximized);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
}

void WaylandPlatformWindow::Restore() noexcept {
    if (!m_impl || !m_impl->xdgToplevel) return;
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelUnsetMaximized);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
}

void WaylandPlatformWindow::RequestClose() noexcept {
    if (!m_impl) return;
    if (!m_impl->callbacks.onCloseRequested || m_impl->callbacks.onCloseRequested()) {
        m_impl->closeRequested = true;
    }
}

bool WaylandPlatformWindow::IsMaximized() const noexcept {
    return m_impl ? m_impl->maximized : false;
}

bool WaylandPlatformWindow::IsFocused() const noexcept {
    return m_impl ? m_impl->focused : true;
}

bool WaylandPlatformWindow::SupportsCustomTitleBar() const noexcept {
    return true;
}

bool WaylandPlatformWindow::IsCustomTitleBar() const noexcept {
    return m_impl ? m_impl->customTitleBar : false;
}

bool WaylandPlatformWindow::SetCustomTitleBar(bool enabled) noexcept {
    if (!m_impl) return false;
    if (!enabled && !m_impl->compositorSupportsSsd) {
        // CSD-only compositor (e.g. GNOME Mutter): cannot switch to server-side decorations
        return false;
    }
    if (m_impl->customTitleBar == enabled) {
        return true;
    }
    m_impl->customTitleBar = enabled;
    if (m_impl->toplevelDeco) {
        wl_argument arg[1]{};
        arg[0].u = enabled ? kZxdgDecoModeClientSide : kZxdgDecoModeServerSide;
        m_impl->wl.MarshalVoid(m_impl->toplevelDeco, kZxdgToplevelDecoSetMode, arg);
        if (m_impl->display) {
            (void)m_impl->wl.DisplayFlush(m_impl->display);
        }
    }
    if (!enabled && m_impl->resizeCursorOverride) {
        m_impl->resizeCursorOverride = false;
        m_impl->ApplyCurrentCursor();
    }
    return true;
}

void WaylandPlatformWindow::StartSystemMove() noexcept {
    if (!m_impl || !m_impl->xdgToplevel || !m_impl->seat || m_impl->lastPointerSerial == 0u) return;
    wl_argument args[2]{};
    args[0].o = reinterpret_cast<wl_object*>(m_impl->seat);
    args[1].u = m_impl->lastPointerSerial;
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelMove, args);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
    if (m_impl->activeUi) {
        m_impl->activeUi->ResetMouseButtonsAfterModalDialog();
    }
}

void WaylandPlatformWindow::StartSystemResize(WindowHitRegion edge) noexcept {
    if (!m_impl || !m_impl->xdgToplevel || !m_impl->seat || m_impl->lastPointerSerial == 0u) return;
    keel::u32 wlEdge = 0u;
    keel::u32 shape = 0u;
    CursorType fallbackCur = CursorType::Default;
    if (!MapHitRegionToWaylandResize(edge, wlEdge, shape, fallbackCur)) {
        return;
    }
    wl_argument args[3]{};
    args[0].o = reinterpret_cast<wl_object*>(m_impl->seat);
    args[1].u = m_impl->lastPointerSerial;
    args[2].u = wlEdge;
    m_impl->wl.MarshalVoid(m_impl->xdgToplevel, kXdgToplevelResize, args);
    if (m_impl->display) {
        (void)m_impl->wl.DisplayFlush(m_impl->display);
    }
    if (m_impl->activeUi) {
        m_impl->activeUi->ResetMouseButtonsAfterModalDialog();
    }
}

bool WaylandPlatformWindow::SupportsFileDialogs() const noexcept {
    return true;
}

FileDialogResult WaylandPlatformWindow::ShowFileDialog(const FileDialogOptions& opts) {
    return linux_detail::ShowLinuxFileDialog(opts);
}

MessageBoxChoice WaylandPlatformWindow::ShowMessageBox(const MessageBoxOptions& opts) {
    return linux_detail::ShowLinuxMessageBox(opts);
}

keel::StringRef WaylandPlatformWindow::GetPlatformName() const noexcept {
    return "Wayland + EGL";
}

#else // !GUIKIT_NATIVE_COMPILE_WAYLAND

struct WaylandPlatformWindow::Impl {};

WaylandPlatformWindow::WaylandPlatformWindow() noexcept = default;
WaylandPlatformWindow::~WaylandPlatformWindow() = default;
bool WaylandPlatformWindow::IsRuntimeAvailable() noexcept { return false; }
bool WaylandPlatformWindow::Create(const PlatformWindowDesc&, Context&, WindowHostCallbacks) { return false; }
void WaylandPlatformWindow::Destroy() noexcept {}
bool WaylandPlatformWindow::PumpEvents(Context&, bool, keel::i32) { return false; }
void WaylandPlatformWindow::WakeUp() noexcept {}
bool WaylandPlatformWindow::MakeContextCurrent() noexcept { return false; }
void WaylandPlatformWindow::SwapBuffers() noexcept {}
GlProcAddressFn WaylandPlatformWindow::GetGlProcAddressFn() const noexcept { return nullptr; }
keel::i32 WaylandPlatformWindow::GetFramebufferWidth() const noexcept { return 0; }
keel::i32 WaylandPlatformWindow::GetFramebufferHeight() const noexcept { return 0; }
keel::f32 WaylandPlatformWindow::GetDpiScale() const noexcept { return 1.0f; }
keel::f32 WaylandPlatformWindow::GetDisplayPixelsPerMm() const noexcept { return 96.0f / 25.4f; }
bool WaylandPlatformWindow::IsMinimized() const noexcept { return false; }
bool WaylandPlatformWindow::ShouldRenderFrame() const noexcept { return false; }
bool WaylandPlatformWindow::IsLiveResizeFrame() const noexcept { return false; }
bool WaylandPlatformWindow::IsVsyncEnabled() const noexcept { return true; }
void WaylandPlatformWindow::SetVsyncEnabled(bool) noexcept {}
void WaylandPlatformWindow::SetCursor(CursorType) noexcept {}
void WaylandPlatformWindow::OpenUrl(keel::StringRef) const noexcept {}
void WaylandPlatformWindow::SetTitle(keel::StringRef) noexcept {}
void WaylandPlatformWindow::Minimize() noexcept {}
void WaylandPlatformWindow::Maximize() noexcept {}
void WaylandPlatformWindow::Restore() noexcept {}
void WaylandPlatformWindow::RequestClose() noexcept {}
bool WaylandPlatformWindow::IsMaximized() const noexcept { return false; }
bool WaylandPlatformWindow::IsFocused() const noexcept { return true; }
bool WaylandPlatformWindow::SupportsCustomTitleBar() const noexcept { return false; }
bool WaylandPlatformWindow::IsCustomTitleBar() const noexcept { return false; }
bool WaylandPlatformWindow::SetCustomTitleBar(bool) noexcept { return false; }
void WaylandPlatformWindow::StartSystemMove() noexcept {}
void WaylandPlatformWindow::StartSystemResize(WindowHitRegion) noexcept {}
bool WaylandPlatformWindow::SupportsFileDialogs() const noexcept { return false; }
FileDialogResult WaylandPlatformWindow::ShowFileDialog(const FileDialogOptions&) { return {.accepted = false, .supported = false, .paths = {}}; }
MessageBoxChoice WaylandPlatformWindow::ShowMessageBox(const MessageBoxOptions&) { return MessageBoxChoice::Cancel; }
keel::StringRef WaylandPlatformWindow::GetPlatformName() const noexcept { return "Wayland (Unavailable)"; }

#endif // GUIKIT_NATIVE_COMPILE_WAYLAND

} // namespace guikit::native
