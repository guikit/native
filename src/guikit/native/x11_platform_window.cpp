#include "guikit/native/x11_platform_window.hpp"

#include "guikit/backend/vector_backend.hpp"
#include "guikit/context.hpp"
#include "guikit/core/assets.hpp"
#include "keel/array.h"
#include "keel/dynlib.h"
#include "keel/env.h"
#include "keel/format.h"
#include "keel/math.h"
#include "keel/string.h"
#include "keel/time.h"
#include "linux/linux_desktop_services.hpp"
#include "linux/linux_egl_context.hpp"
#include "linux/linux_keymap.hpp"

#if defined(__linux__) && !defined(__ANDROID__) && __has_include(<X11/Xlib.h>)
#define GUIKIT_NATIVE_COMPILE_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
using X11Card = unsigned long; // keel-lint: ignore
using X11Long = long; // keel-lint: ignore
static constexpr X11Card kX11None = None;
static constexpr Atom kX11XaAtom = 4u;
static constexpr Atom kX11XaCardinal = 6u;
static constexpr Atom kX11XaString = 31u;
#undef None
#undef DestroyAll
#undef DefaultScreen
#undef DefaultDepth
#undef DefaultVisual
#undef RootWindow
#undef ConnectionNumber
#undef DisplayWidth
#undef DisplayWidthMM
#else
#define GUIKIT_NATIVE_COMPILE_X11 0
#endif

namespace guikit::native {

#if GUIKIT_NATIVE_COMPILE_X11

namespace {

// Exact XSync C ABI from <X11/extensions/syncconst.h> & <X11/extensions/sync.h>
// (dynamically loaded from libXext.so.6 with zero link-time Xext dependency).
struct X11SyncValue {
    keel::i32 hi;
    keel::u32 lo;
};
using X11SyncCounter = XID;

[[nodiscard]] constexpr X11SyncValue MakeSyncValue(keel::u64 v) noexcept {
    return X11SyncValue{
        .hi = keel::as<keel::i32>((v >> 32u) & 0xFFFFFFFFu),
        .lo = keel::as<keel::u32>(v & 0xFFFFFFFFu)
    };
}

struct X11Api {
    keel::DynLib lib;

    using PfnXOpenDisplay = Display*(*)(const char*);
    using PfnXCloseDisplay = keel::i32(*)(Display*);
    using PfnXDefaultScreen = keel::i32(*)(Display*);
    using PfnXDefaultDepth = keel::i32(*)(Display*, keel::i32);
    using PfnXDefaultVisual = Visual*(*)(Display*, keel::i32);
    using PfnXRootWindow = Window(*)(Display*, keel::i32);
    using PfnXGetVisualInfo = XVisualInfo*(*)(Display*, X11Long, XVisualInfo*, keel::i32*);
    using PfnXCreateColormap = Colormap(*)(Display*, Window, Visual*, keel::i32);
    using PfnXFreeColormap = keel::i32(*)(Display*, Colormap);
    using PfnXCreateWindow = Window(*)(Display*, Window, keel::i32, keel::i32, keel::u32, keel::u32, keel::u32, keel::i32, keel::u32, Visual*, X11Card, XSetWindowAttributes*);
    using PfnXCreateSimpleWindow = Window(*)(Display*, Window, keel::i32, keel::i32, keel::u32, keel::u32, keel::u32, X11Card, X11Card);
    using PfnXDestroyWindow = keel::i32(*)(Display*, Window);
    using PfnXSelectInput = keel::i32(*)(Display*, Window, X11Long);
    using PfnXMapWindow = keel::i32(*)(Display*, Window);
    using PfnXFlush = keel::i32(*)(Display*);
    using PfnXPending = keel::i32(*)(Display*);
    using PfnXNextEvent = keel::i32(*)(Display*, XEvent*);
    using PfnXInternAtom = Atom(*)(Display*, const char*, Bool);
    using PfnXSetWMProtocols = Status(*)(Display*, Window, Atom*, keel::i32);
    using PfnXStoreName = keel::i32(*)(Display*, Window, const char*);
    using PfnXChangeProperty = keel::i32(*)(Display*, Window, Atom, Atom, keel::i32, keel::i32, const keel::u8*, keel::i32);
    using PfnXAllocSizeHints = XSizeHints*(*)();
    using PfnXSetWMNormalHints = void(*)(Display*, Window, XSizeHints*);
    using PfnXFree = keel::i32(*)(void*);
    using PfnXLookupKeysym = KeySym(*)(XKeyEvent*, keel::i32);
    using PfnXLookupString = keel::i32(*)(XKeyEvent*, char*, keel::i32, KeySym*, XComposeStatus*);
    using PfnXOpenIM = XIM(*)(Display*, void*, char*, char*);
    using PfnXCloseIM = Status(*)(XIM);
    using PfnXCreateIC = XIC(*)(XIM, ...);
    using PfnXDestroyIC = void(*)(XIC);
    using PfnXutf8LookupString = keel::i32(*)(XIC, XKeyPressedEvent*, char*, keel::i32, KeySym*, Status*);
    using PfnXFilterEvent = Bool(*)(XEvent*, Window);
    using PfnXCreateFontCursor = Cursor(*)(Display*, keel::u32);
    using PfnXDefineCursor = keel::i32(*)(Display*, Window, Cursor);
    using PfnXUndefineCursor = keel::i32(*)(Display*, Window);
    using PfnXFreeCursor = keel::i32(*)(Display*, Cursor);
    using PfnXConnectionNumber = keel::i32(*)(Display*);
    using PfnXDisplayWidth = keel::i32(*)(Display*, keel::i32);
    using PfnXDisplayWidthMM = keel::i32(*)(Display*, keel::i32);
    using PfnXResourceManagerString = char*(*)(Display*);
    using PfnXSetSelectionOwner = keel::i32(*)(Display*, Atom, Window, Time);
    using PfnXGetSelectionOwner = Window(*)(Display*, Atom);
    using PfnXConvertSelection = keel::i32(*)(Display*, Atom, Atom, Atom, Window, Time);
    using PfnXGetWindowProperty = keel::i32(*)(Display*, Window, Atom, X11Long, X11Long, Bool, Atom, Atom*, keel::i32*, X11Card*, X11Card*, keel::u8**);
    using PfnXDeleteProperty = keel::i32(*)(Display*, Window, Atom);
    using PfnXSendEvent = Status(*)(Display*, Window, Bool, X11Long, XEvent*);
    using PfnXCheckTypedWindowEvent = Bool(*)(Display*, Window, keel::i32, XEvent*);
    using PfnXIconifyWindow = Status(*)(Display*, Window, keel::i32);
    using PfnXUngrabPointer = keel::i32(*)(Display*, Time);
    using PfnXQueryPointer = Bool(*)(Display*, Window, Window*, Window*, keel::i32*, keel::i32*, keel::i32*, keel::i32*, keel::u32*);

