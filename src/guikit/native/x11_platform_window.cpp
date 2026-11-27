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

#if defined(__linux__) && !defined(__ANDROID__) && __has_include(<X11/Xlib.h>)
#define GUIKIT_NATIVE_COMPILE_X11 1
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
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

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLNativeDisplayType = void*;
using EGLNativeWindowType = unsigned long; // keel-lint: ignore
using EGLBoolean = keel::u32;
using EGLint = keel::i32;
using EGLenum = keel::u32;
using EglProcAddressRaw = void*(*)(const char*);

static constexpr EGLint kEglFalse = 0;
static constexpr EGLint kEglTrue = 1;
static constexpr EGLint kEglNone = 0x3038;
static constexpr EGLint kEglRedSize = 0x3024;
static constexpr EGLint kEglGreenSize = 0x3023;
static constexpr EGLint kEglBlueSize = 0x3022;
static constexpr EGLint kEglAlphaSize = 0x3021;
static constexpr EGLint kEglStencilSize = 0x3026;
static constexpr EGLint kEglSamples = 0x3031;
static constexpr EGLint kEglSampleBuffers = 0x3032;
static constexpr EGLint kEglSurfaceType = 0x3033;
static constexpr EGLint kEglWindowBit = 0x0004;
static constexpr EGLint kEglRenderableType = 0x3040;
static constexpr EGLint kEglOpenGlEs2Bit = 0x0004;
static constexpr EGLint kEglOpenGlEs3Bit = 0x0040;
static constexpr EGLint kEglOpenGlBit = 0x0008;
static constexpr EGLint kEglContextClientVersion = 0x3098;
static constexpr EGLenum kEglOpenGlEsApi = 0x30A0;
static constexpr EGLenum kEglOpenGlApi = 0x30A2;
static constexpr EGLenum kEglPlatformX11Khr = 0x31D5;

struct X11Api {
    keel::DynLib lib;

    using PfnXOpenDisplay = Display*(*)(const char*);
    using PfnXCloseDisplay = keel::i32(*)(Display*);
    using PfnXDefaultScreen = keel::i32(*)(Display*);
    using PfnXRootWindow = Window(*)(Display*, keel::i32);
    using PfnXCreateSimpleWindow = Window(*)(Display*, Window, keel::i32, keel::i32, keel::u32, keel::u32, keel::u32, unsigned long, unsigned long); // keel-lint: ignore
    using PfnXDestroyWindow = keel::i32(*)(Display*, Window);
    using PfnXSelectInput = keel::i32(*)(Display*, Window, long); // keel-lint: ignore
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
    PfnXRootWindow RootWindow{nullptr};
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
        RootWindow = lib.symbol<PfnXRootWindow>("XRootWindow");
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
               CreateSimpleWindow && DestroyWindow && SelectInput && MapWindow &&
               Flush && Pending && NextEvent && InternAtom && SetWMProtocols &&
               StoreName && LookupKeysym && LookupString && ConnectionNumber;
    }
};

struct EglApi {
    keel::DynLib lib;

