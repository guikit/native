#include "guikit/native/emscripten_platform_window.hpp"

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include "guikit/context.hpp"
#include "keel/array.h"
#include "keel/format.h"
#include "keel/math.h"
#include "keel/string.h"
#include "keel/time.h"
#include "keel/utf8.h"

#endif

namespace guikit::native {

#if defined(__EMSCRIPTEN__)

namespace {

static constexpr const char* kCanvasSelector = "#canvas";

void* EmscriptenGlProcSentinel(const char* /*procName*/) noexcept {
    // GlEs3Api binds GLES3/WebGL2 entry points directly on Emscripten; non-null sentinel signals active context.
    static keel::u8 s_sentinel = 1u;
    return &s_sentinel;
}

[[nodiscard]] const char* MapCssCursor(CursorType cursor) noexcept {
    switch (cursor) {
        case CursorType::Pointer:    return "pointer";
        case CursorType::Text:       return "text";
        case CursorType::ResizeEW:   return "ew-resize";
        case CursorType::ResizeNS:   return "ns-resize";
        case CursorType::ResizeNWSE: return "nwse-resize";
        case CursorType::ResizeNESW: return "nesw-resize";
        case CursorType::Move:       return "move";
        case CursorType::NotAllowed: return "not-allowed";
        case CursorType::Default:
        default:                     return "default";
    }
}

[[nodiscard]] Key MapDomKey(keel::StringRef code, keel::StringRef key) noexcept {
    if (key == "Backspace" || code == "Backspace") return Key::Backspace;
    if (key == "Delete" || code == "Delete") return Key::Delete;
    if (key == "Enter" || code == "Enter" || code == "NumpadEnter") return Key::Enter;
    if (key == "Escape" || code == "Escape") return Key::Escape;
    if (key == "Tab" || code == "Tab") return Key::Tab;
    if (code == "Space" || key == " ") return Key::Space;
    if (key == "ArrowLeft" || code == "ArrowLeft") return Key::Left;
    if (key == "ArrowRight" || code == "ArrowRight") return Key::Right;
    if (key == "ArrowUp" || code == "ArrowUp") return Key::Up;
    if (key == "ArrowDown" || code == "ArrowDown") return Key::Down;
    if (key == "Home" || code == "Home") return Key::Home;
    if (key == "End" || code == "End") return Key::End;
    if (key == "PageUp" || code == "PageUp") return Key::PageUp;
    if (key == "PageDown" || code == "PageDown") return Key::PageDown;

    // Match shortcuts by physical key code so Ctrl+C / Ctrl+V / Ctrl+Z work across non-Latin layouts
    if (code == "KeyA") return Key::A;
    if (code == "KeyC") return Key::C;
    if (code == "KeyD") return Key::D;
    if (code == "KeyF") return Key::F;
    if (code == "KeyG") return Key::G;
    if (code == "KeyH") return Key::H;
    if (code == "KeyK") return Key::K;
    if (code == "KeyV") return Key::V;
    if (code == "KeyX") return Key::X;
    if (code == "KeyY") return Key::Y;
    if (code == "KeyZ") return Key::Z;
    if (code == "Slash" || code == "NumpadDivide" || key == "/") return Key::Slash;
    if (code == "Equal" || code == "NumpadAdd" || key == "+" || key == "=") return Key::Plus;
    if (code == "Minus" || code == "NumpadSubtract" || key == "-") return Key::Minus;
    if (code == "Digit0" || code == "Numpad0" || key == "0") return Key::Zero;
    if (code == "F3" || key == "F3") return Key::F3;
    if (code == "F5" || key == "F5") return Key::F5;
    if (code == "F9" || key == "F9") return Key::F9;
    if (code == "F10" || key == "F10") return Key::F10;
    if (code == "F11" || key == "F11") return Key::F11;
    return Key::None;
}

[[nodiscard]] bool IsPrintableUtf8SingleChar(keel::StringRef key) noexcept {
    if (key.empty() || key.size() > 4u) return false;
    const keel::u8 first = keel::as<keel::u8>(key[0]);
    if (first < 0x20u || first == 0x7Fu) return false;
    // Named DOM keys like "Tab", "End", "Alt", "Meta", "Dead", "Home", "F1".."F12" are ASCII multi-byte strings
    if (first < 0x80u) {
        return key.size() == 1u;
    }
    return keel::utf8_codepoint_count(key) == 1u;
}

[[nodiscard]] bool ShouldPreventDefaultKey(Key logicalKey, const KeyModifiers& mods) noexcept {
    if (logicalKey == Key::Tab || logicalKey == Key::Backspace ||
        logicalKey == Key::Left || logicalKey == Key::Right ||
        logicalKey == Key::Up || logicalKey == Key::Down ||
        logicalKey == Key::PageUp || logicalKey == Key::PageDown ||
        logicalKey == Key::Home || logicalKey == Key::End ||
        logicalKey == Key::Space || logicalKey == Key::F3) {
        return true;
    }
    if (mods.ctrl) {
        // Leave Ctrl+V un-prevented so the browser fires the synchronous DOM `paste` event with OS clipboard text
        if (logicalKey == Key::V) {
            return false;
        }
        return logicalKey == Key::A || logicalKey == Key::C || logicalKey == Key::D ||
               logicalKey == Key::F || logicalKey == Key::G || logicalKey == Key::H ||
               logicalKey == Key::K || logicalKey == Key::X || logicalKey == Key::Y ||
               logicalKey == Key::Z || logicalKey == Key::Plus || logicalKey == Key::Minus ||
               logicalKey == Key::Zero || logicalKey == Key::Slash;
    }
    return false;
}

[[nodiscard]] keel::i32 MapDomMouseButton(keel::u16 domButton) noexcept {
    if (domButton == 0u) return 1; // Left
    if (domButton == 1u) return 2; // Middle
    if (domButton == 2u) return 3; // Right
    return 0;
}

// clang-format off
EM_JS(keel::f64, guikit_js_canvas_rect_left, (), {
    var el = document.querySelector('#canvas');
    return el ? el.getBoundingClientRect().left : 0;
});

EM_JS(keel::f64, guikit_js_canvas_rect_top, (), {
    var el = document.querySelector('#canvas');
    return el ? el.getBoundingClientRect().top : 0;
});

EM_JS(void, guikit_js_focus_canvas, (), {
    var el = document.querySelector('#canvas');
    if (el && typeof el.focus === 'function') {
        el.focus({ preventScroll: true });
    }
});

EM_JS(void, guikit_js_write_clipboard, (const char* ptr, keel::i32 len), {
    var txt = new TextDecoder().decode(HEAPU8.subarray(ptr, ptr + len));
    if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
        navigator.clipboard.writeText(txt).catch(function() {});
    }
});

EM_JS(void, guikit_js_set_cursor, (const char* ptr, keel::i32 len), {
    var el = document.querySelector('#canvas');
    if (el) {
        el.style.cursor = new TextDecoder().decode(HEAPU8.subarray(ptr, ptr + len));
    }
});

EM_JS(void, guikit_js_open_url, (const char* ptr, keel::i32 len), {
    var target = new TextDecoder().decode(HEAPU8.subarray(ptr, ptr + len));
    window.open(target, '_blank', 'noopener');
});

EM_JS(void, guikit_js_install_paste_listener, (), {
    if (!window.__guikitPasteInstalled) {
        window.__guikitPasteInstalled = true;
        window.addEventListener('paste', function(ev) {
            var data = ev.clipboardData || window.clipboardData;
            if (!data) return;
            var txt = data.getData('text/plain');
            if (typeof txt !== 'string' || txt.length === 0) return;
            var bytes = new TextEncoder().encode(txt);
            var ptr = _guikit_native_emscripten_paste_alloc(bytes.length);
            if (!ptr) return;
            HEAPU8.set(bytes, ptr);
            _guikit_native_emscripten_paste_commit(bytes.length);
            ev.preventDefault();
        });
    }
});
// clang-format on

void QueryCanvasRectOriginCss(keel::f64& outLeft, keel::f64& outTop) noexcept {
    outLeft = guikit_js_canvas_rect_left();
    outTop = guikit_js_canvas_rect_top();
}

void FocusBrowserCanvas() noexcept {
    guikit_js_focus_canvas();
}

void WriteBrowserClipboardAsync(const char* utf8Text, keel::usize len) noexcept {
    if (!utf8Text) return;
    guikit_js_write_clipboard(utf8Text, keel::as<keel::i32>(len));
}

using PasteCallbackFn = void(*)(void* userData, keel::StringRef utf8Text);
void* g_pasteUserData = nullptr;
PasteCallbackFn g_pasteCallback = nullptr;
keel::Array<char> g_pasteScratch;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
char* guikit_native_emscripten_paste_alloc(keel::i32 byteLen) {
    if (byteLen <= 0 || byteLen > 1024 * 1024) {
        return nullptr;
    }
    g_pasteScratch.resize(keel::as<keel::usize>(byteLen), '\0');
    return g_pasteScratch.data();
}

EMSCRIPTEN_KEEPALIVE
void guikit_native_emscripten_paste_commit(keel::i32 byteLen) {
    if (g_pasteCallback && g_pasteUserData && byteLen > 0 &&
        keel::as<keel::usize>(byteLen) <= g_pasteScratch.size()) {
        g_pasteCallback(g_pasteUserData, keel::StringRef(g_pasteScratch.data(), keel::as<keel::usize>(byteLen)));
    }
}

} // extern "C"