    PfnXOpenDisplay OpenDisplay{nullptr};
    PfnXCloseDisplay CloseDisplay{nullptr};
    PfnXDefaultScreen DefaultScreen{nullptr};
    PfnXDefaultDepth DefaultDepth{nullptr};
    PfnXDefaultVisual DefaultVisual{nullptr};
    PfnXRootWindow RootWindow{nullptr};
    PfnXGetVisualInfo GetVisualInfo{nullptr};
    PfnXCreateColormap CreateColormap{nullptr};
    PfnXFreeColormap FreeColormap{nullptr};
    PfnXCreateWindow CreateWindow{nullptr};
    PfnXCreateSimpleWindow CreateSimpleWindow{nullptr};
    PfnXDestroyWindow DestroyWindow{nullptr};
    PfnXSelectInput SelectInput{nullptr};
    PfnXMapWindow MapWindow{nullptr};
    PfnXFlush Flush{nullptr};
    PfnXPending Pending{nullptr};
    PfnXNextEvent NextEvent{nullptr};
    PfnXInternAtom InternAtom{nullptr};
    PfnXSetWMProtocols SetWMProtocols{nullptr};
    PfnXStoreName StoreName{nullptr};
    PfnXChangeProperty ChangeProperty{nullptr};
    PfnXAllocSizeHints AllocSizeHints{nullptr};
    PfnXSetWMNormalHints SetWMNormalHints{nullptr};
    PfnXFree Free{nullptr};
    PfnXLookupKeysym LookupKeysym{nullptr};
    PfnXLookupString LookupString{nullptr};
    PfnXOpenIM OpenIM{nullptr};
    PfnXCloseIM CloseIM{nullptr};
    PfnXCreateIC CreateIC{nullptr};
    PfnXDestroyIC DestroyIC{nullptr};
    PfnXutf8LookupString Utf8LookupString{nullptr};
    PfnXFilterEvent FilterEvent{nullptr};
    PfnXCreateFontCursor CreateFontCursor{nullptr};
    PfnXDefineCursor DefineCursor{nullptr};
    PfnXUndefineCursor UndefineCursor{nullptr};
    PfnXFreeCursor FreeCursor{nullptr};
    PfnXConnectionNumber ConnectionNumber{nullptr};
    PfnXDisplayWidth DisplayWidth{nullptr};
    PfnXDisplayWidthMM DisplayWidthMM{nullptr};
    PfnXResourceManagerString ResourceManagerString{nullptr};
    PfnXSetSelectionOwner SetSelectionOwner{nullptr};
    PfnXGetSelectionOwner GetSelectionOwner{nullptr};
    PfnXConvertSelection ConvertSelection{nullptr};
    PfnXGetWindowProperty GetWindowProperty{nullptr};
    PfnXDeleteProperty DeleteProperty{nullptr};
    PfnXSendEvent SendEvent{nullptr};
    PfnXCheckTypedWindowEvent CheckTypedWindowEvent{nullptr};
    PfnXIconifyWindow IconifyWindow{nullptr};
    PfnXUngrabPointer UngrabPointer{nullptr};
    PfnXQueryPointer QueryPointer{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libX11.so.6");
        if (!opened) opened = keel::DynLib::open("libX11.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        OpenDisplay = lib.symbol<PfnXOpenDisplay>("XOpenDisplay");
        CloseDisplay = lib.symbol<PfnXCloseDisplay>("XCloseDisplay");
        DefaultScreen = lib.symbol<PfnXDefaultScreen>("XDefaultScreen");
        DefaultDepth = lib.symbol<PfnXDefaultDepth>("XDefaultDepth");
        DefaultVisual = lib.symbol<PfnXDefaultVisual>("XDefaultVisual");
        RootWindow = lib.symbol<PfnXRootWindow>("XRootWindow");
        GetVisualInfo = lib.symbol<PfnXGetVisualInfo>("XGetVisualInfo");
        CreateColormap = lib.symbol<PfnXCreateColormap>("XCreateColormap");
        FreeColormap = lib.symbol<PfnXFreeColormap>("XFreeColormap");
        CreateWindow = lib.symbol<PfnXCreateWindow>("XCreateWindow");
        CreateSimpleWindow = lib.symbol<PfnXCreateSimpleWindow>("XCreateSimpleWindow");
        DestroyWindow = lib.symbol<PfnXDestroyWindow>("XDestroyWindow");
        SelectInput = lib.symbol<PfnXSelectInput>("XSelectInput");
        MapWindow = lib.symbol<PfnXMapWindow>("XMapWindow");
        Flush = lib.symbol<PfnXFlush>("XFlush");
        Pending = lib.symbol<PfnXPending>("XPending");
        NextEvent = lib.symbol<PfnXNextEvent>("XNextEvent");
        InternAtom = lib.symbol<PfnXInternAtom>("XInternAtom");
        SetWMProtocols = lib.symbol<PfnXSetWMProtocols>("XSetWMProtocols");
        StoreName = lib.symbol<PfnXStoreName>("XStoreName");
        ChangeProperty = lib.symbol<PfnXChangeProperty>("XChangeProperty");
        AllocSizeHints = lib.symbol<PfnXAllocSizeHints>("XAllocSizeHints");
        SetWMNormalHints = lib.symbol<PfnXSetWMNormalHints>("XSetWMNormalHints");
        Free = lib.symbol<PfnXFree>("XFree");
        LookupKeysym = lib.symbol<PfnXLookupKeysym>("XLookupKeysym");
        LookupString = lib.symbol<PfnXLookupString>("XLookupString");
        OpenIM = lib.symbol<PfnXOpenIM>("XOpenIM");
        CloseIM = lib.symbol<PfnXCloseIM>("XCloseIM");
        CreateIC = lib.symbol<PfnXCreateIC>("XCreateIC");
        DestroyIC = lib.symbol<PfnXDestroyIC>("XDestroyIC");
        Utf8LookupString = lib.symbol<PfnXutf8LookupString>("Xutf8LookupString");
        FilterEvent = lib.symbol<PfnXFilterEvent>("XFilterEvent");
        CreateFontCursor = lib.symbol<PfnXCreateFontCursor>("XCreateFontCursor");
        DefineCursor = lib.symbol<PfnXDefineCursor>("XDefineCursor");
        UndefineCursor = lib.symbol<PfnXUndefineCursor>("XUndefineCursor");
        FreeCursor = lib.symbol<PfnXFreeCursor>("XFreeCursor");
        ConnectionNumber = lib.symbol<PfnXConnectionNumber>("XConnectionNumber");
        DisplayWidth = lib.symbol<PfnXDisplayWidth>("XDisplayWidth");
        DisplayWidthMM = lib.symbol<PfnXDisplayWidthMM>("XDisplayWidthMM");
        ResourceManagerString = lib.symbol<PfnXResourceManagerString>("XResourceManagerString");
        SetSelectionOwner = lib.symbol<PfnXSetSelectionOwner>("XSetSelectionOwner");
        GetSelectionOwner = lib.symbol<PfnXGetSelectionOwner>("XGetSelectionOwner");
        ConvertSelection = lib.symbol<PfnXConvertSelection>("XConvertSelection");
        GetWindowProperty = lib.symbol<PfnXGetWindowProperty>("XGetWindowProperty");
        DeleteProperty = lib.symbol<PfnXDeleteProperty>("XDeleteProperty");
        SendEvent = lib.symbol<PfnXSendEvent>("XSendEvent");
        CheckTypedWindowEvent = lib.symbol<PfnXCheckTypedWindowEvent>("XCheckTypedWindowEvent");
        IconifyWindow = lib.symbol<PfnXIconifyWindow>("XIconifyWindow");
        UngrabPointer = lib.symbol<PfnXUngrabPointer>("XUngrabPointer");
        QueryPointer = lib.symbol<PfnXQueryPointer>("XQueryPointer");

        return OpenDisplay && CloseDisplay && DefaultScreen && RootWindow &&
               (CreateWindow || CreateSimpleWindow) && DestroyWindow && SelectInput && MapWindow &&
               Flush && Pending && NextEvent && InternAtom && SetWMProtocols &&
               StoreName && LookupKeysym && LookupString && ConnectionNumber;
    }
};

struct XSyncApi {
    keel::DynLib lib;

    using PfnQueryExtension = Status(*)(Display*, keel::i32*, keel::i32*);
    using PfnInitialize = Status(*)(Display*, keel::i32*, keel::i32*);
    using PfnCreateCounter = X11SyncCounter(*)(Display*, X11SyncValue);
    using PfnSetCounter = Status(*)(Display*, X11SyncCounter, X11SyncValue);
    using PfnDestroyCounter = Status(*)(Display*, X11SyncCounter);