    using PfnGetDisplay = EGLDisplay(*)(EGLNativeDisplayType);
    using PfnGetPlatformDisplay = EGLDisplay(*)(EGLenum, void*, const keel::isize*);
    using PfnGetPlatformDisplayExt = EGLDisplay(*)(EGLenum, void*, const EGLint*);
    using PfnInitialize = EGLBoolean(*)(EGLDisplay, EGLint*, EGLint*);
    using PfnTerminate = EGLBoolean(*)(EGLDisplay);
    using PfnBindApi = EGLBoolean(*)(EGLenum);
    using PfnChooseConfig = EGLBoolean(*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    using PfnCreateWindowSurface = EGLSurface(*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
    using PfnDestroySurface = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnCreateContext = EGLContext(*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using PfnDestroyContext = EGLBoolean(*)(EGLDisplay, EGLContext);
    using PfnMakeCurrent = EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using PfnSwapBuffers = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnSwapInterval = EGLBoolean(*)(EGLDisplay, EGLint);
    using PfnGetProcAddress = EglProcAddressRaw(*)(const char*);

    PfnGetDisplay GetDisplay{nullptr};
    PfnGetPlatformDisplay GetPlatformDisplay{nullptr};
    PfnGetPlatformDisplayExt GetPlatformDisplayEXT{nullptr};
    PfnInitialize Initialize{nullptr};
    PfnTerminate Terminate{nullptr};
    PfnBindApi BindAPI{nullptr};
    PfnChooseConfig ChooseConfig{nullptr};
    PfnCreateWindowSurface CreateWindowSurface{nullptr};
    PfnDestroySurface DestroySurface{nullptr};
    PfnCreateContext CreateContext{nullptr};
    PfnDestroyContext DestroyContext{nullptr};
    PfnMakeCurrent MakeCurrent{nullptr};
    PfnSwapBuffers SwapBuffers{nullptr};
    PfnSwapInterval SwapInterval{nullptr};
    PfnGetProcAddress GetProcAddress{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libEGL.so.1");
        if (!opened) opened = keel::DynLib::open("libEGL.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        GetDisplay = lib.symbol<PfnGetDisplay>("eglGetDisplay");
        GetPlatformDisplay = lib.symbol<PfnGetPlatformDisplay>("eglGetPlatformDisplay");
        Initialize = lib.symbol<PfnInitialize>("eglInitialize");
        Terminate = lib.symbol<PfnTerminate>("eglTerminate");
        BindAPI = lib.symbol<PfnBindApi>("eglBindAPI");
        ChooseConfig = lib.symbol<PfnChooseConfig>("eglChooseConfig");
        CreateWindowSurface = lib.symbol<PfnCreateWindowSurface>("eglCreateWindowSurface");
        DestroySurface = lib.symbol<PfnDestroySurface>("eglDestroySurface");
        CreateContext = lib.symbol<PfnCreateContext>("eglCreateContext");
        DestroyContext = lib.symbol<PfnDestroyContext>("eglDestroyContext");
        MakeCurrent = lib.symbol<PfnMakeCurrent>("eglMakeCurrent");
        SwapBuffers = lib.symbol<PfnSwapBuffers>("eglSwapBuffers");
        SwapInterval = lib.symbol<PfnSwapInterval>("eglSwapInterval");
        GetProcAddress = lib.symbol<PfnGetProcAddress>("eglGetProcAddress");
        if (GetProcAddress) {
            void* rawExt = reinterpret_cast<void*>(GetProcAddress("eglGetPlatformDisplayEXT"));
            if (rawExt) {
                keel::mem_copy(&GetPlatformDisplayEXT, &rawExt, sizeof(GetPlatformDisplayEXT));
            }
        }

        return GetDisplay && Initialize && Terminate && BindAPI && ChooseConfig &&
               CreateWindowSurface && DestroySurface && CreateContext &&
               DestroyContext && MakeCurrent && SwapBuffers && GetProcAddress;
    }
};

EglApi::PfnGetProcAddress g_activeEglGetProcAddress = nullptr;

void* EglProcLoaderThunk(const char* procName) {
    if (!g_activeEglGetProcAddress || !procName) {
        return nullptr;
    }
    return reinterpret_cast<void*>(g_activeEglGetProcAddress(procName));
}

[[nodiscard]] Key MapX11KeySym(KeySym ks) noexcept {
    switch (ks) {
        case XK_BackSpace: return Key::Backspace;
        case XK_Delete:
        case XK_KP_Delete: return Key::Delete;
        case XK_Return:
        case XK_KP_Enter:  return Key::Enter;
        case XK_Escape:    return Key::Escape;
        case XK_Tab:
        case XK_ISO_Left_Tab: return Key::Tab;
        case XK_space:     return Key::Space;
        case XK_Left:
        case XK_KP_Left:   return Key::Left;
        case XK_Right:
        case XK_KP_Right:  return Key::Right;
        case XK_Home:
        case XK_KP_Home:   return Key::Home;
        case XK_End:
        case XK_KP_End:    return Key::End;
        case XK_Up:
        case XK_KP_Up:     return Key::Up;
        case XK_Down:
        case XK_KP_Down:   return Key::Down;
        case XK_Page_Up:
        case XK_KP_Page_Up: return Key::PageUp;
        case XK_Page_Down:
        case XK_KP_Page_Down: return Key::PageDown;
        case XK_a: case XK_A: return Key::A;
        case XK_c: case XK_C: return Key::C;
        case XK_d: case XK_D: return Key::D;
        case XK_f: case XK_F: return Key::F;
        case XK_g: case XK_G: return Key::G;
        case XK_h: case XK_H: return Key::H;
        case XK_k: case XK_K: return Key::K;
        case XK_v: case XK_V: return Key::V;
        case XK_x: case XK_X: return Key::X;
        case XK_y: case XK_Y: return Key::Y;
        case XK_z: case XK_Z: return Key::Z;
        case XK_slash: case XK_KP_Divide: return Key::Slash;
        case XK_plus: case XK_equal: case XK_KP_Add: return Key::Plus;
        case XK_minus: case XK_KP_Subtract: return Key::Minus;
        case XK_0: case XK_KP_0: return Key::Zero;
        case XK_F3:  return Key::F3;
        case XK_F5:  return Key::F5;
        case XK_F9:  return Key::F9;
        case XK_F10: return Key::F10;
        case XK_F11: return Key::F11;
        default: return Key::None;
    }
}

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
            // _NET_WM_ICON expects packed 0xAARRGGBB in each 32-bit CARDINAL element
            const keel::u32 argb = (a << 24) | (r << 16) | (g << 8) | b;
            outData.push_back(keel::as<X11Card>(argb));
        }
    }
}

} // namespace