struct EmscriptenPlatformWindow::Impl {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE webglCtx{0};
    Context* ui{nullptr};
    WindowHostCallbacks callbacks{};
    keel::Function<bool()> stepFn;

    keel::i32 width{1280};
    keel::i32 height{720};
    keel::f32 dpiScale{1.0f};
    bool highDpi{true};
    bool vsync{true};
    bool minimized{false};
    bool skipRenderThisStep{false};
    bool wakeRequested{false};
    bool hadEventSinceLastStep{false};
    bool alive{false};
    bool touchActive{false};
    keel::u64 lastRenderNs{0u};

    CursorType activeCursor{CursorType::Default};
    keel::String clipboardText;

    void SyncCanvasSize(bool notifyCallbacks) noexcept {
        keel::f64 cssW = 0.0;
        keel::f64 cssH = 0.0;
        if (emscripten_get_element_css_size(kCanvasSelector, &cssW, &cssH) != EMSCRIPTEN_RESULT_SUCCESS ||
            cssW < 64.0 || cssH < 64.0) {
            return;
        }
        keel::f64 dpr = highDpi ? emscripten_get_device_pixel_ratio() : 1.0;
        if (dpr <= 0.1 || dpr > 8.0) {
            dpr = 1.0;
        }
        const keel::i32 physW = keel::max(64, keel::round_i32(cssW * dpr));
        const keel::i32 physH = keel::max(64, keel::round_i32(cssH * dpr));
        const keel::f32 newDpr = keel::as<keel::f32>(dpr);

        if (physW != width || physH != height || keel::abs(newDpr - dpiScale) > 0.01f) {
            width = physW;
            height = physH;
            dpiScale = newDpr;
            (void)emscripten_set_canvas_element_size(kCanvasSelector, width, height);
            if (notifyCallbacks) {
                if (callbacks.onResize) {
                    callbacks.onResize(width, height);
                }
                if (ui) {
                    ui->InvalidateAll();
                    ui->RequestRepaint(2);
                }
                hadEventSinceLastStep = true;
            }
        }
    }

