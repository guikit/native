#include "guikit/native/win32_platform_window.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <imm.h>
#ifdef IsMinimized
#undef IsMinimized
#endif
#ifdef DrawText
#undef DrawText
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#include "guikit/backend/vector_backend.hpp"
#include "guikit/context.hpp"
#include "guikit/core/assets.hpp"
#include "keel/array.h"
#include "keel/dynlib.h"
#include "keel/format.h"
#include "keel/math.h"
#include "keel/memory.h"
#include "keel/string.h"
#include "keel/utf8.h"

namespace guikit::native {

namespace {

static constexpr wchar_t kWindowClassName[] = L"GuiKitNativeWin32WindowClass";
static constexpr wchar_t kDummyClassName[]  = L"GuiKitNativeWglDummyClass";
static constexpr UINT_PTR kLiveResizeTimerId = 1u;

// WGL extension constants (avoiding dependency on external wglext.h)
static constexpr keel::i32 kWglDrawToWindowArb          = 0x2001;
static constexpr keel::i32 kWglSupportOpenGlArb         = 0x2010;
static constexpr keel::i32 kWglDoubleBufferArb          = 0x2011;
static constexpr keel::i32 kWglPixelTypeArb             = 0x2013;
static constexpr keel::i32 kWglColorBitsArb             = 0x2014;
static constexpr keel::i32 kWglAlphaBitsArb             = 0x201B;
static constexpr keel::i32 kWglStencilBitsArb           = 0x2023;
static constexpr keel::i32 kWglSampleBuffersArb         = 0x2041;
static constexpr keel::i32 kWglSamplesArb               = 0x2042;
static constexpr keel::i32 kWglAccelerationArb          = 0x2003;
static constexpr keel::i32 kWglFullAccelerationArb      = 0x2027;
static constexpr keel::i32 kWglTypeRgbaArb              = 0x202B;
static constexpr keel::i32 kWglContextMajorVersionArb   = 0x2091;
static constexpr keel::i32 kWglContextMinorVersionArb   = 0x2092;
static constexpr keel::i32 kWglContextProfileMaskArb    = 0x9126;
static constexpr keel::i32 kWglContextCoreProfileBitArb = 0x00000001;

using PfnWglChoosePixelFormatArb = BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*); // keel-lint: ignore
using PfnWglCreateContextAttribsArb = HGLRC(WINAPI*)(HDC, HGLRC, const int*); // keel-lint: ignore
using PfnWglSwapIntervalExt = BOOL(WINAPI*)(int); // keel-lint: ignore

using PfnSetProcessDpiAwarenessContext = BOOL(WINAPI*)(HANDLE);
using PfnSetProcessDpiAwareness = HRESULT(WINAPI*)(keel::i32);
using PfnSetProcessDPIAware = BOOL(WINAPI*)();
using PfnGetDpiForWindow = UINT(WINAPI*)(HWND);
using PfnAdjustWindowRectExForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
using PfnDwmSetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
using PfnDwmFlush = HRESULT(WINAPI*)();
using PfnDwmIsCompositionEnabled = HRESULT(WINAPI*)(BOOL*);
using PfnImmGetContext = HIMC(WINAPI*)(HWND);
using PfnImmReleaseContext = BOOL(WINAPI*)(HWND, HIMC);
using PfnImmGetCompositionStringW = LONG(WINAPI*)(HIMC, DWORD, LPVOID, DWORD);
using PfnImmSetCompositionWindow = BOOL(WINAPI*)(HIMC, LPCOMPOSITIONFORM);
using PfnImmSetCandidateWindow = BOOL(WINAPI*)(HIMC, LPCANDIDATEFORM);

HMODULE g_opengl32Module{nullptr};

template <typename Fn, typename RawProc>
[[nodiscard]] inline Fn CastWin32Proc(RawProc raw) noexcept {
    Fn out = nullptr;
    void* ptr = reinterpret_cast<void*>(raw);
    if (ptr) {
        keel::mem_copy(&out, &ptr, sizeof(out));
    }
    return out;
}

void* WglProcLoaderThunk(const char* name) noexcept {
    if (!name) return nullptr;
    PROC raw = ::wglGetProcAddress(name);
    const keel::usize addr = reinterpret_cast<keel::usize>(raw);
    if (addr == 0u || addr == 1u || addr == 2u || addr == 3u || addr == keel::Limits<keel::usize>::kMax) {
        if (!g_opengl32Module) {
            g_opengl32Module = ::GetModuleHandleW(L"opengl32.dll");
            if (!g_opengl32Module) {
                g_opengl32Module = ::LoadLibraryW(L"opengl32.dll");
            }
        }
        if (g_opengl32Module) {
            raw = ::GetProcAddress(g_opengl32Module, name);
        } else {
            raw = nullptr;
        }
    }
    return reinterpret_cast<void*>(raw);
}

[[nodiscard]] keel::Array<wchar_t> Utf8ToWideZ(keel::StringRef utf8) {
    keel::Array<wchar_t> out;
    if (utf8.empty()) {
        out.push_back(L'\0');
        return out;
    }
    const keel::i32 req = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), keel::as<keel::i32>(utf8.size()), nullptr, 0
    );
    if (req <= 0) {
        out.push_back(L'\0');
        return out;
    }
    out.resize(keel::as<keel::usize>(req) + 1u, L'\0');
    (void)::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), keel::as<keel::i32>(utf8.size()), out.data(), req
    );
    out[keel::as<keel::usize>(req)] = L'\0';
    return out;
}

[[nodiscard]] keel::String WideToUtf8(const wchar_t* wide, keel::i32 wideLen = -1) {
    if (!wide || (wideLen == 0)) {
        return {};
    }
    const keel::i32 req = ::WideCharToMultiByte(
        CP_UTF8, 0, wide, wideLen, nullptr, 0, nullptr, nullptr
    );
    if (req <= 0) {
        return {};
    }
    keel::Array<char> buf(keel::as<keel::usize>(req), '\0');
    const keel::i32 written = ::WideCharToMultiByte(
        CP_UTF8, 0, wide, wideLen, buf.data(), req, nullptr, nullptr
    );
    if (written <= 0) {
        return {};
    }
    keel::usize outLen = keel::as<keel::usize>(written);
    if (wideLen < 0 && outLen > 0u && buf[outLen - 1u] == '\0') {
        --outLen;
    }
    return keel::String::from(keel::StringRef(buf.data(), outLen));
}

void EnableBestProcessDpiAwareness() noexcept {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        auto setCtx = CastWin32Proc<PfnSetProcessDpiAwarenessContext>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        );
        if (setCtx) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = (DPI_AWARENESS_CONTEXT)-4
            if (setCtx(reinterpret_cast<HANDLE>(keel::as<keel::isize>(-4)))) {
                return;
            }
        }
    }

    if (auto shcore = keel::DynLib::open("shcore.dll")) {
        auto setAware = shcore->symbol<PfnSetProcessDpiAwareness>("SetProcessDpiAwareness");
        if (setAware && setAware(2 /*PROCESS_PER_MONITOR_DPI_AWARE*/) == S_OK) {
            return;
        }
    }

    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        auto setLegacy = CastWin32Proc<PfnSetProcessDPIAware>(
            ::GetProcAddress(user32, "SetProcessDPIAware")
        );
        if (setLegacy) {
            (void)setLegacy();
        }
    }
}