struct X11PlatformWindow::Impl {
    X11Api x11{};
    EglApi egl{};

    Display* display{nullptr};
    keel::i32 screen{0};
    Window window{0};
    Atom wmDeleteWindow{0};
    Atom atomClipboard{0};
    Atom atomUtf8String{0};
    Atom atomTargets{0};
    Atom atomClipboardProp{0};
    Atom atomNetWmState{0};
    Atom atomNetWmStateMaxH{0};
    Atom atomNetWmStateMaxV{0};
    XIM xim{nullptr};
    XIC xic{nullptr};

    EGLDisplay eglDisplay{nullptr};
    EGLSurface eglSurface{nullptr};
    EGLContext eglContext{nullptr};

    keel::i32 wakeFd{-1};
    keel::i32 width{1380};
    keel::i32 height{800};
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
    keel::String clipboardText;

    void DestroyAll() noexcept {
        alive = false;
        if (eglDisplay && egl.MakeCurrent) {
            (void)egl.MakeCurrent(eglDisplay, nullptr, nullptr, nullptr);
        }
        if (eglDisplay && eglContext && egl.DestroyContext) {
            (void)egl.DestroyContext(eglDisplay, eglContext);
            eglContext = nullptr;
        }
        if (eglDisplay && eglSurface && egl.DestroySurface) {
            (void)egl.DestroySurface(eglDisplay, eglSurface);
            eglSurface = nullptr;
        }
        if (eglDisplay && egl.Terminate) {
            (void)egl.Terminate(eglDisplay);
            eglDisplay = nullptr;
        }
        if (display) {
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
            if (x11.CloseDisplay) {
                x11.CloseDisplay(display);
            }
            display = nullptr;
        }
        if (wakeFd >= 0) {
            ::close(wakeFd);
            wakeFd = -1;
        }
        callbacks = {};
    }

    bool ChooseEglConfig(EGLint renderableBit, EGLConfig& outCfg) noexcept {
        struct Profile {
            EGLint stencilBits;
            EGLint sampleBuffers;
            EGLint samples;
        };
        static constexpr Profile kProfiles[] = {
            {8, 0, 0}, // 8-bit stencil, 0 MSAA (all UI primitives use analytical SDF AA; avoids 4x fill-rate overhead)
            {0, 0, 0}  // Minimal RGBA8 fallback
        };
        for (const Profile& prof : kProfiles) {
            EGLint attribs[24]{};
            keel::usize idx = 0u;
            attribs[idx++] = kEglSurfaceType;    attribs[idx++] = kEglWindowBit;
            attribs[idx++] = kEglRenderableType; attribs[idx++] = renderableBit;
            attribs[idx++] = kEglRedSize;        attribs[idx++] = 8;
            attribs[idx++] = kEglGreenSize;      attribs[idx++] = 8;
            attribs[idx++] = kEglBlueSize;       attribs[idx++] = 8;
            attribs[idx++] = kEglAlphaSize;      attribs[idx++] = 8;
            if (prof.stencilBits > 0) {
                attribs[idx++] = kEglStencilSize; attribs[idx++] = prof.stencilBits;
            }
            if (prof.sampleBuffers > 0) {
                attribs[idx++] = kEglSampleBuffers; attribs[idx++] = prof.sampleBuffers;
                attribs[idx++] = kEglSamples;       attribs[idx++] = prof.samples;
            }
            attribs[idx++] = kEglNone;

            EGLConfig cfg = nullptr;
            EGLint numConfigs = 0;
            if (egl.ChooseConfig(eglDisplay, attribs, &cfg, 1, &numConfigs) == kEglTrue && numConfigs > 0 && cfg) {
                outCfg = cfg;
                return true;
            }
        }
        return false;
    }