    [[nodiscard]] Vec2 ClientToPhysical(keel::f64 clientX, keel::f64 clientY) const noexcept {
        keel::f64 left = 0.0;
        keel::f64 top = 0.0;
        QueryCanvasRectOriginCss(left, top);
        return Vec2{
            keel::as<keel::f32>((clientX - left) * keel::as<keel::f64>(dpiScale)),
            keel::as<keel::f32>((clientY - top) * keel::as<keel::f64>(dpiScale))
        };
    }

    void ApplyModifiers(EM_BOOL ctrl, EM_BOOL meta, EM_BOOL shift, EM_BOOL alt) noexcept {
        if (!ui) return;
        KeyModifiers mods{};
        mods.ctrl = (ctrl != 0) || (meta != 0);
        mods.shift = (shift != 0);
        mods.alt = (alt != 0);
        ui->SetKeyModifiers(mods);
    }

    void OnDomPaste(keel::StringRef text) {
        if (!ui) return;
        clipboardText = keel::String::from(text);
        KeyModifiers mods{};
        mods.ctrl = true;
        ui->SetKeyModifiers(mods);
        ui->OnKeyDown(Key::V, mods);
        hadEventSinceLastStep = true;
        ui->RequestRepaint(2);
    }

    static EM_BOOL OnMouseMove(keel::i32 /*type*/, const EmscriptenMouseEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;
        self->ApplyModifiers(e->ctrlKey, e->metaKey, e->shiftKey, e->altKey);
        self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(e->clientX), keel::as<keel::f64>(e->clientY)));
        self->hadEventSinceLastStep = true;
        return EM_FALSE;
    }

    static EM_BOOL OnMouseDown(keel::i32 /*type*/, const EmscriptenMouseEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;
        FocusBrowserCanvas();
        self->ApplyModifiers(e->ctrlKey, e->metaKey, e->shiftKey, e->altKey);
        self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(e->clientX), keel::as<keel::f64>(e->clientY)));
        const keel::i32 btn = MapDomMouseButton(keel::as<keel::u16>(e->button));
        if (btn > 0) {
            self->ui->OnMouseDown(btn);
            self->hadEventSinceLastStep = true;
            return EM_TRUE;
        }
        return EM_FALSE;
    }

    static EM_BOOL OnMouseUp(keel::i32 /*type*/, const EmscriptenMouseEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;
        self->ApplyModifiers(e->ctrlKey, e->metaKey, e->shiftKey, e->altKey);
        self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(e->clientX), keel::as<keel::f64>(e->clientY)));
        const keel::i32 btn = MapDomMouseButton(keel::as<keel::u16>(e->button));
        if (btn > 0) {
            self->ui->OnMouseUp(btn);
            self->hadEventSinceLastStep = true;
        }
        return EM_FALSE;
    }

    static EM_BOOL OnWheel(keel::i32 /*type*/, const EmscriptenWheelEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;
        self->ApplyModifiers(e->mouse.ctrlKey, e->mouse.metaKey, e->mouse.shiftKey, e->mouse.altKey);
        self->ui->OnMouseMove(
            self->ClientToPhysical(keel::as<keel::f64>(e->mouse.clientX), keel::as<keel::f64>(e->mouse.clientY))
        );

        keel::f64 scale = 1.0 / 100.0; // DOM_DELTA_PIXEL
        if (e->deltaMode == DOM_DELTA_LINE) {
            scale = 1.0 / 3.0;
        } else if (e->deltaMode == DOM_DELTA_PAGE) {
            scale = 3.0;
        }
        const keel::f32 dx = keel::as<keel::f32>(-e->deltaX * scale);
        const keel::f32 dy = keel::as<keel::f32>(-e->deltaY * scale);
        self->ui->OnMouseWheel(dx, dy);
        self->hadEventSinceLastStep = true;
        return EM_TRUE;
    }

    static EM_BOOL OnTouchStart(keel::i32 /*type*/, const EmscriptenTouchEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e || e->numTouches <= 0) return EM_FALSE;
        FocusBrowserCanvas();
        const EmscriptenTouchPoint& tp = e->touches[0];
        self->touchActive = true;
        self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(tp.clientX), keel::as<keel::f64>(tp.clientY)));
        self->ui->OnMouseDown(1);
        self->hadEventSinceLastStep = true;
        return EM_TRUE;
    }

    static EM_BOOL OnTouchMove(keel::i32 /*type*/, const EmscriptenTouchEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e || e->numTouches <= 0 || !self->touchActive) return EM_FALSE;
        const EmscriptenTouchPoint& tp = e->touches[0];
        self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(tp.clientX), keel::as<keel::f64>(tp.clientY)));
        self->hadEventSinceLastStep = true;
        return EM_TRUE;
    }

    static EM_BOOL OnTouchEnd(keel::i32 /*type*/, const EmscriptenTouchEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !self->touchActive) return EM_FALSE;
        if (e && e->numTouches > 0) {
            const EmscriptenTouchPoint& tp = e->touches[0];
            self->ui->OnMouseMove(self->ClientToPhysical(keel::as<keel::f64>(tp.clientX), keel::as<keel::f64>(tp.clientY)));
        }
        self->touchActive = false;
        self->ui->OnMouseUp(1);
        self->hadEventSinceLastStep = true;
        return EM_TRUE;
    }

    static EM_BOOL OnKeyDown(keel::i32 /*type*/, const EmscriptenKeyboardEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;

        KeyModifiers mods{};
        mods.ctrl = (e->ctrlKey != 0) || (e->metaKey != 0);
        mods.shift = (e->shiftKey != 0);
        mods.alt = (e->altKey != 0);
        self->ui->SetKeyModifiers(mods);

        const keel::StringRef code(e->code);
        const keel::StringRef key(e->key);
        const Key logicalKey = MapDomKey(code, key);
        if (logicalKey != Key::None) {
            // Defer Ctrl+V dispatch to the browser `paste` event so OS clipboard content is ingested synchronously
            if (!(mods.ctrl && logicalKey == Key::V)) {
                self->ui->OnKeyDown(logicalKey, mods);
            }
        }

        if (!mods.ctrl && !mods.alt && IsPrintableUtf8SingleChar(key)) {
            self->ui->OnTextInput(key);
        }

        self->hadEventSinceLastStep = true;
        return ShouldPreventDefaultKey(logicalKey, mods) ? EM_TRUE : EM_FALSE;
    }

    static EM_BOOL OnKeyUp(keel::i32 /*type*/, const EmscriptenKeyboardEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui || !e) return EM_FALSE;
        self->ApplyModifiers(e->ctrlKey, e->metaKey, e->shiftKey, e->altKey);
        self->hadEventSinceLastStep = true;
        return EM_FALSE;
    }

    static EM_BOOL OnBlur(keel::i32 /*type*/, const EmscriptenFocusEvent* /*e*/, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !self->ui) return EM_FALSE;
        self->ui->SetKeyModifiers({});
        self->touchActive = false;
        return EM_FALSE;
    }

    static EM_BOOL OnResize(keel::i32 /*type*/, const EmscriptenUiEvent* /*e*/, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (self) {
            self->SyncCanvasSize(true);
        }
        return EM_FALSE;
    }

    static EM_BOOL OnVisibilityChange(keel::i32 /*type*/, const EmscriptenVisibilityChangeEvent* e, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        if (!self || !e) return EM_FALSE;
        self->minimized = (e->hidden != 0);
        if (!self->minimized && self->ui) {
            self->ui->InvalidateAll();
            self->ui->RequestRepaint(2);
            self->hadEventSinceLastStep = true;
        }
        return EM_FALSE;
    }

    void UnregisterDomCallbacks() noexcept {
        (void)emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_mousedown_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_wheel_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchstart_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchmove_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchend_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_touchcancel_callback(kCanvasSelector, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, nullptr);
        (void)emscripten_set_visibilitychange_callback(nullptr, EM_FALSE, nullptr);
    }

    static void DispatchPasteThunk(void* ud, keel::StringRef text) {
        if (auto* self = static_cast<Impl*>(ud)) {
            self->OnDomPaste(text);
        }
    }

    void DestroyAll() noexcept {
        alive = false;
        UnregisterDomCallbacks();
        if (webglCtx > 0) {
            (void)emscripten_webgl_destroy_context(webglCtx);
            webglCtx = 0;
        }
        ui = nullptr;
        callbacks = {};
        stepFn = {};
    }
};