[[nodiscard]] UINT QueryWindowDpi(HWND hwnd) noexcept {
    if (hwnd) {
        if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
            auto getDpiForWin = CastWin32Proc<PfnGetDpiForWindow>(
                ::GetProcAddress(user32, "GetDpiForWindow")
            );
            if (getDpiForWin) {
                const UINT dpi = getDpiForWin(hwnd);
                if (dpi >= 48u) return dpi;
            }
        }
    }
    HDC dc = ::GetDC(hwnd);
    UINT dpi = 96u;
    if (dc) {
        const keel::i32 logX = ::GetDeviceCaps(dc, LOGPIXELSX);
        if (logX >= 48) {
            dpi = keel::as<UINT>(logX);
        }
        ::ReleaseDC(hwnd, dc);
    }
    return dpi;
}

[[nodiscard]] keel::f32 QueryHdcPixelsPerMm(HDC hdc) noexcept {
    if (!hdc) return 0.0f;
    const keel::i32 horzPx = ::GetDeviceCaps(hdc, HORZRES);
    const keel::i32 horzMm = ::GetDeviceCaps(hdc, HORZSIZE);
    if (horzPx > 0 && horzMm > 0) {
        const keel::f32 ppm = keel::as<keel::f32>(horzPx) / keel::as<keel::f32>(horzMm);
        if (ppm >= 1.5f && ppm <= 25.0f) {
            return ppm;
        }
    }
    return 0.0f;
}

[[nodiscard]] keel::f32 QueryWindowPixelsPerMm(HWND hwnd, keel::f32 dpiScale) noexcept {
    if (hwnd) {
        HMONITOR hmon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (hmon) {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (::GetMonitorInfoW(hmon, &mi)) {
                HDC ic = ::CreateICW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
                if (ic) {
                    const keel::f32 ppm = QueryHdcPixelsPerMm(ic);
                    ::DeleteDC(ic);
                    if (ppm > 0.0f) return ppm;
                }
            }
        }
    }
    HDC screenDc = ::GetDC(nullptr);
    if (screenDc) {
        const keel::f32 ppm = QueryHdcPixelsPerMm(screenDc);
        ::ReleaseDC(nullptr, screenDc);
        if (ppm > 0.0f) return ppm;
    }
    const keel::f32 safeScale = (dpiScale > 0.1f) ? dpiScale : 1.0f;
    return (96.0f * safeScale) / 25.4f;
}

void ApplyDwmDarkTitlebar(HWND hwnd) noexcept {
    if (!hwnd) return;
    if (auto dwm = keel::DynLib::open("dwmapi.dll")) {
        auto setAttr = dwm->symbol<PfnDwmSetWindowAttribute>("DwmSetWindowAttribute");
        if (setAttr) {
            const BOOL enableDark = TRUE;
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Win10 20H1+/Win11), 19 (Win10 1903/1909)
            if (FAILED(setAttr(hwnd, 20u, &enableDark, sizeof(enableDark)))) {
                (void)setAttr(hwnd, 19u, &enableDark, sizeof(enableDark));
            }
        }
    }
}

[[nodiscard]] KeyModifiers QueryWin32Modifiers() noexcept {
    KeyModifiers mods{};
    mods.ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    mods.alt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;
    return mods;
}

[[nodiscard]] Key MapWin32VirtualKey(WPARAM vk) noexcept {
    switch (vk) {
        case VK_BACK:     return Key::Backspace;
        case VK_DELETE:   return Key::Delete;
        case VK_RETURN:   return Key::Enter;
        case VK_ESCAPE:   return Key::Escape;
        case VK_TAB:      return Key::Tab;
        case VK_SPACE:    return Key::Space;
        case VK_LEFT:     return Key::Left;
        case VK_RIGHT:    return Key::Right;
        case VK_HOME:     return Key::Home;
        case VK_END:      return Key::End;
        case VK_UP:       return Key::Up;
        case VK_DOWN:     return Key::Down;
        case VK_PRIOR:    return Key::PageUp;
        case VK_NEXT:     return Key::PageDown;
        case 'A':         return Key::A;
        case 'C':         return Key::C;
        case 'D':         return Key::D;
        case 'F':         return Key::F;
        case 'G':         return Key::G;
        case 'H':         return Key::H;
        case 'K':         return Key::K;
        case 'V':         return Key::V;
        case 'X':         return Key::X;
        case 'Y':         return Key::Y;
        case 'Z':         return Key::Z;
        case VK_OEM_2:
        case VK_DIVIDE:   return Key::Slash;
        case VK_OEM_PLUS:
        case VK_ADD:      return Key::Plus;
        case VK_OEM_MINUS:
        case VK_SUBTRACT: return Key::Minus;
        case '0':
        case VK_NUMPAD0:  return Key::Zero;
        case VK_F3:       return Key::F3;
        case VK_F5:       return Key::F5;
        case VK_F9:       return Key::F9;
        case VK_F10:      return Key::F10;
        case VK_F11:      return Key::F11;
        default:          return Key::None;
    }
}

[[nodiscard]] LPCWSTR MapWin32CursorId(CursorType cursor) noexcept {
    switch (cursor) {
        case CursorType::Pointer:    return IDC_HAND;
        case CursorType::Text:       return IDC_IBEAM;
        case CursorType::ResizeEW:   return IDC_SIZEWE;
        case CursorType::ResizeNS:   return IDC_SIZENS;
        case CursorType::ResizeNWSE: return IDC_SIZENWSE;
        case CursorType::ResizeNESW: return IDC_SIZENESW;
        case CursorType::Move:       return IDC_SIZEALL;
        case CursorType::NotAllowed: return IDC_NO;
        case CursorType::Default:
        default:                     return IDC_ARROW;
    }
}

[[nodiscard]] keel::String ReadWin32Clipboard(HWND hwnd) {
    if (!::OpenClipboard(hwnd)) {
        return {};
    }
    keel::String result;
    HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const auto* wide = static_cast<const wchar_t*>(::GlobalLock(hData));
        if (wide) {
            result = WideToUtf8(wide, -1);
            ::GlobalUnlock(hData);
        }
    }
    ::CloseClipboard();
    return result;
}