    bool InitEgl(GraphicsApi requestedApi) noexcept {
        if (!egl.Load()) {
            keel::eprintln("[X11PlatformWindow] Failed to load libEGL.so.1");
            return false;
        }
        if (egl.GetPlatformDisplay) {
            eglDisplay = egl.GetPlatformDisplay(kEglPlatformX11Khr, display, nullptr);
        } else if (egl.GetPlatformDisplayEXT) {
            eglDisplay = egl.GetPlatformDisplayEXT(kEglPlatformX11Khr, display, nullptr);
        }
        if (!eglDisplay) {
            eglDisplay = egl.GetDisplay(static_cast<EGLNativeDisplayType>(display));
        }
        if (!eglDisplay) {
            keel::eprintln("[X11PlatformWindow] eglGetDisplay() failed");
            return false;
        }
        EGLint major = 0, minor = 0;
        if (egl.Initialize(eglDisplay, &major, &minor) == kEglFalse) {
            keel::eprintln("[X11PlatformWindow] eglInitialize() failed");
            eglDisplay = nullptr;
            return false;
        }

        const bool tryEs3First = (requestedApi != GraphicsApi::OpenGL3);
        bool boundEs = false;
        EGLConfig cfg = nullptr;

        if (tryEs3First && egl.BindAPI(kEglOpenGlEsApi) == kEglTrue) {
            if (ChooseEglConfig(kEglOpenGlEs3Bit, cfg) || ChooseEglConfig(kEglOpenGlEs2Bit, cfg)) {
                boundEs = true;
            }
        }

        if (!boundEs) {
            if (egl.BindAPI(kEglOpenGlApi) == kEglFalse) {
                keel::eprintln("[X11PlatformWindow] Failed to bind EGL OpenGL ES or Desktop OpenGL API");
                return false;
            }
            if (!ChooseEglConfig(kEglOpenGlBit, cfg)) {
                keel::eprintln("[X11PlatformWindow] eglChooseConfig() failed");
                return false;
            }
        }

        eglSurface = egl.CreateWindowSurface(eglDisplay, cfg, static_cast<EGLNativeWindowType>(window), nullptr);
        if (!eglSurface) {
            keel::eprintln("[X11PlatformWindow] eglCreateWindowSurface() failed");
            return false;
        }

        if (boundEs) {
            const EGLint ctxAttribs[] = {kEglContextClientVersion, 3, kEglNone};
            eglContext = egl.CreateContext(eglDisplay, cfg, nullptr, ctxAttribs);
        } else {
            const EGLint ctxAttribs[] = {kEglNone};
            eglContext = egl.CreateContext(eglDisplay, cfg, nullptr, ctxAttribs);
        }
        if (!eglContext) {
            keel::eprintln("[X11PlatformWindow] eglCreateContext() failed");
            return false;
        }

        g_activeEglGetProcAddress = egl.GetProcAddress;
        if (egl.MakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == kEglFalse) {
            keel::eprintln("[X11PlatformWindow] eglMakeCurrent() failed");
            return false;
        }
        if (egl.SwapInterval) {
            (void)egl.SwapInterval(eglDisplay, vsync ? 1 : 0);
        }
        return true;
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
                        262144, // up to 1 MiB (in 32-bit longs)
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
                const MotifWmHints hints{2u, 0u, 0u, 0, 0u}; // MWM_HINTS_DECORATIONS = 2, decorations = 0
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
            case ClientMessage:
                if (keel::as<Atom>(ev.xclient.data.l[0]) == wmDeleteWindow) {
                    if (!callbacks.onCloseRequested || callbacks.onCloseRequested()) {
                        closeRequested = true;
                    }
                }
                break;

            case ConfigureNotify: {
                const keel::i32 nw = ev.xconfigure.width;
                const keel::i32 nh = ev.xconfigure.height;
                if (nw > 0 && nh > 0 && (nw != width || nh != height)) {
                    width = nw;
                    height = nh;
                    if (callbacks.onResize) {
                        callbacks.onResize(width, height);
                    }
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
                const Key logicalKey = MapX11KeySym(ks);
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
    EglApi egl;
    return x11.Load() && egl.Load();
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

    const Window root = m_impl->x11.RootWindow(m_impl->display, m_impl->screen);
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
    if (m_impl->window == 0) {
        m_impl->DestroyAll();
        return false;
    }

    const long eventMask = // keel-lint: ignore
        ExposureMask | StructureNotifyMask | PropertyChangeMask |
        KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | FocusChangeMask;
    m_impl->x11.SelectInput(m_impl->display, m_impl->window, eventMask);

    m_impl->wmDeleteWindow = m_impl->x11.InternAtom(m_impl->display, "WM_DELETE_WINDOW", False);
    m_impl->atomClipboard = m_impl->x11.InternAtom(m_impl->display, "CLIPBOARD", False);
    m_impl->atomUtf8String = m_impl->x11.InternAtom(m_impl->display, "UTF8_STRING", False);
    m_impl->atomTargets = m_impl->x11.InternAtom(m_impl->display, "TARGETS", False);
    m_impl->atomClipboardProp = m_impl->x11.InternAtom(m_impl->display, "GUIKIT_CLIPBOARD", False);
    m_impl->atomNetWmState = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE", False);
    m_impl->atomNetWmStateMaxH = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    m_impl->atomNetWmStateMaxV = m_impl->x11.InternAtom(m_impl->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    m_impl->x11.SetWMProtocols(m_impl->display, m_impl->window, &m_impl->wmDeleteWindow, 1);

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

    if (desc.graphicsApi != GraphicsApi::None) {
        if (!m_impl->InitEgl(desc.graphicsApi)) {
            m_impl->DestroyAll();
            return false;
        }
    }

    m_impl->ApplyWindowIcon();
    m_impl->ApplyWindowStateFlags(desc);

    m_impl->wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    // Bind X11 CLIPBOARD selection protocol callbacks (UTF8_STRING / STRING / TARGETS)
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

    // Drain any already-queued X11 events first
    bool hadEvents = false;
    while (m_impl->x11.Pending(m_impl->display) > 0) {
        XEvent ev{};
        m_impl->x11.NextEvent(m_impl->display, &ev);
        m_impl->DispatchEvent(ev, ui);
        hadEvents = true;
        if (m_impl->closeRequested) {
            return false;
        }
    }

    const bool shouldSleep = !hadEvents && sleepTimeoutMs != 0 &&
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
            while (m_impl->x11.Pending(m_impl->display) > 0) {
                XEvent ev{};
                m_impl->x11.NextEvent(m_impl->display, &ev);
                m_impl->DispatchEvent(ev, ui);
                if (m_impl->closeRequested) {
                    return false;
                }
            }
        }
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
    if (!m_impl || !m_impl->eglDisplay || !m_impl->eglContext || !m_impl->eglSurface) {
        return true;
    }
    return m_impl->egl.MakeCurrent(m_impl->eglDisplay, m_impl->eglSurface, m_impl->eglSurface, m_impl->eglContext) == kEglTrue;
}

void X11PlatformWindow::SwapBuffers() noexcept {
    if (m_impl && m_impl->eglDisplay && m_impl->eglSurface) {
        (void)m_impl->egl.SwapBuffers(m_impl->eglDisplay, m_impl->eglSurface);
    }
}

GlProcAddressFn X11PlatformWindow::GetGlProcAddressFn() const noexcept {
    return (m_impl && m_impl->egl.GetProcAddress) ? &EglProcLoaderThunk : nullptr;
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
    return false;
}

bool X11PlatformWindow::IsVsyncEnabled() const noexcept {
    return m_impl ? m_impl->vsync : true;
}

void X11PlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    if (!m_impl) return;
    m_impl->vsync = enabled;
    if (m_impl->eglDisplay && m_impl->egl.SwapInterval) {
        (void)m_impl->egl.SwapInterval(m_impl->eglDisplay, enabled ? 1 : 0);
    }
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
    if (url.empty()) return;
    const keel::String urlStr = keel::String::from(url);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("xdg-open", "xdg-open", urlStr.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
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

namespace {

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

struct ExecCaptureResult {
    bool launched{false};
    keel::i32 exitCode{-1};
    keel::String output;
};

ExecCaptureResult RunExternalDialogCommand(const keel::Array<keel::String>& args) {
    ExecCaptureResult res{};
    if (args.empty()) return res;

    int pipeFds[2]{-1, -1}; // keel-lint: ignore
    if (::pipe(pipeFds) != 0) {
        return res;
    }

    keel::Array<char*> argv;
    argv.reserve(args.size() + 1u);
    for (const keel::String& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return res;
    }
    if (pid == 0) {
        ::close(pipeFds[0]);
        (void)::dup2(pipeFds[1], STDOUT_FILENO);
        ::close(pipeFds[1]);
        const int devNull = ::open("/dev/null", O_WRONLY); // keel-lint: ignore
        if (devNull >= 0) {
            (void)::dup2(devNull, STDERR_FILENO);
            ::close(devNull);
        }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }

    ::close(pipeFds[1]);
    char buf[1024]{};
    while (true) {
        const keel::isize n = ::read(pipeFds[0], buf, sizeof(buf));
        if (n <= 0) break;
        res.output.append(keel::StringRef(buf, keel::as<keel::usize>(n)));
    }
    ::close(pipeFds[0]);

    int status = 0; // keel-lint: ignore
    (void)::waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        res.exitCode = WEXITSTATUS(status);
        res.launched = (res.exitCode != 127);
    }
    return res;
}

} // namespace

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
}

void X11PlatformWindow::StartSystemResize(WindowHitRegion edge) noexcept {
    if (!m_impl) return;
    CursorType ignoredCur = CursorType::Default;
    X11Long dir = -1;
    if (Impl::MapResizeRegionToX11(edge, ignoredCur, dir)) {
        m_impl->StartMoveResizeAction(dir);
    }
}

bool X11PlatformWindow::SupportsFileDialogs() const noexcept {
    return true;
}

FileDialogResult X11PlatformWindow::ShowFileDialog(const FileDialogOptions& opts) {
    FileDialogResult result{};
    result.supported = false;
    result.accepted = false;

    // 1. Try zenity first (GNOME/GTK ecosystems)
    {
        keel::Array<keel::String> args;
        args.push_back(keel::String("zenity"));
        args.push_back(keel::String("--file-selection"));
        if (!opts.title.empty()) {
            args.push_back(keel::String("--title"));
            args.push_back(keel::String::from(opts.title));
        }
        if (opts.kind == FileDialogKind::OpenFiles) {
            args.push_back(keel::String("--multiple"));
            args.push_back(keel::String("--separator=\n"));
        } else if (opts.kind == FileDialogKind::SaveFile) {
            args.push_back(keel::String("--save"));
            args.push_back(keel::String("--confirm-overwrite"));
        } else if (opts.kind == FileDialogKind::SelectFolder) {
            args.push_back(keel::String("--directory"));
        }
        if (!opts.defaultPath.empty() || !opts.defaultName.empty()) {
            keel::String fn = keel::String::from(opts.defaultPath);
            if (!opts.defaultName.empty()) {
                if (!fn.empty() && fn.back() != '/') fn.push_back('/');
                fn.append(opts.defaultName);
            }
            if (!fn.empty()) {
                args.push_back(keel::String("--filename"));
                args.push_back(keel::move(fn));
            }
        }
        if (opts.kind != FileDialogKind::SelectFolder) {
            for (const FileDialogFilter& f : opts.filters) {
                if (f.pattern.empty()) continue;
                keel::String spec = keel::String::from(f.name.empty() ? f.pattern : f.name);
                spec.append(" | ");
                for (keel::usize i = 0u; i < f.pattern.size(); ++i) {
                    spec.push_back(f.pattern[i] == ';' ? ' ' : f.pattern[i]);
                }
                args.push_back(keel::String("--file-filter"));
                args.push_back(keel::move(spec));
            }
        }

        const ExecCaptureResult exec = RunExternalDialogCommand(args);
        if (exec.launched) {
            result.supported = true;
            if (exec.exitCode == 0) {
                keel::usize pos = 0u;
                while (pos < exec.output.size()) {
                    keel::usize nl = exec.output.find('\n', pos);
                    if (nl == keel::StringRef::npos) nl = exec.output.size();
                    const keel::StringRef line = exec.output.substr(pos, nl - pos).trim();
                    if (!line.empty()) {
                        result.paths.push_back(keel::String::from(line));
                    }
                    pos = (nl < exec.output.size()) ? (nl + 1u) : exec.output.size();
                }
                result.accepted = !result.paths.empty();
            }
            return result;
        }
    }

    // 2. Fallback to kdialog (KDE/Qt ecosystems)
    {
        keel::Array<keel::String> args;
        args.push_back(keel::String("kdialog"));
        if (!opts.title.empty()) {
            args.push_back(keel::String("--title"));
            args.push_back(keel::String::from(opts.title));
        }
        switch (opts.kind) {
            case FileDialogKind::OpenFile:     args.push_back(keel::String("--getopenfilename")); break;
            case FileDialogKind::OpenFiles:    args.push_back(keel::String("--getopenfilename")); args.push_back(keel::String("--multiple")); args.push_back(keel::String("--separate-output")); break;
            case FileDialogKind::SaveFile:     args.push_back(keel::String("--getsavefilename")); break;
            case FileDialogKind::SelectFolder: args.push_back(keel::String("--getexistingdirectory")); break;
        }
        if (!opts.defaultPath.empty()) {
            args.push_back(keel::String::from(opts.defaultPath));
        } else {
            args.push_back(keel::String("."));
        }

        const ExecCaptureResult exec = RunExternalDialogCommand(args);
        if (exec.launched) {
            result.supported = true;
            if (exec.exitCode == 0) {
                keel::usize pos = 0u;
                while (pos < exec.output.size()) {
                    keel::usize nl = exec.output.find('\n', pos);
                    if (nl == keel::StringRef::npos) nl = exec.output.size();
                    const keel::StringRef line = exec.output.substr(pos, nl - pos).trim();
                    if (!line.empty()) {
                        result.paths.push_back(keel::String::from(line));
                    }
                    pos = (nl < exec.output.size()) ? (nl + 1u) : exec.output.size();
                }
                result.accepted = !result.paths.empty();
            }
            return result;
        }
    }

    return result;
}

MessageBoxChoice X11PlatformWindow::ShowMessageBox(const MessageBoxOptions& opts) {
    keel::Array<keel::String> args;
    args.push_back(keel::String("zenity"));
    if (opts.buttons == MessageBoxButtons::Ok) {
        switch (opts.icon) {
            case MessageBoxIcon::Warning: args.push_back(keel::String("--warning")); break;
            case MessageBoxIcon::Error:   args.push_back(keel::String("--error")); break;
            default:                      args.push_back(keel::String("--info")); break;
        }
    } else {
        args.push_back(keel::String("--question"));
    }
    if (!opts.title.empty()) {
        args.push_back(keel::String("--title"));
        args.push_back(keel::String::from(opts.title));
    }
    args.push_back(keel::String("--text"));
    args.push_back(keel::String::from(opts.message));

    const ExecCaptureResult exec = RunExternalDialogCommand(args);
    if (!exec.launched) {
        return MessageBoxChoice::Cancel;
    }
    if (exec.exitCode == 0) {
        return (opts.buttons == MessageBoxButtons::YesNo || opts.buttons == MessageBoxButtons::YesNoCancel)
            ? MessageBoxChoice::Yes
            : MessageBoxChoice::Ok;
    }
    return (opts.buttons == MessageBoxButtons::YesNo) ? MessageBoxChoice::No : MessageBoxChoice::Cancel;
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