EmscriptenPlatformWindow::EmscriptenPlatformWindow() noexcept
    : m_impl(keel::make_unique<Impl>()) {
}

EmscriptenPlatformWindow::~EmscriptenPlatformWindow() {
    Destroy();
}

bool EmscriptenPlatformWindow::IsRuntimeAvailable() noexcept {
    return true;
}

bool EmscriptenPlatformWindow::Create(
    const PlatformWindowDesc& desc,
    Context& ui,
    WindowHostCallbacks callbacks
) {
    if (!m_impl) return false;
    m_impl->DestroyAll();

    m_impl->ui = &ui;
    m_impl->callbacks = keel::move(callbacks);
    m_impl->highDpi = desc.highDpi;
    m_impl->vsync = desc.vsync;
    m_impl->width = keel::max(desc.width, 64);
    m_impl->height = keel::max(desc.height, 64);
    m_impl->dpiScale = 1.0f;
    m_impl->minimized = false;
    m_impl->skipRenderThisStep = false;
    m_impl->hadEventSinceLastStep = true;

    const keel::String titleStr = keel::String::from(desc.title.as_ref());
    emscripten_set_window_title(titleStr.c_str());

    // Size the HTML5 canvas to match CSS viewport * devicePixelRatio before creating WebGL2 context
    m_impl->SyncCanvasSize(false);
    (void)emscripten_set_canvas_element_size(kCanvasSelector, m_impl->width, m_impl->height);

    if (desc.graphicsApi != GraphicsApi::None) {
        EmscriptenWebGLContextAttributes attrs{};
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.alpha = EM_FALSE;
        attrs.depth = EM_FALSE;
        attrs.stencil = EM_TRUE;
        attrs.antialias = EM_FALSE;
        attrs.premultipliedAlpha = EM_FALSE;
        attrs.preserveDrawingBuffer = EM_FALSE;
        attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
        attrs.failIfMajorPerformanceCaveat = EM_FALSE;
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;

        m_impl->webglCtx = emscripten_webgl_create_context(kCanvasSelector, &attrs);
        if (m_impl->webglCtx <= 0) {
            keel::eprintln("[EmscriptenPlatformWindow] Failed to create WebGL 2.0 context on #canvas");
            m_impl->DestroyAll();
            return false;
        }
        if (emscripten_webgl_make_context_current(m_impl->webglCtx) != EMSCRIPTEN_RESULT_SUCCESS) {
            keel::eprintln("[EmscriptenPlatformWindow] emscripten_webgl_make_context_current() failed");
            m_impl->DestroyAll();
            return false;
        }
    }

    // Bind DOM mouse, touch, wheel, keyboard, resize, and visibility events
    Impl* rawImpl = m_impl.get();
    (void)emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnMouseMove);
    (void)emscripten_set_mousedown_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnMouseDown);
    (void)emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnMouseUp);
    (void)emscripten_set_wheel_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnWheel);
    (void)emscripten_set_touchstart_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnTouchStart);
    (void)emscripten_set_touchmove_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnTouchMove);
    (void)emscripten_set_touchend_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnTouchEnd);
    (void)emscripten_set_touchcancel_callback(kCanvasSelector, rawImpl, EM_FALSE, &Impl::OnTouchEnd);
    (void)emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnKeyDown);
    (void)emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnKeyUp);
    (void)emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnBlur);
    (void)emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, rawImpl, EM_FALSE, &Impl::OnResize);
    (void)emscripten_set_visibilitychange_callback(rawImpl, EM_FALSE, &Impl::OnVisibilityChange);

    g_pasteUserData = rawImpl;
    g_pasteCallback = &Impl::DispatchPasteThunk;
    guikit_js_install_paste_listener();

    ui.SetClipboardCallbacks(
        [rawImpl]() -> keel::String { return rawImpl->clipboardText.clone(); },
        [rawImpl](keel::StringRef txt) {
            rawImpl->clipboardText = keel::String::from(txt);
            WriteBrowserClipboardAsync(rawImpl->clipboardText.c_str(), rawImpl->clipboardText.size());
        }
    );

    m_impl->lastRenderNs = keel::monotonic_ns();
    m_impl->alive = true;
    return true;
}