    PfnQueryExtension QueryExtension{nullptr};
    PfnInitialize Initialize{nullptr};
    PfnCreateCounter CreateCounter{nullptr};
    PfnSetCounter SetCounter{nullptr};
    PfnDestroyCounter DestroyCounter{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libXext.so.6");
        if (!opened) opened = keel::DynLib::open("libXext.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        QueryExtension = lib.symbol<PfnQueryExtension>("XSyncQueryExtension");
        Initialize = lib.symbol<PfnInitialize>("XSyncInitialize");
        CreateCounter = lib.symbol<PfnCreateCounter>("XSyncCreateCounter");
        SetCounter = lib.symbol<PfnSetCounter>("XSyncSetCounter");
        DestroyCounter = lib.symbol<PfnDestroyCounter>("XSyncDestroyCounter");
        return QueryExtension && Initialize && CreateCounter && SetCounter && DestroyCounter;
    }
};

[[nodiscard]] keel::u32 MapCursorShape(CursorType cursor) noexcept {
    switch (cursor) {
        case CursorType::Pointer:    return XC_hand2;
        case CursorType::Text:       return XC_xterm;
        case CursorType::ResizeEW:   return XC_sb_h_double_arrow;
        case CursorType::ResizeNS:   return XC_sb_v_double_arrow;
        case CursorType::ResizeNWSE: return XC_top_left_corner;
        case CursorType::ResizeNESW: return XC_top_right_corner;
        case CursorType::Move:       return XC_fleur;
        case CursorType::NotAllowed: return XC_X_cursor;
        case CursorType::Default:
        default:                     return XC_left_ptr;
    }
}

[[nodiscard]] keel::f32 ParseXftDpiScale(const char* xrm) noexcept {
    if (!xrm) return 1.0f;
    const keel::StringRef s(xrm);
    const keel::usize pos = s.find("Xft.dpi:");
    if (pos == keel::StringRef::npos) return 1.0f;
    keel::usize i = pos + 8u;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    keel::f32 val = 0.0f;
    bool hasDigit = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        val = val * 10.0f + keel::as<keel::f32>(s[i] - '0');
        hasDigit = true;
        ++i;
    }
    if (!hasDigit || val < 48.0f || val > 480.0f) return 1.0f;
    return keel::clamp(val / 96.0f, 0.75f, 4.0f);
}

[[nodiscard]] KeyModifiers ExtractX11Modifiers(keel::u32 state) noexcept {
    KeyModifiers mods{};
    mods.shift = (state & ShiftMask) != 0u;
    mods.ctrl = (state & ControlMask) != 0u;
    mods.alt = (state & Mod1Mask) != 0u;
    return mods;
}

void AppendDownsampledNetWmIcon(
    keel::Array<X11Card>& outData,
    const keel::u32* srcRgba,
    keel::u32 srcW,
    keel::u32 srcH,
    keel::u32 dstDim
) {
    if (!srcRgba || srcW == 0u || srcH == 0u || dstDim == 0u) return;
    outData.push_back(keel::as<X11Card>(dstDim));
    outData.push_back(keel::as<X11Card>(dstDim));

    for (keel::u32 y = 0u; y < dstDim; ++y) {
        const keel::u32 sy0 = (y * srcH) / dstDim;
        const keel::u32 sy1 = keel::max(sy0 + 1u, ((y + 1u) * srcH) / dstDim);
        for (keel::u32 x = 0u; x < dstDim; ++x) {
            const keel::u32 sx0 = (x * srcW) / dstDim;
            const keel::u32 sx1 = keel::max(sx0 + 1u, ((x + 1u) * srcW) / dstDim);
            keel::u64 sumR = 0u, sumG = 0u, sumB = 0u, sumA = 0u, cnt = 0u;
            for (keel::u32 sy = sy0; sy < sy1 && sy < srcH; ++sy) {
                const keel::u32* row = srcRgba + keel::as<keel::usize>(sy) * srcW;
                for (keel::u32 sx = sx0; sx < sx1 && sx < srcW; ++sx) {
                    const keel::u32 p = row[sx];
                    sumR += (p & 0xFFu);
                    sumG += ((p >> 8) & 0xFFu);
                    sumB += ((p >> 16) & 0xFFu);
                    sumA += ((p >> 24) & 0xFFu);
                    ++cnt;
                }
            }
            if (cnt == 0u) cnt = 1u;
            const keel::u32 r = keel::as<keel::u32>(sumR / cnt);
            const keel::u32 g = keel::as<keel::u32>(sumG / cnt);
            const keel::u32 b = keel::as<keel::u32>(sumB / cnt);
            const keel::u32 a = keel::as<keel::u32>(sumA / cnt);
            const keel::u32 argb = (a << 24) | (r << 16) | (g << 8) | b;
            outData.push_back(keel::as<X11Card>(argb));
        }
    }
}

void SendX11NetWmState(
    X11Api& x11,
    Display* display,
    keel::i32 screen,
    Window win,
    X11Long action,
    Atom prop1,
    Atom prop2
) noexcept {
    if (!display || win == 0 || !x11.SendEvent) return;
    const Atom netWmState = x11.InternAtom(display, "_NET_WM_STATE", False);
    if (netWmState == kX11None) return;
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = netWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = action;
    ev.xclient.data.l[1] = keel::as<X11Long>(prop1);
    ev.xclient.data.l[2] = keel::as<X11Long>(prop2);
    ev.xclient.data.l[3] = 1; // normal application source
    const Window root = x11.RootWindow(display, screen);
    (void)x11.SendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    x11.Flush(display);
}

} // namespace

struct X11PlatformWindow::Impl {
    X11Api x11{};
    XSyncApi xsync{};
    linux_detail::LinuxEglContext egl{};

    Display* display{nullptr};
    keel::i32 screen{0};
    Colormap colormap{0};
    Window window{0};
    Atom wmProtocols{0};
    Atom wmDeleteWindow{0};
    Atom atomNetWmSyncRequest{0};
    Atom atomNetWmSyncCounter{0};
    Atom atomClipboard{0};
    Atom atomUtf8String{0};
    Atom atomTargets{0};
    Atom atomClipboardProp{0};
    Atom atomNetWmState{0};
    Atom atomNetWmStateMaxH{0};
    Atom atomNetWmStateMaxV{0};
    XIM xim{nullptr};
    XIC xic{nullptr};

    // EWMH _NET_WM_SYNC_REQUEST (basic + extended frame synchronization counters)
    bool hasXSync{false};
    X11SyncCounter syncCounterBasic{0};
    X11SyncCounter syncCounterExtended{0};
    bool hasPendingSync{false};
    bool pendingSyncIsExtended{false};
    bool extendedFrameStarted{false};
    keel::u64 pendingSyncValue{0u};

    keel::i32 wakeFd{-1};
    keel::i32 width{1380};
    keel::i32 height{800};
    bool resizeCoalescedDirty{false};
    keel::f32 dpiScale{1.0f};
    keel::f32 pixelsPerMm{96.0f / 25.4f};
    bool vsync{true};
    bool customTitleBar{false};
    bool maximized{false};
    bool focused{true};
    bool minimized{false};
    bool closeRequested{false};
    bool alive{false};
    bool resizeCursorOverride{false};

    CursorType activeCursor{CursorType::Default};
    Cursor cursors[9]{};

    WindowHostCallbacks callbacks{};
    Context* activeUi{nullptr};
    keel::String clipboardText;

    void DestroyAll() noexcept {
        alive = false;
        egl.Destroy();
        if (display) {
            if (hasXSync && xsync.DestroyCounter) {
                if (syncCounterExtended != 0) {
                    (void)xsync.DestroyCounter(display, syncCounterExtended);
                    syncCounterExtended = 0;
                }
                if (syncCounterBasic != 0) {
                    (void)xsync.DestroyCounter(display, syncCounterBasic);
                    syncCounterBasic = 0;
                }
            }
            hasXSync = false;
            hasPendingSync = false;
            extendedFrameStarted = false;
            if (x11.FreeCursor) {
                for (Cursor& c : cursors) {
                    if (c != 0) {
                        x11.FreeCursor(display, c);
                        c = 0;
                    }
                }
            }
            if (xic && x11.DestroyIC) {
                x11.DestroyIC(xic);
                xic = nullptr;
            }
            if (xim && x11.CloseIM) {
                x11.CloseIM(xim);
                xim = nullptr;
            }
            if (window != 0 && x11.DestroyWindow) {
                x11.DestroyWindow(display, window);
                window = 0;
            }
            if (colormap != 0 && x11.FreeColormap) {
                x11.FreeColormap(display, colormap);
                colormap = 0;
            }
            if (x11.CloseDisplay) {
                x11.CloseDisplay(display);
            }
            display = nullptr;
        }
        if (wakeFd >= 0) {
            ::close(wakeFd);
            wakeFd = -1;
        }
        activeUi = nullptr;
        callbacks = {};
    }

    void InitXSyncCounters() noexcept {
        if (!display || window == 0 || atomNetWmSyncRequest == kX11None || atomNetWmSyncCounter == kX11None) {
            return;
        }
        if (!xsync.Load()) {
            return;
        }
        keel::i32 eventBase = 0;
        keel::i32 errorBase = 0;
        keel::i32 major = 0;
        keel::i32 minor = 0;
        if (!xsync.QueryExtension(display, &eventBase, &errorBase) ||
            !xsync.Initialize(display, &major, &minor)) {
            return;
        }

        const X11SyncValue zero{0, 0u};
        syncCounterBasic = xsync.CreateCounter(display, zero);
        syncCounterExtended = xsync.CreateCounter(display, zero);
        if (syncCounterBasic == 0 || syncCounterExtended == 0) {
            if (syncCounterBasic != 0) {
                (void)xsync.DestroyCounter(display, syncCounterBasic);
                syncCounterBasic = 0;
            }
            if (syncCounterExtended != 0) {
                (void)xsync.DestroyCounter(display, syncCounterExtended);
                syncCounterExtended = 0;
            }
            return;
        }

        if (x11.ChangeProperty) {
            const X11Card counters[2] = {
                keel::as<X11Card>(syncCounterBasic),
                keel::as<X11Card>(syncCounterExtended)
            };
            (void)x11.ChangeProperty(
                display,
                window,
                atomNetWmSyncCounter,
                kX11XaCardinal,
                32,
                PropModeReplace,
                reinterpret_cast<const keel::u8*>(counters),
                2
            );
        }
        hasXSync = true;
    }

    void NotifySyncFrameBegin() noexcept {
        if (!hasXSync || !hasPendingSync || !pendingSyncIsExtended || extendedFrameStarted) {
            return;
        }
        if (display && syncCounterExtended != 0 && xsync.SetCounter && pendingSyncValue >= 1u) {
            // EWMH extended sync: odd value signals compositor "frame draw in progress"
            const keel::u64 oddVal = ((pendingSyncValue & 1u) == 0u) ? (pendingSyncValue - 1u) : pendingSyncValue;
            (void)xsync.SetCounter(display, syncCounterExtended, MakeSyncValue(oddVal));
            x11.Flush(display);
            extendedFrameStarted = true;
        }
    }