void WriteWin32Clipboard(HWND hwnd, keel::StringRef utf8) {
    if (!::OpenClipboard(hwnd)) {
        return;
    }
    ::EmptyClipboard();
    const keel::Array<wchar_t> wide = Utf8ToWideZ(utf8);
    const keel::usize byteCount = wide.size() * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (hMem) {
        void* dst = ::GlobalLock(hMem);
        if (dst) {
            keel::mem_copy(dst, wide.data(), byteCount);
            ::GlobalUnlock(hMem);
            if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
                ::GlobalFree(hMem);
            }
        } else {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

[[nodiscard]] HICON CreateDownsampledWin32Icon(
    const keel::u32* srcRgba,
    keel::u32 srcW,
    keel::u32 srcH,
    keel::u32 dstDim
) noexcept {
    if (!srcRgba || srcW == 0u || srcH == 0u || dstDim == 0u) {
        return nullptr;
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = keel::as<LONG>(dstDim);
    bi.bV5Height = -keel::as<LONG>(dstDim); // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000u;
    bi.bV5GreenMask = 0x0000FF00u;
    bi.bV5BlueMask = 0x000000FFu;
    bi.bV5AlphaMask = 0xFF000000u;

    void* bits = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HBITMAP colorBmp = ::CreateDIBSection(
        screenDc,
        reinterpret_cast<const BITMAPINFO*>(&bi),
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0u
    );
    ::ReleaseDC(nullptr, screenDc);
    if (!colorBmp || !bits) {
        if (colorBmp) ::DeleteObject(colorBmp);
        return nullptr;
    }

    auto* dst = static_cast<keel::u32*>(bits);
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
            // Win32 BGRA32 (0xAARRGGBB in little-endian uint32)
            dst[keel::as<keel::usize>(y) * dstDim + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    HBITMAP maskBmp = ::CreateBitmap(keel::as<keel::i32>(dstDim), keel::as<keel::i32>(dstDim), 1u, 1u, nullptr);
    HICON icon = nullptr;
    if (maskBmp) {
        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmMask = maskBmp;
        ii.hbmColor = colorBmp;
        icon = ::CreateIconIndirect(&ii);
        ::DeleteObject(maskBmp);
    }
    ::DeleteObject(colorBmp);
    return icon;
}

} // namespace

struct Win32PlatformWindow::Impl {
    HINSTANCE hInstance{nullptr};
    HWND hwnd{nullptr};
    HDC hdc{nullptr};
    HGLRC hglrc{nullptr};
    HANDLE wakeEvent{nullptr};
    HICON iconBig{nullptr};
    HICON iconSmall{nullptr};

    PfnWglChoosePixelFormatArb wglChoosePixelFormatArb{nullptr};
    PfnWglCreateContextAttribsArb wglCreateContextAttribsArb{nullptr};
    PfnWglSwapIntervalExt wglSwapIntervalExt{nullptr};

    bool dwmProbed{false};
    keel::Optional<keel::DynLib> dwmLib;
    PfnDwmFlush dwmFlush{nullptr};
    PfnDwmIsCompositionEnabled dwmIsCompositionEnabled{nullptr};

    bool immProbed{false};
    keel::Optional<keel::DynLib> imm32Lib;
    PfnImmGetContext immGetContext{nullptr};
    PfnImmReleaseContext immReleaseContext{nullptr};
    PfnImmGetCompositionStringW immGetCompositionStringW{nullptr};
    PfnImmSetCompositionWindow immSetCompositionWindow{nullptr};
    PfnImmSetCandidateWindow immSetCandidateWindow{nullptr};
    Rect lastImeCaretPx{};
    bool imeActive{false};

    Context* ui{nullptr};
    WindowHostCallbacks callbacks{};

    keel::i32 width{1380};
    keel::i32 height{800};
    keel::i32 minWidth{320};
    keel::i32 minHeight{240};
    keel::f32 dpiScale{1.0f};
    keel::f32 pixelsPerMm{96.0f / 25.4f};
    bool highDpi{true};
    bool vsync{true};
    bool fullscreen{false};
    bool dwmPacingActive{false};
    bool minimized{false};
    bool inSizeMove{false};
    bool liveResizeFrame{false};
    bool closeRequested{false};
    bool alive{false};
    keel::u8 capturedButtons{0u};
    keel::u16 pendingHighSurrogate{0u};

    CursorType activeCursor{CursorType::Default};
    HCURSOR cursors[9]{};

    void DestroyAll() noexcept {
        alive = false;
        if (hwnd) {
            ::KillTimer(hwnd, kLiveResizeTimerId);
        }
        if (hglrc) {
            (void)::wglMakeCurrent(nullptr, nullptr);
            (void)::wglDeleteContext(hglrc);
            hglrc = nullptr;
        }
        if (hdc && hwnd) {
            (void)::ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
        if (hwnd) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            (void)::DestroyWindow(hwnd);
            hwnd = nullptr;
        }
        if (wakeEvent) {
            (void)::CloseHandle(wakeEvent);
            wakeEvent = nullptr;
        }
        if (iconBig) {
            (void)::DestroyIcon(iconBig);
            iconBig = nullptr;
        }
        if (iconSmall) {
            (void)::DestroyIcon(iconSmall);
            iconSmall = nullptr;
        }
        ui = nullptr;
        callbacks = {};
        capturedButtons = 0u;
        pendingHighSurrogate = 0u;
        inSizeMove = false;
        liveResizeFrame = false;
        dwmPacingActive = false;
        fullscreen = false;
        imeActive = false;
        lastImeCaretPx = {};
    }

    void EnsureDwmLoaded() noexcept {
        if (dwmProbed) {
            return;
        }
        dwmProbed = true;
        if (auto lib = keel::DynLib::open("dwmapi.dll"); lib.is_ok()) {
            dwmLib = keel::move(lib.value());
            dwmFlush = dwmLib->symbol<PfnDwmFlush>("DwmFlush");
            dwmIsCompositionEnabled = dwmLib->symbol<PfnDwmIsCompositionEnabled>("DwmIsCompositionEnabled");
        }
    }

    void EnsureImmLoaded() noexcept {
        if (immProbed) {
            return;
        }
        immProbed = true;
        if (auto lib = keel::DynLib::open("imm32.dll"); lib.is_ok()) {
            imm32Lib = keel::move(lib.value());
            immGetContext = imm32Lib->symbol<PfnImmGetContext>("ImmGetContext");
            immReleaseContext = imm32Lib->symbol<PfnImmReleaseContext>("ImmReleaseContext");
            immGetCompositionStringW = imm32Lib->symbol<PfnImmGetCompositionStringW>("ImmGetCompositionStringW");
            immSetCompositionWindow = imm32Lib->symbol<PfnImmSetCompositionWindow>("ImmSetCompositionWindow");
            immSetCandidateWindow = imm32Lib->symbol<PfnImmSetCandidateWindow>("ImmSetCandidateWindow");
        }
    }

    void UpdateImmCaretPosition(Rect caretRectPx) noexcept {
        if (!hwnd) return;
        EnsureImmLoaded();
        if (!immGetContext || !immReleaseContext) return;
        HIMC himc = immGetContext(hwnd);
        if (!himc) return;
        const LONG x = keel::as<LONG>(keel::round_i32(caretRectPx.x));
        const LONG y = keel::as<LONG>(keel::round_i32(caretRectPx.y));
        const LONG bottom = keel::as<LONG>(keel::round_i32(caretRectPx.y + keel::max(caretRectPx.h, 16.0f)));
        if (immSetCompositionWindow) {
            COMPOSITIONFORM cf{};
            cf.dwStyle = CFS_POINT;
            cf.ptCurrentPos.x = x;
            cf.ptCurrentPos.y = y;
            (void)immSetCompositionWindow(himc, &cf);
        }
        if (immSetCandidateWindow) {
            CANDIDATEFORM cand{};
            cand.dwIndex = 0u;
            cand.dwStyle = CFS_CANDIDATEPOS;
            cand.ptCurrentPos.x = x;
            cand.ptCurrentPos.y = bottom;
            (void)immSetCandidateWindow(himc, &cand);
        }
        (void)immReleaseContext(hwnd, himc);
    }

    bool BootstrapWglExtensions() noexcept {
        WNDCLASSEXW dummyCls{};
        dummyCls.cbSize = sizeof(dummyCls);
        dummyCls.style = CS_OWNDC;
        dummyCls.lpfnWndProc = ::DefWindowProcW;
        dummyCls.hInstance = hInstance;
        dummyCls.lpszClassName = kDummyClassName;
        if (!::GetClassInfoExW(hInstance, kDummyClassName, &dummyCls)) {
            (void)::RegisterClassExW(&dummyCls);
        }

        HWND dummyHwnd = ::CreateWindowExW(
            0, kDummyClassName, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 1, 1, nullptr, nullptr, hInstance, nullptr
        );
        if (!dummyHwnd) {
            return false;
        }

        HDC dummyDc = ::GetDC(dummyHwnd);
        if (!dummyDc) {
            ::DestroyWindow(dummyHwnd);
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        const keel::i32 fmt = ::ChoosePixelFormat(dummyDc, &pfd);
        if (fmt != 0 && ::SetPixelFormat(dummyDc, fmt, &pfd)) {
            HGLRC dummyRc = ::wglCreateContext(dummyDc);
            if (dummyRc) {
                if (::wglMakeCurrent(dummyDc, dummyRc)) {
                    wglChoosePixelFormatArb = CastWin32Proc<PfnWglChoosePixelFormatArb>(
                        ::wglGetProcAddress("wglChoosePixelFormatARB")
                    );
                    wglCreateContextAttribsArb = CastWin32Proc<PfnWglCreateContextAttribsArb>(
                        ::wglGetProcAddress("wglCreateContextAttribsARB")
                    );
                    wglSwapIntervalExt = CastWin32Proc<PfnWglSwapIntervalExt>(
                        ::wglGetProcAddress("wglSwapIntervalEXT")
                    );
                    (void)::wglMakeCurrent(nullptr, nullptr);
                }
                (void)::wglDeleteContext(dummyRc);
            }
        }

        ::ReleaseDC(dummyHwnd, dummyDc);
        ::DestroyWindow(dummyHwnd);
        return true;
    }

    void ApplySwapInterval() noexcept {
        dwmPacingActive = false;
        if (!hglrc) return;
        if (vsync && !fullscreen) {
            EnsureDwmLoaded();
            BOOL compEnabled = TRUE;
            const bool dwmActive = dwmFlush &&
                (!dwmIsCompositionEnabled ||
                 (SUCCEEDED(dwmIsCompositionEnabled(&compEnabled)) && compEnabled == TRUE));
            if (dwmActive) {
                // On DWM-composited Windows desktops, windowed OpenGL is always presented tear-free
                // by DWM. Setting swap interval 0 and pacing via DwmFlush() in WaitForVBlank()
                // avoids double-vblank halving (30 FPS) and prevents driver stalls inside EndFrame().
                dwmPacingActive = true;
                if (wglSwapIntervalExt) {
                    (void)wglSwapIntervalExt(0);
                }
                return;
            }
        }
        if (wglSwapIntervalExt) {
            (void)wglSwapIntervalExt(vsync ? 1 : 0);
        }
    }

    bool InitWgl() noexcept {
        if (!hwnd) return false;
        (void)BootstrapWglExtensions();

        hdc = ::GetDC(hwnd);
        if (!hdc) {
            keel::eprintln("[Win32PlatformWindow] GetDC() failed");
            return false;
        }

        keel::i32 pixelFormat = 0;
        if (wglChoosePixelFormatArb) {
            struct PfProfile {
                keel::i32 stencilBits;
                keel::i32 sampleBuffers;
                keel::i32 samples;
            };
            static constexpr PfProfile kProfiles[] = {
                {8, 1, 4}, // 4x MSAA + 8-bit stencil
                {8, 1, 2}, // 2x MSAA + 8-bit stencil
                {8, 0, 0}, // 8-bit stencil without MSAA
                {0, 0, 0}  // Minimal RGBA8 fallback
            };
            for (const PfProfile& prof : kProfiles) {
                int pfAttribs[24]{}; // keel-lint: ignore
                keel::usize idx = 0u;
                pfAttribs[idx++] = kWglDrawToWindowArb;  pfAttribs[idx++] = 1;
                pfAttribs[idx++] = kWglSupportOpenGlArb; pfAttribs[idx++] = 1;
                pfAttribs[idx++] = kWglDoubleBufferArb;  pfAttribs[idx++] = 1;
                pfAttribs[idx++] = kWglAccelerationArb;  pfAttribs[idx++] = kWglFullAccelerationArb;
                pfAttribs[idx++] = kWglPixelTypeArb;     pfAttribs[idx++] = kWglTypeRgbaArb;
                pfAttribs[idx++] = kWglColorBitsArb;     pfAttribs[idx++] = 32;
                pfAttribs[idx++] = kWglAlphaBitsArb;     pfAttribs[idx++] = 8;
                if (prof.stencilBits > 0) {
                    pfAttribs[idx++] = kWglStencilBitsArb; pfAttribs[idx++] = prof.stencilBits;
                }
                if (prof.sampleBuffers > 0) {
                    pfAttribs[idx++] = kWglSampleBuffersArb; pfAttribs[idx++] = prof.sampleBuffers;
                    pfAttribs[idx++] = kWglSamplesArb;       pfAttribs[idx++] = prof.samples;
                }
                pfAttribs[idx++] = 0;

                int chosen = 0; // keel-lint: ignore
                UINT numFormats = 0u;
                if (wglChoosePixelFormatArb(hdc, pfAttribs, nullptr, 1u, &chosen, &numFormats) && numFormats > 0u) {
                    pixelFormat = keel::as<keel::i32>(chosen);
                    break;
                }
            }
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cAlphaBits = 8;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        if (pixelFormat == 0) {
            pixelFormat = ::ChoosePixelFormat(hdc, &pfd);
        } else {
            (void)::DescribePixelFormat(hdc, pixelFormat, sizeof(pfd), &pfd);
        }
        if (pixelFormat == 0 || !::SetPixelFormat(hdc, pixelFormat, &pfd)) {
            keel::eprintln("[Win32PlatformWindow] SetPixelFormat() failed");
            return false;
        }

        if (wglCreateContextAttribsArb) {
            const int ctx33[] = { // keel-lint: ignore
                kWglContextMajorVersionArb, 3,
                kWglContextMinorVersionArb, 3,
                kWglContextProfileMaskArb, kWglContextCoreProfileBitArb,
                0
            };
            hglrc = wglCreateContextAttribsArb(hdc, nullptr, ctx33);
            if (!hglrc) {
                const int ctx32[] = { // keel-lint: ignore
                    kWglContextMajorVersionArb, 3,
                    kWglContextMinorVersionArb, 2,
                    kWglContextProfileMaskArb, kWglContextCoreProfileBitArb,
                    0
                };
                hglrc = wglCreateContextAttribsArb(hdc, nullptr, ctx32);
            }
        }
        if (!hglrc) {
            hglrc = ::wglCreateContext(hdc);
        }
        if (!hglrc) {
            keel::eprintln("[Win32PlatformWindow] Failed to create OpenGL context");
            return false;
        }

        if (!::wglMakeCurrent(hdc, hglrc)) {
            keel::eprintln("[Win32PlatformWindow] wglMakeCurrent() failed");
            return false;
        }

        ApplySwapInterval();
        return true;
    }

    void UpdateDpiMetrics(UINT dpi) noexcept {
        dpiScale = highDpi ? (keel::as<keel::f32>(dpi) / 96.0f) : 1.0f;
        if (dpiScale < 0.5f) dpiScale = 1.0f;
        pixelsPerMm = QueryWindowPixelsPerMm(hwnd, dpiScale);
    }

    void HandleMouseButtonDown(keel::i32 btn, keel::u8 mask, LPARAM lp) {
        if (!ui) return;
        ui->SetKeyModifiers(QueryWin32Modifiers());
        ui->OnMouseMove({keel::as<keel::f32>(GET_X_LPARAM(lp)), keel::as<keel::f32>(GET_Y_LPARAM(lp))});
        if (capturedButtons == 0u && hwnd) {
            ::SetCapture(hwnd);
        }
        capturedButtons |= mask;
        ui->OnMouseDown(btn);
    }

    void HandleMouseButtonUp(keel::i32 btn, keel::u8 mask, LPARAM lp) {
        if (!ui) return;
        ui->SetKeyModifiers(QueryWin32Modifiers());
        ui->OnMouseMove({keel::as<keel::f32>(GET_X_LPARAM(lp)), keel::as<keel::f32>(GET_Y_LPARAM(lp))});
        capturedButtons &= keel::as<keel::u8>(~mask);
        if (capturedButtons == 0u && ::GetCapture() == hwnd) {
            ::ReleaseCapture();
        }
        ui->OnMouseUp(btn);
    }

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CLOSE: {
                const bool allow = callbacks.onCloseRequested ? callbacks.onCloseRequested() : true;
                if (allow) {
                    closeRequested = true;
                }
                return 0;
            }

            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                if (mmi && minWidth > 0 && minHeight > 0) {
                    RECT rc{0, 0, minWidth, minHeight};
                    const DWORD style = keel::as<DWORD>(::GetWindowLongPtrW(hWnd, GWL_STYLE));
                    const DWORD exStyle = keel::as<DWORD>(::GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
                    bool adjusted = false;
                    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
                        auto adjDpi = CastWin32Proc<PfnAdjustWindowRectExForDpi>(
                            ::GetProcAddress(user32, "AdjustWindowRectExForDpi")
                        );
                        if (adjDpi) {
                            adjusted = (adjDpi(&rc, style, FALSE, exStyle, QueryWindowDpi(hWnd)) != FALSE);
                        }
                    }
                    if (!adjusted) {
                        (void)::AdjustWindowRectEx(&rc, style, FALSE, exStyle);
                    }
                    mmi->ptMinTrackSize.x = rc.right - rc.left;
                    mmi->ptMinTrackSize.y = rc.bottom - rc.top;
                    return 0;
                }
                break;
            }

            case WM_ENTERSIZEMOVE:
                inSizeMove = true;
                ::SetTimer(hWnd, kLiveResizeTimerId, 16u, nullptr);
                return 0;

            case WM_EXITSIZEMOVE:
                inSizeMove = false;
                ::KillTimer(hWnd, kLiveResizeTimerId);
                if (ui) {
                    ui->InvalidateAll();
                    ui->RequestRepaint(2);
                }
                return 0;

            case WM_TIMER:
                if (wParam == kLiveResizeTimerId && inSizeMove && !minimized && callbacks.onLiveResizeRender) {
                    liveResizeFrame = true;
                    callbacks.onLiveResizeRender();
                    liveResizeFrame = false;
                    return 0;
                }
                break;

            case WM_SIZE: {
                const keel::i32 nw = keel::as<keel::i32>(LOWORD(lParam));
                const keel::i32 nh = keel::as<keel::i32>(HIWORD(lParam));
                if (wParam == SIZE_MINIMIZED || nw == 0 || nh == 0) {
                    minimized = true;
                    return 0;
                }
                const bool wasMin = minimized;
                minimized = false;
                if (nw != width || nh != height || wasMin) {
                    width = nw;
                    height = nh;
                    if (callbacks.onResize) {
                        callbacks.onResize(width, height);
                    }
                    if (ui) {
                        ui->InvalidateAll();
                        ui->RequestRepaint(2);
                    }
                    if (inSizeMove && callbacks.onLiveResizeRender) {
                        liveResizeFrame = true;
                        callbacks.onLiveResizeRender();
                        liveResizeFrame = false;
                    }
                }
                return 0;
            }

            case WM_DPICHANGED: {
                const UINT newDpi = HIWORD(wParam);
                UpdateDpiMetrics(newDpi);
                if (const auto* suggested = reinterpret_cast<const RECT*>(lParam)) {
                    ::SetWindowPos(
                        hWnd,
                        nullptr,
                        suggested->left,
                        suggested->top,
                        suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOZORDER | SWP_NOACTIVATE
                    );
                }
                if (ui) {
                    ui->InvalidateAll();
                    ui->RequestRepaint(2);
                }
                return 0;
            }

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                (void)::BeginPaint(hWnd, &ps);
                ::EndPaint(hWnd, &ps);
                if (ui) {
                    ui->InvalidateAll();
                }
                return 0;
            }

            case WM_ERASEBKGND:
                return 1; // Prevent GDI flicker over OpenGL swapchain

            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT) {
                    const keel::usize idx = keel::as<keel::usize>(activeCursor);
                    HCURSOR cur = (idx < 9u && cursors[idx]) ? cursors[idx] : ::LoadCursorW(nullptr, IDC_ARROW);
                    ::SetCursor(cur);
                    return TRUE;
                }
                break;

            case WM_CAPTURECHANGED:
                if (ui && capturedButtons != 0u) {
                    if (capturedButtons & 1u) ui->OnMouseUp(1);
                    if (capturedButtons & 2u) ui->OnMouseUp(2);
                    if (capturedButtons & 4u) ui->OnMouseUp(3);
                }
                capturedButtons = 0u;
                return 0;

            case WM_MOUSEMOVE:
                if (ui) {
                    ui->SetKeyModifiers(QueryWin32Modifiers());
                    ui->OnMouseMove({keel::as<keel::f32>(GET_X_LPARAM(lParam)), keel::as<keel::f32>(GET_Y_LPARAM(lParam))});
                }
                return 0;

            case WM_LBUTTONDOWN:
                HandleMouseButtonDown(1, 1u, lParam);
                return 0;
            case WM_LBUTTONUP:
                HandleMouseButtonUp(1, 1u, lParam);
                return 0;
            case WM_MBUTTONDOWN:
                HandleMouseButtonDown(2, 2u, lParam);
                return 0;
            case WM_MBUTTONUP:
                HandleMouseButtonUp(2, 2u, lParam);
                return 0;
            case WM_RBUTTONDOWN:
                HandleMouseButtonDown(3, 4u, lParam);
                return 0;
            case WM_RBUTTONUP:
                HandleMouseButtonUp(3, 4u, lParam);
                return 0;

            case WM_MOUSEWHEEL: {
                if (ui) {
                    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ::ScreenToClient(hWnd, &pt);
                    ui->SetKeyModifiers(QueryWin32Modifiers());
                    ui->OnMouseMove({keel::as<keel::f32>(pt.x), keel::as<keel::f32>(pt.y)});
                    const keel::f32 delta = keel::as<keel::f32>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
                    ui->OnMouseWheel(0.0f, delta);
                }
                return 0;
            }

            case WM_MOUSEHWHEEL: {
                if (ui) {
                    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ::ScreenToClient(hWnd, &pt);
                    ui->SetKeyModifiers(QueryWin32Modifiers());
                    ui->OnMouseMove({keel::as<keel::f32>(pt.x), keel::as<keel::f32>(pt.y)});
                    const keel::f32 delta = keel::as<keel::f32>(GET_WHEEL_DELTA_WPARAM(wParam)) / 120.0f;
                    ui->OnMouseWheel(delta, 0.0f);
                }
                return 0;
            }

            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (ui) {
                    const KeyModifiers mods = QueryWin32Modifiers();
                    ui->SetKeyModifiers(mods);
                    const Key logicalKey = MapWin32VirtualKey(wParam);
                    if (logicalKey != Key::None) {
                        ui->OnKeyDown(logicalKey, mods);
                    }
                }
                if (msg == WM_SYSKEYDOWN && wParam == VK_F4) {
                    break; // Let Alt+F4 trigger WM_CLOSE via DefWindowProcW
                }
                return 0;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (ui) {
                    ui->SetKeyModifiers(QueryWin32Modifiers());
                }
                return 0;

            case WM_CHAR: {
                if (!ui) return 0;
                const keel::u32 ch = keel::as<keel::u32>(wParam);
                if (ch >= 0xD800u && ch <= 0xDBFFu) {
                    pendingHighSurrogate = keel::as<keel::u16>(ch);
                    return 0;
                }
                keel::u32 codepoint = ch;
                if (ch >= 0xDC00u && ch <= 0xDFFFu) {
                    if (pendingHighSurrogate != 0u) {
                        codepoint = 0x10000u +
                            ((keel::as<keel::u32>(pendingHighSurrogate) - 0xD800u) << 10) +
                            (ch - 0xDC00u);
                        pendingHighSurrogate = 0u;
                    } else {
                        return 0;
                    }
                } else {
                    pendingHighSurrogate = 0u;
                }

                if (codepoint >= 0x20u && codepoint != 0x7Fu) {
                    const KeyModifiers mods = QueryWin32Modifiers();
                    // Allow AltGr (Ctrl+Alt) characters while suppressing pure Ctrl shortcuts
                    if (!(mods.ctrl && !mods.alt)) {
                        char utf8Buf[4]{};
                        const keel::usize len = keel::utf8_encode(codepoint, utf8Buf);
                        if (len > 0u) {
                            ui->OnTextInput(keel::StringRef(utf8Buf, len));
                        }
                    }
                }
                return 0;
            }

            case WM_SETFOCUS:
                if (ui) {
                    ui->SetKeyModifiers(QueryWin32Modifiers());
                    ui->InvalidateAll();
                }
                return 0;

            case WM_IME_STARTCOMPOSITION:
                if (imeActive) {
                    UpdateImmCaretPosition(lastImeCaretPx);
                }
                break;

            case WM_IME_COMPOSITION: {
                if (ui && (lParam & GCS_COMPSTR) != 0) {
                    EnsureImmLoaded();
                    if (immGetContext && immReleaseContext && immGetCompositionStringW) {
                        HIMC himc = immGetContext(hWnd);
                        if (himc) {
                            const LONG byteLen = immGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0u);
                            if (byteLen > 0) {
                                const keel::usize wcharCount = keel::as<keel::usize>(byteLen) / sizeof(wchar_t);
                                keel::Array<wchar_t> buf(wcharCount, L'\0');
                                (void)immGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), keel::as<DWORD>(byteLen));
                                const keel::String utf8 = WideToUtf8(buf.data(), keel::as<keel::i32>(wcharCount));
                                const LONG cursorPos = immGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0u);
                                const keel::i32 cursorChars = (cursorPos >= 0) ? keel::as<keel::i32>(cursorPos) : keel::as<keel::i32>(wcharCount);
                                ui->OnTextComposition(utf8.as_ref(), cursorChars, 0);
                            } else {
                                ui->OnTextCompositionEnd();
                            }
                            (void)immReleaseContext(hWnd, himc);
                        }
                    }
                }
                break;
            }

            case WM_IME_ENDCOMPOSITION:
                if (ui) {
                    ui->OnTextCompositionEnd();
                }
                break;

            case WM_DROPFILES: {
                HDROP hDrop = reinterpret_cast<HDROP>(wParam);
                if (hDrop && ui) {
                    POINT pt{};
                    (void)::DragQueryPoint(hDrop, &pt);
                    const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFFu, nullptr, 0u);
                    keel::Array<keel::String> ownedPaths;
                    keel::Array<keel::StringRef> pathRefs;
                    ownedPaths.reserve(count);
                    pathRefs.reserve(count);
                    for (UINT i = 0u; i < count; ++i) {
                        const UINT charLen = ::DragQueryFileW(hDrop, i, nullptr, 0u);
                        if (charLen == 0u) continue;
                        keel::Array<wchar_t> wbuf(keel::as<keel::usize>(charLen) + 1u, L'\0');
                        if (::DragQueryFileW(hDrop, i, wbuf.data(), charLen + 1u) > 0u) {
                            ownedPaths.push_back(WideToUtf8(wbuf.data(), keel::as<keel::i32>(charLen)));
                        }
                    }
                    for (const keel::String& s : ownedPaths) {
                        pathRefs.push_back(s.as_ref());
                    }
                    if (!pathRefs.empty()) {
                        ui->OnFilesDropped(
                            pathRefs.as_ref(),
                            Vec2{keel::as<keel::f32>(pt.x), keel::as<keel::f32>(pt.y)}
                        );
                    }
                }
                if (hDrop) {
                    ::DragFinish(hDrop);
                }
                return 0;
            }

            case WM_KILLFOCUS:
                pendingHighSurrogate = 0u;
                if (ui) {
                    ui->OnTextCompositionEnd();
                    if (capturedButtons != 0u) {
                        if (capturedButtons & 1u) ui->OnMouseUp(1);
                        if (capturedButtons & 2u) ui->OnMouseUp(2);
                        if (capturedButtons & 4u) ui->OnMouseUp(3);
                    }
                    ui->SetKeyModifiers({});
                }
                capturedButtons = 0u;
                return 0;

            default:
                break;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            if (cs && cs->lpCreateParams) {
                ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }
        }
        auto* self = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (self) {
            return self->HandleMessage(hWnd, msg, wParam, lParam);
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

Win32PlatformWindow::Win32PlatformWindow() noexcept
    : m_impl(keel::make_unique<Impl>()) {
}

Win32PlatformWindow::~Win32PlatformWindow() {
    Destroy();
}

bool Win32PlatformWindow::IsRuntimeAvailable() noexcept {
    return true;
}

bool Win32PlatformWindow::Create(
    const PlatformWindowDesc& desc,
    Context& ui,
    WindowHostCallbacks callbacks
) {
    if (!m_impl) return false;
    m_impl->DestroyAll();

    if (desc.highDpi) {
        EnableBestProcessDpiAwareness();
    }

    m_impl->hInstance = ::GetModuleHandleW(nullptr);
    m_impl->ui = &ui;
    m_impl->callbacks = keel::move(callbacks);
    m_impl->highDpi = desc.highDpi;
    m_impl->vsync = desc.vsync;
    m_impl->minWidth = keel::max(desc.minWidth, 64);
    m_impl->minHeight = keel::max(desc.minHeight, 64);
    m_impl->closeRequested = false;
    m_impl->minimized = false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = &Impl::StaticWndProc;
    wc.hInstance = m_impl->hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    if (!::GetClassInfoExW(m_impl->hInstance, kWindowClassName, &wc)) {
        if (!::RegisterClassExW(&wc)) {
            keel::eprintln("[Win32PlatformWindow] RegisterClassExW() failed");
            return false;
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (desc.borderless || desc.fullscreen) {
        style = WS_POPUP;
    } else if (!desc.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    const DWORD exStyle = WS_EX_APPWINDOW;

    keel::i32 reqW = keel::max(desc.width, 64);
    keel::i32 reqH = keel::max(desc.height, 64);
    keel::i32 winX = CW_USEDEFAULT;
    keel::i32 winY = CW_USEDEFAULT;

    m_impl->fullscreen = desc.fullscreen;
    if (desc.fullscreen) {
        reqW = ::GetSystemMetrics(SM_CXSCREEN);
        reqH = ::GetSystemMetrics(SM_CYSCREEN);
        winX = 0;
        winY = 0;
    }

    RECT rc{0, 0, reqW, reqH};
    (void)::AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    const keel::i32 outerW = rc.right - rc.left;
    const keel::i32 outerH = rc.bottom - rc.top;

    const keel::Array<wchar_t> wideTitle = Utf8ToWideZ(desc.title);
    m_impl->hwnd = ::CreateWindowExW(
        exStyle,
        kWindowClassName,
        wideTitle.data(),
        style,
        winX,
        winY,
        outerW,
        outerH,
        nullptr,
        nullptr,
        m_impl->hInstance,
        m_impl.get()
    );
    if (!m_impl->hwnd) {
        keel::eprintln("[Win32PlatformWindow] CreateWindowExW() failed");
        m_impl->DestroyAll();
        return false;
    }

    ApplyDwmDarkTitlebar(m_impl->hwnd);
    ::DragAcceptFiles(m_impl->hwnd, TRUE);
    if (const auto iconAsset = Assets::Find("assets/guikit-icon-inter850-1024.png"); iconAsset && !iconAsset->Empty()) {
        keel::u32 srcW = 0u;
        keel::u32 srcH = 0u;
        keel::Array<keel::u32> rgba;
        if (DecodeImageFromMemory(iconAsset->Bytes(), srcW, srcH, rgba) && srcW > 0u && srcH > 0u) {
            m_impl->iconBig = CreateDownsampledWin32Icon(rgba.data(), srcW, srcH, 64u);
            m_impl->iconSmall = CreateDownsampledWin32Icon(rgba.data(), srcW, srcH, 32u);
            if (m_impl->iconBig) {
                ::SendMessageW(m_impl->hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_impl->iconBig));
            }
            if (m_impl->iconSmall) {
                ::SendMessageW(m_impl->hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_impl->iconSmall));
            }
        }
    }
    m_impl->UpdateDpiMetrics(QueryWindowDpi(m_impl->hwnd));

    // Preload system cursor handles
    for (keel::usize i = 0u; i < 9u; ++i) {
        m_impl->cursors[i] = ::LoadCursorW(nullptr, MapWin32CursorId(keel::from_underlying<CursorType>(i)));
    }

    if (desc.graphicsApi != GraphicsApi::None) {
        if (!m_impl->InitWgl()) {
            m_impl->DestroyAll();
            return false;
        }
    }

    m_impl->wakeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HWND rawHwnd = m_impl->hwnd;
    ui.SetClipboardCallbacks(
        [rawHwnd]() -> keel::String { return ReadWin32Clipboard(rawHwnd); },
        [rawHwnd](keel::StringRef txt) { WriteWin32Clipboard(rawHwnd, txt); }
    );

    const keel::i32 showCmd = desc.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    ::ShowWindow(m_impl->hwnd, showCmd);
    // Win32 ignores nCmdShow on the process's first ShowWindow() call when the parent
    // provided STARTF_USESHOWWINDOW (e.g. SW_HIDE or SW_SHOWMINNOACTIVE). Calling it
    // again when still hidden/iconic guarantees the requested window state is applied.
    if (!::IsWindowVisible(m_impl->hwnd) || ::IsIconic(m_impl->hwnd)) {
        ::ShowWindow(m_impl->hwnd, showCmd);
    }
    (void)::BringWindowToTop(m_impl->hwnd);
    (void)::SetForegroundWindow(m_impl->hwnd);
    ::UpdateWindow(m_impl->hwnd);
    m_impl->minimized = (::IsIconic(m_impl->hwnd) != FALSE);

    RECT clientRc{};
    if (::GetClientRect(m_impl->hwnd, &clientRc)) {
        m_impl->width = keel::max(1, keel::as<keel::i32>(clientRc.right - clientRc.left));
        m_impl->height = keel::max(1, keel::as<keel::i32>(clientRc.bottom - clientRc.top));
    } else {
        m_impl->width = reqW;
        m_impl->height = reqH;
    }

    m_impl->alive = true;
    return true;
}

void Win32PlatformWindow::Destroy() noexcept {
    if (m_impl) {
        m_impl->DestroyAll();
    }
}

bool Win32PlatformWindow::PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) {
    if (!m_impl || !m_impl->alive || !m_impl->hwnd) {
        return false;
    }

    bool hadEvents = false;
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_impl->closeRequested = true;
            return false;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        hadEvents = true;
        if (m_impl->closeRequested) {
            return false;
        }
    }

    const bool shouldSleep = !hadEvents && sleepTimeoutMs != 0 &&
        (m_impl->minimized || (reactiveMode && !ui.NeedsRepaint()));
    if (shouldSleep) {
        const DWORD waitMs = (sleepTimeoutMs < 0) ? INFINITE : keel::as<DWORD>(sleepTimeoutMs);
        if (m_impl->wakeEvent) {
            (void)::MsgWaitForMultipleObjects(1u, &m_impl->wakeEvent, FALSE, waitMs, QS_ALLINPUT);
        } else {
            (void)::MsgWaitForMultipleObjects(0u, nullptr, FALSE, waitMs, QS_ALLINPUT);
        }

        while (::PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_impl->closeRequested = true;
                return false;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (m_impl->closeRequested) {
                return false;
            }
        }
    }

    return !m_impl->closeRequested;
}