void EmscriptenPlatformWindow::Destroy() noexcept {
    if (m_impl) {
        if (g_pasteUserData == m_impl.get()) {
            g_pasteUserData = nullptr;
            g_pasteCallback = nullptr;
        }
        m_impl->DestroyAll();
    }
}

bool EmscriptenPlatformWindow::PumpEvents(Context& ui, bool reactiveMode, keel::i32 sleepTimeoutMs) {
    if (!m_impl || !m_impl->alive) return false;
    m_impl->skipRenderThisStep = false;
    m_impl->SyncCanvasSize(true);

    if (m_impl->wakeRequested) {
        m_impl->wakeRequested = false;
        m_impl->hadEventSinceLastStep = true;
        ui.RequestRepaint(1);
    }

    const bool hadEvent = m_impl->hadEventSinceLastStep;
    m_impl->hadEventSinceLastStep = false;

    if (m_impl->minimized) {
        m_impl->skipRenderThisStep = true;
        return true;
    }

    if (reactiveMode && !hadEvent && !ui.NeedsRepaint()) {
        if (sleepTimeoutMs <= 0) {
            m_impl->skipRenderThisStep = true;
            return true;
        }
        const keel::u64 nowNs = keel::monotonic_ns();
        const keel::f64 elapsedMs = keel::as<keel::f64>(nowNs - m_impl->lastRenderNs) * 1e-6;
        if (elapsedMs < keel::as<keel::f64>(sleepTimeoutMs)) {
            m_impl->skipRenderThisStep = true;
            return true;
        }
    }

    m_impl->lastRenderNs = keel::monotonic_ns();
    return true;
}