    void NotifySyncFrameComplete() noexcept {
        if (!hasXSync || !hasPendingSync || !display || !xsync.SetCounter) {
            return;
        }
        if (pendingSyncIsExtended && syncCounterExtended != 0) {
            // Even value signals compositor "frame presented to DRI3, safe to composite atomically"
            const keel::u64 evenVal = ((pendingSyncValue & 1u) == 0u) ? pendingSyncValue : (pendingSyncValue + 1u);
            (void)xsync.SetCounter(display, syncCounterExtended, MakeSyncValue(evenVal));
        } else if (syncCounterBasic != 0) {
            (void)xsync.SetCounter(display, syncCounterBasic, MakeSyncValue(pendingSyncValue));
        }
        x11.Flush(display);
        hasPendingSync = false;
        extendedFrameStarted = false;
    }

    void SetSystemClipboard(keel::StringRef text) {
        clipboardText = keel::String::from(text);
        if (display && window != 0 && atomClipboard != kX11None && x11.SetSelectionOwner) {
            (void)x11.SetSelectionOwner(display, atomClipboard, window, CurrentTime);
            x11.Flush(display);
        }
    }

    [[nodiscard]] keel::String GetSystemClipboard() {
        if (!display || window == 0 || atomClipboard == kX11None ||
            !x11.GetSelectionOwner || !x11.ConvertSelection ||
            !x11.GetWindowProperty || !x11.CheckTypedWindowEvent || !x11.Free) {
            return clipboardText.clone();
        }

        const Window owner = x11.GetSelectionOwner(display, atomClipboard);
        if (owner == kX11None) {
            return {};
        }
        if (owner == window) {
            return clipboardText.clone();
        }

        auto requestTarget = [&](Atom targetAtom, keel::String& out) -> bool {
            if (x11.DeleteProperty) {
                (void)x11.DeleteProperty(display, window, atomClipboardProp);
            }
            (void)x11.ConvertSelection(display, atomClipboard, targetAtom, atomClipboardProp, window, CurrentTime);
            x11.Flush(display);

            const keel::u64 deadlineMs = keel::monotonic_ms() + 150u;
            const keel::i32 x11Fd = x11.ConnectionNumber(display);
            for (;;) {
                XEvent ev{};
                if (x11.CheckTypedWindowEvent(display, window, SelectionNotify, &ev)) {
                    if (ev.xselection.property == kX11None) {
                        return false;
                    }
                    Atom actualType = kX11None;
                    keel::i32 actualFormat = 0;
                    X11Card numItems = 0u;
                    X11Card bytesAfter = 0u;
                    keel::u8* propData = nullptr;
                    const keel::i32 rc = x11.GetWindowProperty(
                        display,
                        window,
                        ev.xselection.property,
                        0,
                        262144,
                        True,
                        AnyPropertyType,
                        &actualType,
                        &actualFormat,
                        &numItems,
                        &bytesAfter,
                        &propData
                    );
                    bool ok = false;
                    if (rc == Success && propData && actualFormat == 8 && numItems > 0u) {
                        out = keel::String::from(
                            keel::StringRef(reinterpret_cast<const char*>(propData), keel::as<keel::usize>(numItems))
                        );
                        ok = true;
                    }
                    if (propData) {
                        x11.Free(propData);
                    }
                    return ok;
                }

                const keel::u64 nowMs = keel::monotonic_ms();
                if (nowMs >= deadlineMs) {
                    return false;
                }
                struct pollfd pfd{};
                pfd.fd = x11Fd;
                pfd.events = POLLIN;
                const keel::i32 remMs = keel::as<keel::i32>(deadlineMs - nowMs);
                if (::poll(&pfd, 1, remMs) <= 0) {
                    return false;
                }
            }
        };

        keel::String result;
        if (atomUtf8String != kX11None && requestTarget(atomUtf8String, result)) {
            return result;
        }
        if (requestTarget(kX11XaString, result)) {
            return result;
        }
        return {};
    }

    void HandleSelectionRequest(const XSelectionRequestEvent& req) noexcept {
        if (!display || !x11.SendEvent || !x11.ChangeProperty) return;
        XEvent reply{};
        reply.xselection.type = SelectionNotify;
        reply.xselection.display = req.display;
        reply.xselection.requestor = req.requestor;
        reply.xselection.selection = req.selection;
        reply.xselection.target = req.target;
        reply.xselection.property = (req.property != kX11None) ? req.property : req.target;
        reply.xselection.time = req.time;

        bool handled = false;
        if (req.selection == atomClipboard && reply.xselection.property != kX11None) {
            if (req.target == atomTargets && atomTargets != kX11None) {
                Atom supported[3]{atomTargets, atomUtf8String, kX11XaString};
                const keel::i32 count = (atomUtf8String != kX11None) ? 3 : 2;
                if (atomUtf8String == kX11None) {
                    supported[1] = kX11XaString;
                }
                (void)x11.ChangeProperty(
                    display,
                    req.requestor,
                    reply.xselection.property,
                    kX11XaAtom,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const keel::u8*>(supported),
                    count
                );
                handled = true;
            } else if ((atomUtf8String != kX11None && req.target == atomUtf8String) ||
                       req.target == kX11XaString) {
                (void)x11.ChangeProperty(
                    display,
                    req.requestor,
                    reply.xselection.property,
                    req.target,
                    8,
                    PropModeReplace,
                    reinterpret_cast<const keel::u8*>(clipboardText.c_str()),
                    keel::as<keel::i32>(clipboardText.size())
                );
                handled = true;
            }
        }

        if (!handled) {
            reply.xselection.property = kX11None;
        }
        (void)x11.SendEvent(display, req.requestor, False, 0, &reply);
        x11.Flush(display);
    }

    void ApplyWindowIcon() {
        if (!display || window == 0 || !x11.ChangeProperty) return;
        const auto iconAsset = Assets::Find("assets/guikit-icon-inter850-1024.png");
        if (!iconAsset || iconAsset->Empty()) return;

        keel::u32 srcW = 0u;
        keel::u32 srcH = 0u;
        keel::Array<keel::u32> rgba;
        if (!DecodeImageFromMemory(iconAsset->Bytes(), srcW, srcH, rgba) || srcW == 0u || srcH == 0u) {
            return;
        }

        const Atom netWmIcon = x11.InternAtom(display, "_NET_WM_ICON", False);
        if (netWmIcon == kX11None) return;

        keel::Array<X11Card> iconData;
        iconData.reserve(64u * 64u + 32u * 32u + 4u);
        AppendDownsampledNetWmIcon(iconData, rgba.data(), srcW, srcH, 64u);
        AppendDownsampledNetWmIcon(iconData, rgba.data(), srcW, srcH, 32u);

        (void)x11.ChangeProperty(
            display,
            window,
            netWmIcon,
            kX11XaCardinal,
            32,
            PropModeReplace,
            reinterpret_cast<const keel::u8*>(iconData.data()),
            keel::as<keel::i32>(iconData.size())
        );
    }

    void ApplyWindowStateFlags(const PlatformWindowDesc& desc) noexcept {
        if (!display || window == 0 || !x11.ChangeProperty) return;
        customTitleBar = desc.customTitleBar && !desc.fullscreen;
        maximized = desc.maximized;

        if (desc.borderless || customTitleBar) {
            const Atom motifHintsAtom = x11.InternAtom(display, "_MOTIF_WM_HINTS", False);
            if (motifHintsAtom != kX11None) {
                struct MotifWmHints {
                    X11Card flags;
                    X11Card functions;
                    X11Card decorations;
                    X11Long inputMode;
                    X11Card status;
                };
                const MotifWmHints hints{2u, 0u, 0u, 0, 0u};
                (void)x11.ChangeProperty(
                    display,
                    window,
                    motifHintsAtom,
                    motifHintsAtom,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const keel::u8*>(&hints),
                    5
                );
            }
        }

        if (desc.fullscreen || desc.maximized) {
            const Atom netWmState = x11.InternAtom(display, "_NET_WM_STATE", False);
            if (netWmState != kX11None) {
                Atom states[2]{};
                keel::i32 count = 0;
                if (desc.fullscreen) {
                    const Atom fs = x11.InternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
                    if (fs != kX11None) states[count++] = fs;
                } else if (desc.maximized) {
                    const Atom maxH = x11.InternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
                    const Atom maxV = x11.InternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
                    if (maxH != kX11None) states[count++] = maxH;
                    if (maxV != kX11None) states[count++] = maxV;
                }
                if (count > 0) {
                    (void)x11.ChangeProperty(
                        display,
                        window,
                        netWmState,
                        kX11XaAtom,
                        32,
                        PropModeReplace,
                        reinterpret_cast<const keel::u8*>(states),
                        count
                    );
                }
            }
        }
    }