void Win32PlatformWindow::WakeUp() noexcept {
    if (m_impl && m_impl->wakeEvent) {
        (void)::SetEvent(m_impl->wakeEvent);
    }
}

bool Win32PlatformWindow::MakeContextCurrent() noexcept {
    if (!m_impl || !m_impl->hdc || !m_impl->hglrc) {
        return true;
    }
    if (::wglGetCurrentContext() == m_impl->hglrc && ::wglGetCurrentDC() == m_impl->hdc) {
        return true;
    }
    return ::wglMakeCurrent(m_impl->hdc, m_impl->hglrc) != FALSE;
}

void Win32PlatformWindow::SwapBuffers() noexcept {
    if (m_impl && m_impl->hdc) {
        (void)::SwapBuffers(m_impl->hdc);
    }
}

void Win32PlatformWindow::WaitForVBlank() noexcept {
    if (!m_impl || !m_impl->vsync || !m_impl->hglrc || !m_impl->dwmPacingActive) {
        return;
    }
    if (m_impl->dwmFlush) {
        if (FAILED(m_impl->dwmFlush())) {
            // DWM composition became unavailable (e.g. RDP/headless transition): fall back to WGL swap interval
            m_impl->dwmPacingActive = false;
            if (m_impl->wglSwapIntervalExt) {
                (void)MakeContextCurrent();
                (void)m_impl->wglSwapIntervalExt(1);
            }
        }
    }
}