void EmscriptenPlatformWindow::WakeUp() noexcept {
    if (m_impl) {
        m_impl->wakeRequested = true;
    }
}

bool EmscriptenPlatformWindow::UsesMainLoopCallback() const noexcept {
    return true;
}

void EmscriptenPlatformWindow::RunMainLoopCallback(keel::Function<bool()> stepFn) {
    if (!m_impl) return;
    m_impl->stepFn = keel::move(stepFn);
    emscripten_set_main_loop_arg(
        [](void* arg) {
            auto* impl = static_cast<Impl*>(arg);
            if (!impl || !impl->stepFn || !impl->stepFn()) {
                emscripten_cancel_main_loop();
            }
        },
        m_impl.get(),
        0,
        true
    );
}

bool EmscriptenPlatformWindow::MakeContextCurrent() noexcept {
    if (!m_impl || m_impl->webglCtx <= 0) return true;
    return emscripten_webgl_make_context_current(m_impl->webglCtx) == EMSCRIPTEN_RESULT_SUCCESS;
}

void EmscriptenPlatformWindow::SwapBuffers() noexcept {
    // WebGL presents automatically at the end of the requestAnimationFrame callback.
}

GlProcAddressFn EmscriptenPlatformWindow::GetGlProcAddressFn() const noexcept {
    return (m_impl && m_impl->webglCtx > 0) ? &EmscriptenGlProcSentinel : nullptr;
}