    void ApplyRawCursor(CursorType cursor) noexcept {
        if (!display || window == 0) return;
        const auto idx = keel::as<keel::usize>(cursor);
        if (idx >= 9u) return;
        if (cursor == CursorType::Default) {
            if (x11.UndefineCursor) {
                x11.UndefineCursor(display, window);
            }
            return;
        }
        if (cursors[idx] == 0 && x11.CreateFontCursor) {
            cursors[idx] = x11.CreateFontCursor(display, MapCursorShape(cursor));
        }
        if (cursors[idx] != 0 && x11.DefineCursor) {
            x11.DefineCursor(display, window, cursors[idx]);
        }
    }

    void SyncMaximizedFromNetWmState(Context& ui) noexcept {
        if (!display || window == 0 || atomNetWmState == kX11None || !x11.GetWindowProperty) {
            return;
        }
        Atom actualType = kX11None;
        keel::i32 actualFormat = 0;
        X11Card itemCount = 0u;
        X11Card bytesAfter = 0u;
        keel::u8* propData = nullptr;
        if (x11.GetWindowProperty(
                display, window, atomNetWmState, 0, 32, False, kX11XaAtom,
                &actualType, &actualFormat, &itemCount, &bytesAfter, &propData
            ) == Success && propData) {
            bool hasMaxH = false;
            bool hasMaxV = false;
            if (actualFormat == 32) {
                const auto* atoms = reinterpret_cast<const Atom*>(propData);
                for (X11Card i = 0u; i < itemCount; ++i) {
                    if (atoms[i] == atomNetWmStateMaxH) hasMaxH = true;
                    if (atoms[i] == atomNetWmStateMaxV) hasMaxV = true;
                }
            }
            if (x11.Free) {
                x11.Free(propData);
            }
            const bool nowMax = hasMaxH && hasMaxV;
            if (nowMax != maximized) {
                maximized = nowMax;
                ui.InvalidateAll();
                ui.RequestRepaint(2);
            }
        }
    }

    static bool MapResizeRegionToX11(WindowHitRegion reg, CursorType& outCursor, X11Long& outDir) noexcept {
        switch (reg) {
            case WindowHitRegion::ResizeTopLeft:     outCursor = CursorType::ResizeNWSE; outDir = 0; return true;
            case WindowHitRegion::ResizeTop:         outCursor = CursorType::ResizeNS;   outDir = 1; return true;
            case WindowHitRegion::ResizeTopRight:    outCursor = CursorType::ResizeNESW; outDir = 2; return true;
            case WindowHitRegion::ResizeRight:       outCursor = CursorType::ResizeEW;   outDir = 3; return true;
            case WindowHitRegion::ResizeBottomRight: outCursor = CursorType::ResizeNWSE; outDir = 4; return true;
            case WindowHitRegion::ResizeBottom:      outCursor = CursorType::ResizeNS;   outDir = 5; return true;
            case WindowHitRegion::ResizeBottomLeft:  outCursor = CursorType::ResizeNESW; outDir = 6; return true;
            case WindowHitRegion::ResizeLeft:        outCursor = CursorType::ResizeEW;   outDir = 7; return true;
            default: return false;
        }
    }

    void StartMoveResizeAction(X11Long direction) noexcept {
        if (!display || window == 0 || !x11.SendEvent) return;
        const Atom moveResize = x11.InternAtom(display, "_NET_WM_MOVERESIZE", False);
        if (moveResize == kX11None) return;
        const Window root = x11.RootWindow(display, screen);
        keel::i32 rootX = 0;
        keel::i32 rootY = 0;
        if (x11.QueryPointer) {
            Window dummyRoot = 0;
            Window dummyChild = 0;
            keel::i32 winX = 0;
            keel::i32 winY = 0;
            keel::u32 mask = 0u;
            (void)x11.QueryPointer(display, window, &dummyRoot, &dummyChild, &rootX, &rootY, &winX, &winY, &mask);
        }
        if (x11.UngrabPointer) {
            (void)x11.UngrabPointer(display, CurrentTime);
        }
        XEvent ev{};
        ev.xclient.type = ClientMessage;
        ev.xclient.window = window;
        ev.xclient.message_type = moveResize;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = rootX;
        ev.xclient.data.l[1] = rootY;
        ev.xclient.data.l[2] = direction;
        ev.xclient.data.l[3] = 1; // left mouse button
        ev.xclient.data.l[4] = 1; // normal application source
        (void)x11.SendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
        x11.Flush(display);
    }