GlProcAddressFn Win32PlatformWindow::GetGlProcAddressFn() const noexcept {
    return &WglProcLoaderThunk;
}

keel::i32 Win32PlatformWindow::GetFramebufferWidth() const noexcept {
    return m_impl ? m_impl->width : 0;
}

keel::i32 Win32PlatformWindow::GetFramebufferHeight() const noexcept {
    return m_impl ? m_impl->height : 0;
}

keel::f32 Win32PlatformWindow::GetDpiScale() const noexcept {
    return m_impl ? m_impl->dpiScale : 1.0f;
}

keel::f32 Win32PlatformWindow::GetDisplayPixelsPerMm() const noexcept {
    return m_impl ? m_impl->pixelsPerMm : (96.0f / 25.4f);
}

bool Win32PlatformWindow::IsMinimized() const noexcept {
    return m_impl ? m_impl->minimized : false;
}

bool Win32PlatformWindow::IsLiveResizeFrame() const noexcept {
    return m_impl ? m_impl->liveResizeFrame : false;
}

bool Win32PlatformWindow::IsVsyncEnabled() const noexcept {
    return m_impl ? m_impl->vsync : true;
}

void Win32PlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    if (!m_impl) return;
    m_impl->vsync = enabled;
    if (m_impl->hglrc) {
        (void)MakeContextCurrent();
        m_impl->ApplySwapInterval();
    }
}