keel::i32 EmscriptenPlatformWindow::GetFramebufferWidth() const noexcept {
    return m_impl ? m_impl->width : 0;
}

keel::i32 EmscriptenPlatformWindow::GetFramebufferHeight() const noexcept {
    return m_impl ? m_impl->height : 0;
}

keel::f32 EmscriptenPlatformWindow::GetDpiScale() const noexcept {
    return m_impl ? m_impl->dpiScale : 1.0f;
}

keel::f32 EmscriptenPlatformWindow::GetDisplayPixelsPerMm() const noexcept {
    return (96.0f * GetDpiScale()) / 25.4f;
}

bool EmscriptenPlatformWindow::IsMinimized() const noexcept {
    return m_impl ? m_impl->minimized : false;
}

bool EmscriptenPlatformWindow::ShouldRenderFrame() const noexcept {
    return m_impl && m_impl->alive && !m_impl->minimized && !m_impl->skipRenderThisStep;
}

bool EmscriptenPlatformWindow::IsLiveResizeFrame() const noexcept {
    return false;
}

bool EmscriptenPlatformWindow::IsVsyncEnabled() const noexcept {
    return m_impl ? m_impl->vsync : true;
}

void EmscriptenPlatformWindow::SetVsyncEnabled(bool enabled) noexcept {
    if (m_impl) {
        m_impl->vsync = enabled;
    }
}