    void DispatchEvent(XEvent& ev, Context& ui) {
        if (x11.FilterEvent && x11.FilterEvent(&ev, kX11None)) {
            return;
        }
        switch (ev.type) {
            case ClientMessage: {
                const Atom proto = keel::as<Atom>(ev.xclient.data.l[0]);
                if (proto == wmDeleteWindow) {
                    if (!callbacks.onCloseRequested || callbacks.onCloseRequested()) {
                        closeRequested = true;
                    }
                } else if (hasXSync && proto == atomNetWmSyncRequest) {
                    const keel::u64 lo = keel::as<keel::u32>(ev.xclient.data.l[2]);
                    const keel::u64 hi = keel::as<keel::u32>(ev.xclient.data.l[3]);
                    pendingSyncValue = (hi << 32u) | lo;
                    pendingSyncIsExtended = (ev.xclient.data.l[4] == 1);
                    hasPendingSync = true;
                    extendedFrameStarted = false;
                    if (minimized) {
                        NotifySyncFrameComplete();
                    } else {
                        ui.InvalidateAll();
                    }
                }
                break;
            }

            case ConfigureNotify: {
                const keel::i32 nw = ev.xconfigure.width;
                const keel::i32 nh = ev.xconfigure.height;
                if (nw > 0 && nh > 0 && (nw != width || nh != height)) {
                    width = nw;
                    height = nh;
                    resizeCoalescedDirty = true;
                    ui.InvalidateAll();
                }
                break;
            }

            case Expose:
                if (ev.xexpose.count == 0) {
                    ui.InvalidateAll();
                }
                break;

            case MapNotify:
                minimized = false;
                ui.InvalidateAll();
                ui.RequestRepaint(2);
                break;

            case UnmapNotify:
                minimized = true;
                if (hasPendingSync) {
                    NotifySyncFrameComplete();
                }
                break;

            case SelectionRequest:
                HandleSelectionRequest(ev.xselectionrequest);
                break;

            case FocusIn:
                focused = true;
                ui.InvalidateAll();
                ui.RequestRepaint(2);
                break;

            case FocusOut:
                focused = false;
                ui.SetKeyModifiers({});
                ui.InvalidateAll();
                ui.RequestRepaint(2);
                break;

            case PropertyNotify:
                if (ev.xproperty.atom == atomNetWmState) {
                    SyncMaximizedFromNetWmState(ui);
                }
                break;

            case MotionNotify: {
                ui.SetKeyModifiers(ExtractX11Modifiers(ev.xmotion.state));
                const Vec2 physPos{keel::as<keel::f32>(ev.xmotion.x), keel::as<keel::f32>(ev.xmotion.y)};
                if (customTitleBar && callbacks.onHitTest) {
                    const keel::f32 safeDpi = (dpiScale > 0.1f) ? dpiScale : 1.0f;
                    const WindowHitRegion reg = callbacks.onHitTest({physPos.x / safeDpi, physPos.y / safeDpi});
                    CursorType resizeCur = CursorType::Default;
                    X11Long dir = -1;
                    if (MapResizeRegionToX11(reg, resizeCur, dir)) {
                        ApplyRawCursor(resizeCur);
                        resizeCursorOverride = true;
                    } else if (resizeCursorOverride) {
                        resizeCursorOverride = false;
                        ApplyRawCursor(activeCursor);
                    }
                }
                ui.OnMouseMove(physPos);
                break;
            }

            case ButtonPress: {
                ui.SetKeyModifiers(ExtractX11Modifiers(ev.xbutton.state));
                const Vec2 physPos{keel::as<keel::f32>(ev.xbutton.x), keel::as<keel::f32>(ev.xbutton.y)};
                if (customTitleBar && ev.xbutton.button == 1 && callbacks.onHitTest) {
                    const keel::f32 safeDpi = (dpiScale > 0.1f) ? dpiScale : 1.0f;
                    const WindowHitRegion reg = callbacks.onHitTest({physPos.x / safeDpi, physPos.y / safeDpi});
                    CursorType resizeCur = CursorType::Default;
                    X11Long dir = -1;
                    if (MapResizeRegionToX11(reg, resizeCur, dir)) {
                        StartMoveResizeAction(dir);
                        break;
                    }
                }
                ui.OnMouseMove(physPos);
                if (ev.xbutton.button == 1) ui.OnMouseDown(1);
                else if (ev.xbutton.button == 2) ui.OnMouseDown(2);
                else if (ev.xbutton.button == 3) ui.OnMouseDown(3);
                else if (ev.xbutton.button == 4) ui.OnMouseWheel(0.0f, 1.0f);
                else if (ev.xbutton.button == 5) ui.OnMouseWheel(0.0f, -1.0f);
                else if (ev.xbutton.button == 6) ui.OnMouseWheel(1.0f, 0.0f);
                else if (ev.xbutton.button == 7) ui.OnMouseWheel(-1.0f, 0.0f);
                break;
            }

            case ButtonRelease:
                ui.SetKeyModifiers(ExtractX11Modifiers(ev.xbutton.state));
                ui.OnMouseMove({keel::as<keel::f32>(ev.xbutton.x), keel::as<keel::f32>(ev.xbutton.y)});
                if (ev.xbutton.button == 1) ui.OnMouseUp(1);
                else if (ev.xbutton.button == 2) ui.OnMouseUp(2);
                else if (ev.xbutton.button == 3) ui.OnMouseUp(3);
                break;

            case KeyRelease: {
                KeyModifiers mods = ExtractX11Modifiers(ev.xkey.state);
                const KeySym ks = x11.LookupKeysym(&ev.xkey, 0);
                if (ks == XK_Shift_L || ks == XK_Shift_R) mods.shift = false;
                else if (ks == XK_Control_L || ks == XK_Control_R) mods.ctrl = false;
                else if (ks == XK_Alt_L || ks == XK_Alt_R) mods.alt = false;
                ui.SetKeyModifiers(mods);
                break;
            }

            case KeyPress: {
                const KeyModifiers mods = ExtractX11Modifiers(ev.xkey.state);
                ui.SetKeyModifiers(mods);

                const KeySym ks = x11.LookupKeysym(&ev.xkey, 0);
                const Key logicalKey = linux_detail::MapLinuxKeySym(keel::as<keel::u32>(ks));
                if (logicalKey != Key::None) {
                    ui.OnKeyDown(logicalKey, mods);
                }

                if (!mods.ctrl && !mods.alt) {
                    char utf8Buf[64]{};
                    keel::i32 len = 0;
                    if (xic && x11.Utf8LookupString) {
                        Status status = 0;
                        KeySym ignored = NoSymbol;
                        len = x11.Utf8LookupString(xic, &ev.xkey, utf8Buf, keel::as<keel::i32>(sizeof(utf8Buf) - 1), &ignored, &status);
                    } else {
                        KeySym ignored = NoSymbol;
                        len = x11.LookupString(&ev.xkey, utf8Buf, keel::as<keel::i32>(sizeof(utf8Buf) - 1), &ignored, nullptr);
                    }
                    if (len > 0) {
                        const auto first = keel::as<keel::u8>(utf8Buf[0]);
                        if (first >= 0x20u && first != 0x7Fu) {
                            ui.OnTextInput(keel::StringRef(utf8Buf, keel::as<keel::usize>(len)));
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }
};

X11PlatformWindow::X11PlatformWindow() noexcept
    : m_impl(keel::make_unique<Impl>()) {
}

X11PlatformWindow::~X11PlatformWindow() {
    Destroy();
}

bool X11PlatformWindow::IsRuntimeAvailable() noexcept {
    if (!keel::env_get("DISPLAY").has_value()) {
        return false;
    }
    X11Api x11;
    return x11.Load() && linux_detail::LinuxEglContext::IsRuntimeAvailable();
}

bool X11PlatformWindow::Create(
    const PlatformWindowDesc& desc,
    Context& ui,
    WindowHostCallbacks callbacks
) {
    if (!m_impl) return false;
    m_impl->DestroyAll();

    if (!m_impl->x11.Load()) {
        keel::eprintln("[X11PlatformWindow] Failed to load libX11.so.6");
        return false;
    }

    m_impl->display = m_impl->x11.OpenDisplay(nullptr);
    if (!m_impl->display) {
        keel::eprintln("[X11PlatformWindow] XOpenDisplay(nullptr) failed (is DISPLAY set?)");
        return false;
    }

    m_impl->screen = m_impl->x11.DefaultScreen(m_impl->display);
    m_impl->width = keel::max(desc.width, 64);
    m_impl->height = keel::max(desc.height, 64);
    m_impl->vsync = desc.vsync;
    m_impl->closeRequested = false;
    m_impl->minimized = false;
    m_impl->callbacks = keel::move(callbacks);
    m_impl->activeUi = &ui;

    if (desc.highDpi && m_impl->x11.ResourceManagerString) {
        m_impl->dpiScale = ParseXftDpiScale(m_impl->x11.ResourceManagerString(m_impl->display));
    } else {
        m_impl->dpiScale = 1.0f;
    }
    if (m_impl->x11.DisplayWidth && m_impl->x11.DisplayWidthMM) {
        const keel::i32 pxW = m_impl->x11.DisplayWidth(m_impl->display, m_impl->screen);
        const keel::i32 mmW = m_impl->x11.DisplayWidthMM(m_impl->display, m_impl->screen);
        if (pxW > 320 && mmW > 50) {
            m_impl->pixelsPerMm = keel::as<keel::f32>(pxW) / keel::as<keel::f32>(mmW);
        } else {
            m_impl->pixelsPerMm = (96.0f * m_impl->dpiScale) / 25.4f;
        }
    }

    // 1. Initialize EGL display and pick config FIRST so the X11 window visual matches EGL_NATIVE_VISUAL_ID
    const bool useEgl = (desc.graphicsApi != GraphicsApi::None);
    if (useEgl) {
        if (!m_impl->egl.InitDisplayAndConfig(
                linux_detail::LinuxEglContext::kPlatformX11Khr,
                m_impl->display,
                desc.graphicsApi,
                /*alphaBits=*/0
            )) {
            m_impl->DestroyAll();
            return false;
        }
    }

    const Window root = m_impl->x11.RootWindow(m_impl->display, m_impl->screen);
    const X11Long eventMask =
        ExposureMask | StructureNotifyMask | PropertyChangeMask |
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask;

    // 2. Create X11 window with matching EGL visual, background_pixmap = None, and NorthWestGravity
    //    (prevents X11 / XWayland from clearing the window to black on every resize step).
    if (m_impl->x11.CreateWindow) {
        Visual* targetVisual = m_impl->x11.DefaultVisual ? m_impl->x11.DefaultVisual(m_impl->display, m_impl->screen) : nullptr;
        keel::i32 targetDepth = m_impl->x11.DefaultDepth ? m_impl->x11.DefaultDepth(m_impl->display, m_impl->screen) : CopyFromParent;

        XVisualInfo* matchedVis = nullptr;
        const keel::i32 eglVisualId = m_impl->egl.GetNativeVisualId();
        if (useEgl && eglVisualId != 0 && m_impl->x11.GetVisualInfo) {
            XVisualInfo tmpl{};
            tmpl.visualid = keel::as<VisualID>(eglVisualId);
            keel::i32 count = 0;
            matchedVis = m_impl->x11.GetVisualInfo(m_impl->display, VisualIDMask, &tmpl, &count);
            if (matchedVis && count > 0) {
                targetVisual = matchedVis->visual;
                targetDepth = matchedVis->depth;
            }
        }

        XSetWindowAttributes swa{};
        X11Card mask = CWBackPixmap | CWBorderPixel | CWBitGravity | CWEventMask;
        swa.background_pixmap = kX11None;
        swa.border_pixel = 0u;
        swa.bit_gravity = NorthWestGravity;
        swa.event_mask = eventMask;

        if (targetVisual && m_impl->x11.CreateColormap) {
            m_impl->colormap = m_impl->x11.CreateColormap(m_impl->display, root, targetVisual, AllocNone);
            if (m_impl->colormap != 0) {
                swa.colormap = m_impl->colormap;
                mask |= CWColormap;
            }
        }

        m_impl->window = m_impl->x11.CreateWindow(
            m_impl->display,
            root,
            0,
            0,
            keel::as<keel::u32>(m_impl->width),
            keel::as<keel::u32>(m_impl->height),
            0,
            targetDepth,
            InputOutput,
            targetVisual ? targetVisual : reinterpret_cast<Visual*>(CopyFromParent),
            mask,
            &swa
        );

        if (matchedVis && m_impl->x11.Free) {
            m_impl->x11.Free(matchedVis);
        }
    } else {
        m_impl->window = m_impl->x11.CreateSimpleWindow(
            m_impl->display,
            root,
            0,
            0,
            keel::as<keel::u32>(m_impl->width),
            keel::as<keel::u32>(m_impl->height),
            0,
            0,
            0
        );
        if (m_impl->window != 0) {
            m_impl->x11.SelectInput(m_impl->display, m_impl->window, eventMask);
        }
    }

    if (m_impl->window == 0) {
        m_impl->DestroyAll();
        return false;
    }

    m_impl->wmProtocols = m_impl->x11.InternAtom(m_impl->display, "WM_PROTOCOLS", False);
    m_impl->wmDeleteWindow = m_impl->x11.InternAtom(m_impl->display, "WM_DELETE_WINDOW", False);
    m_impl->atomNetWmSyncRequest = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_SYNC_REQUEST", False);
    m_impl->atomNetWmSyncCounter = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_SYNC_REQUEST_COUNTER", False);
    m_impl->atomClipboard = m_impl->x11.InternAtom(m_impl->display, "CLIPBOARD", False);
    m_impl->atomUtf8String = m_impl->x11.InternAtom(m_impl->display, "UTF8_STRING", False);
    m_impl->atomTargets = m_impl->x11.InternAtom(m_impl->display, "TARGETS", False);
    m_impl->atomClipboardProp = m_impl->x11.InternAtom(m_impl->display, "GUIKIT_CLIPBOARD", False);
    m_impl->atomNetWmState = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE", False);
    m_impl->atomNetWmStateMaxH = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    m_impl->atomNetWmStateMaxV = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    // 3. Register EWMH _NET_WM_SYNC_REQUEST (basic + extended counters) for tear-free WM resize sync
    m_impl->InitXSyncCounters();
    Atom protocols[2]{m_impl->wmDeleteWindow, m_impl->atomNetWmSyncRequest};
    const keel::i32 protocolCount = (m_impl->hasXSync && m_impl->atomNetWmSyncRequest != kX11None) ? 2 : 1;
    m_impl->x11.SetWMProtocols(m_impl->display, m_impl->window, protocols, protocolCount);

    const keel::String titleStr = keel::String::from(desc.title.as_ref());
    m_impl->x11.StoreName(m_impl->display, m_impl->window, titleStr.c_str());

    if (m_impl->x11.ChangeProperty) {
        const Atom netWmName = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_NAME", False);
        const Atom utf8Str = m_impl->x11.InternAtom(m_impl->display, "UTF8_STRING", False);
        if (netWmName != kX11None && utf8Str != kX11None) {
            m_impl->x11.ChangeProperty(
                m_impl->display,
                m_impl->window,
                netWmName,
                utf8Str,
                8,
                PropModeReplace,
                reinterpret_cast<const keel::u8*>(titleStr.c_str()),
                keel::as<keel::i32>(titleStr.size())
            );
        }
    }

    if (m_impl->x11.AllocSizeHints && m_impl->x11.SetWMNormalHints && m_impl->x11.Free) {
        XSizeHints* hints = m_impl->x11.AllocSizeHints();
        if (hints) {
            hints->flags = PMinSize;
            hints->min_width = desc.minWidth;
            hints->min_height = desc.minHeight;
            if (!desc.resizable) {
                hints->flags |= PMaxSize;
                hints->max_width = m_impl->width;
                hints->max_height = m_impl->height;
            }
            m_impl->x11.SetWMNormalHints(m_impl->display, m_impl->window, hints);
            m_impl->x11.Free(hints);
        }
    }

    if (m_impl->x11.OpenIM && m_impl->x11.CreateIC) {
        m_impl->xim = m_impl->x11.OpenIM(m_impl->display, nullptr, nullptr, nullptr);
        if (m_impl->xim) {
            m_impl->xic = m_impl->x11.CreateIC(
                m_impl->xim,
                XNInputStyle,
                XIMPreeditNothing | XIMStatusNothing,
                XNClientWindow,
                m_impl->window,
                nullptr
            );
        }
    }

    if (useEgl) {
        if (!m_impl->egl.CreateSurfaceAndContext(keel::as<keel::usize>(m_impl->window), m_impl->vsync ? 1 : 0)) {
            m_impl->DestroyAll();
            return false;
        }
    }

    m_impl->ApplyWindowIcon();
    m_impl->ApplyWindowStateFlags(desc);

    m_impl->wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    Impl* rawImpl = m_impl.get();
    ui.SetClipboardCallbacks(
        [rawImpl]() -> keel::String { return rawImpl->GetSystemClipboard(); },
        [rawImpl](keel::StringRef txt) { rawImpl->SetSystemClipboard(txt); }
    );

    m_impl->x11.MapWindow(m_impl->display, m_impl->window);
    m_impl->x11.Flush(m_impl->display);
    m_impl->alive = true;
    return true;
}

void X11PlatformWindow::Destroy() noexcept {
    if (m_impl) {
        m_impl->DestroyAll();
    }
}

bool X11PlatformWindow::PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) {
    if (!m_impl || !m_impl->alive || !m_impl->display) {
        return false;
    }

    auto drainQueue = [&]() -> bool {
        bool any = false;
        while (m_impl->x11.Pending(m_impl->display) > 0) {
            XEvent ev{};
            m_impl->x11.NextEvent(m_impl->display, &ev);
            m_impl->DispatchEvent(ev, ui);
            any = true;
            if (m_impl->closeRequested) {
                return true;
            }
        }
        return any;
    };

    const bool hadEvents = drainQueue();
    if (m_impl->closeRequested) {
        return false;
    }

    const bool shouldSleep = !hadEvents && !m_impl->hasPendingSync && sleepTimeoutMs != 0 &&
        (m_impl->minimized || (reactiveMode && !ui.NeedsRepaint()));
    if (shouldSleep) {
        const keel::i32 x11Fd = m_impl->x11.ConnectionNumber(m_impl->display);
        struct pollfd fds[2]{};
        fds[0].fd = x11Fd;
        fds[0].events = POLLIN;
        nfds_t nfds = 1;
        if (m_impl->wakeFd >= 0) {
            fds[1].fd = m_impl->wakeFd;
            fds[1].events = POLLIN;
            nfds = 2;
        }

        const keel::i32 timeout = (sleepTimeoutMs < 0) ? -1 : sleepTimeoutMs;
        const keel::i32 ready = ::poll(fds, nfds, timeout);
        if (ready > 0) {
            if (nfds == 2 && (fds[1].revents & POLLIN) != 0) {
                keel::u64 drained = 0;
                const keel::isize ignored = ::read(m_impl->wakeFd, &drained, sizeof(drained));
                (void)ignored;
            }
            (void)drainQueue();
            if (m_impl->closeRequested) {
                return false;
            }
        }
    }

    if (m_impl->resizeCoalescedDirty) {
        m_impl->resizeCoalescedDirty = false;
        if (m_impl->callbacks.onResize) {
            m_impl->callbacks.onResize(m_impl->width, m_impl->height);
        }
        ui.InvalidateAll();
    }

    return !m_impl->closeRequested;
}

void X11PlatformWindow::WakeUp() noexcept {
    if (m_impl && m_impl->wakeFd >= 0) {
        const keel::u64 one = 1u;
        const keel::isize ignored = ::write(m_impl->wakeFd, &one, sizeof(one));
        (void)ignored;
    }
}

bool X11PlatformWindow::MakeContextCurrent() noexcept {
    return m_impl ? m_impl->egl.MakeCurrent() : true;
}

void X11PlatformWindow::BeginFrameTarget(ICanvas& /*canvas*/, Context& /*ui*/) {
    if (m_impl) {
        m_impl->NotifySyncFrameBegin();
    }
}

void X11PlatformWindow::SwapBuffers() noexcept {
    if (!m_impl) return;
    m_impl->egl.SwapBuffers();
    m_impl->NotifySyncFrameComplete();
}

GlProcAddressFn X11PlatformWindow::GetGlProcAddressFn() const noexcept {
    return m_impl ? m_impl->egl.GetProcAddressFn() : nullptr;
}

keel::i32 X11PlatformWindow::GetFramebufferWidth() const noexcept {
    return m_impl ? m_impl->width : 0;
}

keel::i32 X11PlatformWindow::GetFramebufferHeight() const noexcept {
    return m_impl ? m_impl->height : 0;
}

keel::f32 X11PlatformWindow::GetDpiScale() const noexcept {
    return m_impl ? m_impl->dpiScale : 1.0f;
}

keel::f32 X11PlatformWindow::GetDisplayPixelsPerMm() const noexcept {
    return m_impl ? m_impl->pixelsPerMm : (96.0f / 25.4f);
}

bool X11PlatformWindow::IsMinimized() const noexcept {
    return m_impl ? m_impl->minimized : false;
}

bool X11PlatformWindow::IsLiveResizeFrame() const noexcept {
    return m_impl ? m_impl->hasPendingSync : false;
}

bool X11PlatformWindow::IsVsyncEnabled() const noexcept {
    return m_impl ? m_impl->vsync : true;
}

void X11PlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    if (!m_impl) return;
    m_impl->vsync = enabled;
    m_impl->egl.SetSwapInterval(enabled ? 1 : 0);
}

void X11PlatformWindow::SetCursor(CursorType cursor) noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0 || cursor == m_impl->activeCursor) {
        return;
    }
    m_impl->activeCursor = cursor;
    if (!m_impl->resizeCursorOverride) {
        m_impl->ApplyRawCursor(cursor);
    }
}

void X11PlatformWindow::OpenUrl(keel::StringRef url) const noexcept {
    linux_detail::OpenLinuxUrl(url);
}

void X11PlatformWindow::SetTitle(keel::StringRef title) noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0) return;
    const keel::String titleStr = keel::String::from(title);
    if (m_impl->x11.StoreName) {
        m_impl->x11.StoreName(m_impl->display, m_impl->window, titleStr.c_str());
    }
    if (m_impl->x11.ChangeProperty) {
        const Atom netWmName = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_NAME", False);
        const Atom utf8Str = m_impl->x11.InternAtom(m_impl->display, "UTF8_STRING", False);
        if (netWmName != kX11None && utf8Str != kX11None) {
            m_impl->x11.ChangeProperty(
                m_impl->display,
                m_impl->window,
                netWmName,
                utf8Str,
                8,
                PropModeReplace,
                reinterpret_cast<const keel::u8*>(titleStr.c_str()),
                keel::as<keel::i32>(titleStr.size())
            );
        }
    }
    m_impl->x11.Flush(m_impl->display);
}

void X11PlatformWindow::Minimize() noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0) return;
    if (m_impl->x11.IconifyWindow) {
        (void)m_impl->x11.IconifyWindow(m_impl->display, m_impl->window, m_impl->screen);
        m_impl->x11.Flush(m_impl->display);
    }
}