void Win32PlatformWindow::SetCursor(CursorType cursor) noexcept {
    if (!m_impl || cursor == m_impl->activeCursor) return;
    m_impl->activeCursor = cursor;
    const keel::usize idx = keel::as<keel::usize>(cursor);
    if (idx < 9u && m_impl->cursors[idx]) {
        ::SetCursor(m_impl->cursors[idx]);
    }
}

void Win32PlatformWindow::SetTextInputActive(bool active, Rect caretRectPx) noexcept {
    if (!m_impl || !m_impl->hwnd) return;
    const bool changed = (active != m_impl->imeActive) ||
        (active && (keel::abs(caretRectPx.x - m_impl->lastImeCaretPx.x) > 0.5f ||
                    keel::abs(caretRectPx.y - m_impl->lastImeCaretPx.y) > 0.5f ||
                    keel::abs(caretRectPx.h - m_impl->lastImeCaretPx.h) > 0.5f));
    m_impl->imeActive = active;
    if (active && changed) {
        m_impl->lastImeCaretPx = caretRectPx;
        m_impl->UpdateImmCaretPosition(caretRectPx);
    }
}

void Win32PlatformWindow::OpenUrl(keel::StringRef url) const noexcept {
    if (url.empty()) return;
    const keel::Array<wchar_t> wideUrl = Utf8ToWideZ(url);
    (void)::ShellExecuteW(nullptr, L"open", wideUrl.data(), nullptr, nullptr, SW_SHOWNORMAL);
}