void EmscriptenPlatformWindow::SetCursor(CursorType cursor) noexcept {
    if (!m_impl || cursor == m_impl->activeCursor) return;
    m_impl->activeCursor = cursor;
    const keel::StringRef css = MapCssCursor(cursor);
    guikit_js_set_cursor(css.data(), keel::as<keel::i32>(css.size()));
}

void EmscriptenPlatformWindow::OpenUrl(keel::StringRef url) const noexcept {
    if (url.empty()) return;
    const keel::String urlStr = keel::String::from(url);
    guikit_js_open_url(urlStr.c_str(), keel::as<keel::i32>(urlStr.size()));
}

keel::StringRef EmscriptenPlatformWindow::GetPlatformName() const noexcept {
    return "Emscripten + WebGL2";
}

#else // !__EMSCRIPTEN__

struct EmscriptenPlatformWindow::Impl {};

EmscriptenPlatformWindow::EmscriptenPlatformWindow() noexcept = default;
EmscriptenPlatformWindow::~EmscriptenPlatformWindow() = default;
bool EmscriptenPlatformWindow::IsRuntimeAvailable() noexcept { return false; }
bool EmscriptenPlatformWindow::Create(const PlatformWindowDesc&, Context&, WindowHostCallbacks) { return false; }
void EmscriptenPlatformWindow::Destroy() noexcept {}
bool EmscriptenPlatformWindow::PumpEvents(Context&, bool, keel::i32) { return false; }
void EmscriptenPlatformWindow::WakeUp() noexcept {}
bool EmscriptenPlatformWindow::UsesMainLoopCallback() const noexcept { return false; }
void EmscriptenPlatformWindow::RunMainLoopCallback(keel::Function<bool()>) {}
bool EmscriptenPlatformWindow::MakeContextCurrent() noexcept { return false; }
void EmscriptenPlatformWindow::SwapBuffers() noexcept {}
GlProcAddressFn EmscriptenPlatformWindow::GetGlProcAddressFn() const noexcept { return nullptr; }
keel::i32 EmscriptenPlatformWindow::GetFramebufferWidth() const noexcept { return 0; }
keel::i32 EmscriptenPlatformWindow::GetFramebufferHeight() const noexcept { return 0; }
keel::f32 EmscriptenPlatformWindow::GetDpiScale() const noexcept { return 1.0f; }
keel::f32 EmscriptenPlatformWindow::GetDisplayPixelsPerMm() const noexcept { return 96.0f / 25.4f; }
bool EmscriptenPlatformWindow::IsMinimized() const noexcept { return false; }
bool EmscriptenPlatformWindow::ShouldRenderFrame() const noexcept { return false; }
bool EmscriptenPlatformWindow::IsLiveResizeFrame() const noexcept { return false; }
bool EmscriptenPlatformWindow::IsVsyncEnabled() const noexcept { return true; }
void EmscriptenPlatformWindow::SetVsyncEnabled(bool) noexcept {}
void EmscriptenPlatformWindow::SetCursor(CursorType) noexcept {}
void EmscriptenPlatformWindow::OpenUrl(keel::StringRef) const noexcept {}
keel::StringRef EmscriptenPlatformWindow::GetPlatformName() const noexcept { return "Emscripten (Unavailable)"; }

#endif // __EMSCRIPTEN__

} // namespace guikit::native