void X11PlatformWindow::Maximize() noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0) return;
    const Atom maxH = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    const Atom maxV = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    SendX11NetWmState(m_impl->x11, m_impl->display, m_impl->screen, m_impl->window, 1, maxH, maxV);
    m_impl->maximized = true;
}

void X11PlatformWindow::Restore() noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0) return;
    const Atom maxH = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    const Atom maxV = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    SendX11NetWmState(m_impl->x11, m_impl->display, m_impl->screen, m_impl->window, 0, maxH, maxV);
    m_impl->maximized = false;
}

void X11PlatformWindow::RequestClose() noexcept {
    if (!m_impl) return;
    if (!m_impl->callbacks.onCloseRequested || m_impl->callbacks.onCloseRequested()) {
        m_impl->closeRequested = true;
    }
}

bool X11PlatformWindow::IsMaximized() const noexcept {
    return m_impl ? m_impl->maximized : false;
}

bool X11PlatformWindow::IsFocused() const noexcept {
    return m_impl ? m_impl->focused : true;
}

bool X11PlatformWindow::SupportsCustomTitleBar() const noexcept {
    return true;
}

bool X11PlatformWindow::IsCustomTitleBar() const noexcept {
    return m_impl ? m_impl->customTitleBar : false;
}

bool X11PlatformWindow::SetCustomTitleBar(bool enabled) noexcept {
    if (!m_impl || !m_impl->display || m_impl->window == 0 || !m_impl->x11.ChangeProperty) {
        return false;
    }
    if (m_impl->customTitleBar == enabled) {
        return true;
    }
    m_impl->customTitleBar = enabled;
    const Atom motifHintsAtom = m_impl->x11.InternAtom(m_impl->display, "_MOTIF_WM_HINTS", False);
    if (motifHintsAtom != kX11None) {
        struct MotifWmHints {
            X11Card flags;
            X11Card functions;
            X11Card decorations;
            X11Long inputMode;
            X11Card status;
        };
        const MotifWmHints hints{2u, 0u, enabled ? 0u : 1u, 0, 0u};
        (void)m_impl->x11.ChangeProperty(
            m_impl->display,
            m_impl->window,
            motifHintsAtom,
            motifHintsAtom,
            32,
            PropModeReplace,
            reinterpret_cast<const keel::u8*>(&hints),
            5
        );
        m_impl->x11.Flush(m_impl->display);
    }
    if (!enabled && m_impl->resizeCursorOverride) {
        m_impl->resizeCursorOverride = false;
        m_impl->ApplyRawCursor(m_impl->activeCursor);
    }
    return true;
}