keel::StringRef Win32PlatformWindow::GetPlatformName() const noexcept {
    return "Win32 (WGL + OpenGL 3.3 Core)";
}

} // namespace guikit::native

#else // Non-Windows stub

namespace guikit::native {

struct Win32PlatformWindow::Impl {};

Win32PlatformWindow::Win32PlatformWindow() noexcept = default;
Win32PlatformWindow::~Win32PlatformWindow() = default;
bool Win32PlatformWindow::IsRuntimeAvailable() noexcept { return false; }
bool Win32PlatformWindow::Create(const PlatformWindowDesc&, Context&, WindowHostCallbacks) { return false; }
void Win32PlatformWindow::Destroy() noexcept {}
bool Win32PlatformWindow::PumpEvents(Context&, bool, keel::i32) { return false; }
void Win32PlatformWindow::WakeUp() noexcept {}
bool Win32PlatformWindow::MakeContextCurrent() noexcept { return false; }
void Win32PlatformWindow::SwapBuffers() noexcept {}
void Win32PlatformWindow::WaitForVBlank() noexcept {}
GlProcAddressFn Win32PlatformWindow::GetGlProcAddressFn() const noexcept { return nullptr; }
keel::i32 Win32PlatformWindow::GetFramebufferWidth() const noexcept { return 0; }
keel::i32 Win32PlatformWindow::GetFramebufferHeight() const noexcept { return 0; }
keel::f32 Win32PlatformWindow::GetDpiScale() const noexcept { return 1.0f; }
keel::f32 Win32PlatformWindow::GetDisplayPixelsPerMm() const noexcept { return 96.0f / 25.4f; }
bool Win32PlatformWindow::IsMinimized() const noexcept { return false; }
bool Win32PlatformWindow::IsLiveResizeFrame() const noexcept { return false; }
bool Win32PlatformWindow::IsVsyncEnabled() const noexcept { return true; }
void Win32PlatformWindow::SetVsyncEnabled(bool) noexcept {}
void Win32PlatformWindow::SetCursor(CursorType) noexcept {}
void Win32PlatformWindow::SetTextInputActive(bool, Rect) noexcept {}
void Win32PlatformWindow::OpenUrl(keel::StringRef) const noexcept {}
keel::StringRef Win32PlatformWindow::GetPlatformName() const noexcept { return "Win32 (Unavailable)"; }

} // namespace guikit::native

#endif