void X11PlatformWindow::StartSystemMove() noexcept {
    if (!m_impl) return;
    m_impl->StartMoveResizeAction(8); // _NET_WM_MOVERESIZE_MOVE
    if (m_impl->activeUi) {
        m_impl->activeUi->ResetMouseButtonsAfterModalDialog();
    }
}

void X11PlatformWindow::StartSystemResize(WindowHitRegion edge) noexcept {
    if (!m_impl) return;
    CursorType ignoredCur = CursorType::Default;
    X11Long dir = -1;
    if (Impl::MapResizeRegionToX11(edge, ignoredCur, dir)) {
        m_impl->StartMoveResizeAction(dir);
        if (m_impl->activeUi) {
            m_impl->activeUi->ResetMouseButtonsAfterModalDialog();
        }
    }
}

bool X11PlatformWindow::SupportsFileDialogs() const noexcept {
    return true;
}

FileDialogResult X11PlatformWindow::ShowFileDialog(const FileDialogOptions& opts) {
    return linux_detail::ShowLinuxFileDialog(opts);
}

MessageBoxChoice X11PlatformWindow::ShowMessageBox(const MessageBoxOptions& opts) {
    return linux_detail::ShowLinuxMessageBox(opts);
}

keel::StringRef X11PlatformWindow::GetPlatformName() const noexcept {
    return "X11 + EGL";
}

#else // !GUIKIT_NATIVE_COMPILE_X11

struct X11PlatformWindow::Impl {};

X11PlatformWindow::X11PlatformWindow() noexcept = default;
X11PlatformWindow::~X11PlatformWindow() = default;
bool X11PlatformWindow::IsRuntimeAvailable() noexcept { return false; }
bool X11PlatformWindow::Create(const PlatformWindowDesc&, Context&, WindowHostCallbacks) { return false; }
void X11PlatformWindow::Destroy() noexcept {}
bool X11PlatformWindow::PumpEvents(Context&, bool, keel::i32) { return false; }
void X11PlatformWindow::WakeUp() noexcept {}
bool X11PlatformWindow::MakeContextCurrent() noexcept { return false; }
void X11PlatformWindow::BeginFrameTarget(ICanvas&, Context&) {}
void X11PlatformWindow::SwapBuffers() noexcept {}
GlProcAddressFn X11PlatformWindow::GetGlProcAddressFn() const noexcept { return nullptr; }
keel::i32 X11PlatformWindow::GetFramebufferWidth() const noexcept { return 0; }
keel::i32 X11PlatformWindow::GetFramebufferHeight() const noexcept { return 0; }
keel::f32 X11PlatformWindow::GetDpiScale() const noexcept { return 1.0f; }
keel::f32 X11PlatformWindow::GetDisplayPixelsPerMm() const noexcept { return 96.0f / 25.4f; }
bool X11PlatformWindow::IsMinimized() const noexcept { return false; }
bool X11PlatformWindow::IsLiveResizeFrame() const noexcept { return false; }
bool X11PlatformWindow::IsVsyncEnabled() const noexcept { return true; }
void X11PlatformWindow::SetVsyncEnabled(bool) noexcept {}
void X11PlatformWindow::SetCursor(CursorType) noexcept {}
void X11PlatformWindow::OpenUrl(keel::StringRef) const noexcept {}
void X11PlatformWindow::SetTitle(keel::StringRef) noexcept {}
void X11PlatformWindow::Minimize() noexcept {}
void X11PlatformWindow::Maximize() noexcept {}
void X11PlatformWindow::Restore() noexcept {}
void X11PlatformWindow::RequestClose() noexcept {}
bool X11PlatformWindow::IsMaximized() const noexcept { return false; }
bool X11PlatformWindow::IsFocused() const noexcept { return true; }
bool X11PlatformWindow::SupportsCustomTitleBar() const noexcept { return false; }
bool X11PlatformWindow::IsCustomTitleBar() const noexcept { return false; }
bool X11PlatformWindow::SetCustomTitleBar(bool) noexcept { return false; }
void X11PlatformWindow::StartSystemMove() noexcept {}
void X11PlatformWindow::StartSystemResize(WindowHitRegion) noexcept {}
bool X11PlatformWindow::SupportsFileDialogs() const noexcept { return false; }
FileDialogResult X11PlatformWindow::ShowFileDialog(const FileDialogOptions&) { return {.accepted = false, .supported = false, .paths = {}}; }
MessageBoxChoice X11PlatformWindow::ShowMessageBox(const MessageBoxOptions&) { return MessageBoxChoice::Cancel; }
keel::StringRef X11PlatformWindow::GetPlatformName() const noexcept { return "X11 (Unavailable)"; }

#endif // GUIKIT_NATIVE_COMPILE_X11

} // namespace guikit::native
