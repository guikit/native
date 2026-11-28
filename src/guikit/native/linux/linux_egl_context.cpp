#include "linux_egl_context.hpp"

#include "keel/dynlib.h"
#include "keel/format.h"
#include "keel/memory.h"

namespace guikit::native::linux_detail {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLNativeDisplayType = void*;
using EGLNativeWindowType = keel::usize;
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
static constexpr EGLint kEglNativeVisualId = 0x302E;
static constexpr EGLint kEglSurfaceType = 0x3033;
static constexpr EGLint kEglWindowBit = 0x0004;
static constexpr EGLint kEglRenderableType = 0x3040;
static constexpr EGLint kEglOpenGlEs2Bit = 0x0004;
static constexpr EGLint kEglOpenGlEs3Bit = 0x0040;
static constexpr EGLint kEglOpenGlBit = 0x0008;
static constexpr EGLint kEglHeight = 0x3056;
static constexpr EGLint kEglWidth = 0x3057;
static constexpr EGLint kEglContextClientVersion = 0x3098;
static constexpr EGLenum kEglOpenGlEsApi = 0x30A0u;
static constexpr EGLenum kEglOpenGlApi = 0x30A2u;

struct EglDispatchTable {
    keel::DynLib lib{};
    bool loaded{false};
    bool valid{false};

    using PfnGetDisplay = EGLDisplay(*)(EGLNativeDisplayType);
    using PfnGetPlatformDisplay = EGLDisplay(*)(EGLenum, void*, const keel::isize*);
    using PfnGetPlatformDisplayExt = EGLDisplay(*)(EGLenum, void*, const EGLint*);
    using PfnInitialize = EGLBoolean(*)(EGLDisplay, EGLint*, EGLint*);
    using PfnTerminate = EGLBoolean(*)(EGLDisplay);
    using PfnBindApi = EGLBoolean(*)(EGLenum);
    using PfnChooseConfig = EGLBoolean(*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    using PfnGetConfigAttrib = EGLBoolean(*)(EGLDisplay, EGLConfig, EGLint, EGLint*);
    using PfnCreateWindowSurface = EGLSurface(*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
    using PfnDestroySurface = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnCreateContext = EGLContext(*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    using PfnDestroyContext = EGLBoolean(*)(EGLDisplay, EGLContext);
    using PfnMakeCurrent = EGLBoolean(*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    using PfnSwapBuffers = EGLBoolean(*)(EGLDisplay, EGLSurface);
    using PfnSwapInterval = EGLBoolean(*)(EGLDisplay, EGLint);
    using PfnQuerySurface = EGLBoolean(*)(EGLDisplay, EGLSurface, EGLint, EGLint*);
    using PfnGetProcAddress = EglProcAddressRaw(*)(const char*);

    PfnGetDisplay GetDisplay{nullptr};
    PfnGetPlatformDisplay GetPlatformDisplay{nullptr};
    PfnGetPlatformDisplayExt GetPlatformDisplayExt{nullptr};
    PfnInitialize Initialize{nullptr};
    PfnTerminate Terminate{nullptr};
    PfnBindApi BindApi{nullptr};
    PfnChooseConfig ChooseConfig{nullptr};
    PfnGetConfigAttrib GetConfigAttrib{nullptr};
    PfnCreateWindowSurface CreateWindowSurface{nullptr};
    PfnDestroySurface DestroySurface{nullptr};
    PfnCreateContext CreateContext{nullptr};
    PfnDestroyContext DestroyContext{nullptr};
    PfnMakeCurrent MakeCurrent{nullptr};
    PfnSwapBuffers SwapBuffers{nullptr};
    PfnSwapInterval SwapInterval{nullptr};
    PfnQuerySurface QuerySurface{nullptr};
    PfnGetProcAddress GetProcAddress{nullptr};

    EglDispatchTable() noexcept {
        loaded = true;
        auto opened = keel::DynLib::open("libEGL.so.1");
        if (!opened) {
            opened = keel::DynLib::open("libEGL.so");
        }
        if (!opened) {
            return;
        }
        lib = keel::move(*opened);

        GetDisplay = lib.symbol<PfnGetDisplay>("eglGetDisplay");
        GetPlatformDisplay = lib.symbol<PfnGetPlatformDisplay>("eglGetPlatformDisplay");
        Initialize = lib.symbol<PfnInitialize>("eglInitialize");
        Terminate = lib.symbol<PfnTerminate>("eglTerminate");
        BindApi = lib.symbol<PfnBindApi>("eglBindAPI");
        ChooseConfig = lib.symbol<PfnChooseConfig>("eglChooseConfig");
        GetConfigAttrib = lib.symbol<PfnGetConfigAttrib>("eglGetConfigAttrib");
        CreateWindowSurface = lib.symbol<PfnCreateWindowSurface>("eglCreateWindowSurface");
        DestroySurface = lib.symbol<PfnDestroySurface>("eglDestroySurface");
        CreateContext = lib.symbol<PfnCreateContext>("eglCreateContext");
        DestroyContext = lib.symbol<PfnDestroyContext>("eglDestroyContext");
        MakeCurrent = lib.symbol<PfnMakeCurrent>("eglMakeCurrent");
        SwapBuffers = lib.symbol<PfnSwapBuffers>("eglSwapBuffers");
        SwapInterval = lib.symbol<PfnSwapInterval>("eglSwapInterval");
        QuerySurface = lib.symbol<PfnQuerySurface>("eglQuerySurface");
        GetProcAddress = lib.symbol<PfnGetProcAddress>("eglGetProcAddress");

        if (GetProcAddress) {
            void* rawExt = reinterpret_cast<void*>(GetProcAddress("eglGetPlatformDisplayEXT"));
            if (rawExt) {
                keel::mem_copy(&GetPlatformDisplayExt, &rawExt, sizeof(GetPlatformDisplayExt));
            }
        }

        valid = GetDisplay && Initialize && Terminate && BindApi && ChooseConfig &&
                CreateWindowSurface && DestroySurface && CreateContext &&
                DestroyContext && MakeCurrent && SwapBuffers && GetProcAddress;
    }
};

// Process-wide EGL dispatch singleton: safe across multiple windows and never dangling on window Destroy().
[[nodiscard]] const EglDispatchTable& GetEglTable() noexcept {
    static const EglDispatchTable s_table{};
    return s_table;
}

void* LinuxEglProcLoaderThunk(const char* procName) {
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid || !egl.GetProcAddress || !procName) {
        return nullptr;
    }
    return reinterpret_cast<void*>(egl.GetProcAddress(procName));
}

} // namespace

LinuxEglContext::~LinuxEglContext() noexcept {
    Destroy();
}

bool LinuxEglContext::IsRuntimeAvailable() noexcept {
    return GetEglTable().valid;
}

bool LinuxEglContext::ChooseConfig(EGLint renderableBit, EGLint alphaBits, EGLConfig& outCfg) noexcept {
    const EglDispatchTable& egl = GetEglTable();
    struct Profile {
        EGLint stencilBits;
        EGLint alpha;
    };
    const Profile profiles[4] = {
        {8, alphaBits},
        {8, (alphaBits == 0) ? 8 : 0},
        {0, alphaBits},
        {0, (alphaBits == 0) ? 8 : 0}
    };
    for (const Profile& prof : profiles) {
        EGLint attribs[24]{};
        keel::usize idx = 0u;
        attribs[idx++] = kEglSurfaceType;    attribs[idx++] = kEglWindowBit;
        attribs[idx++] = kEglRenderableType; attribs[idx++] = renderableBit;
        attribs[idx++] = kEglRedSize;        attribs[idx++] = 8;
        attribs[idx++] = kEglGreenSize;      attribs[idx++] = 8;
        attribs[idx++] = kEglBlueSize;       attribs[idx++] = 8;
        attribs[idx++] = kEglAlphaSize;      attribs[idx++] = prof.alpha;
        if (prof.stencilBits > 0) {
            attribs[idx++] = kEglStencilSize; attribs[idx++] = prof.stencilBits;
        }
        attribs[idx++] = kEglNone;

        EGLConfig cfg = nullptr;
        EGLint numConfigs = 0;
        if (egl.ChooseConfig(m_eglDisplay, attribs, &cfg, 1, &numConfigs) == kEglTrue && numConfigs > 0 && cfg) {
            outCfg = cfg;
            return true;
        }
    }
    return false;
}

bool LinuxEglContext::InitDisplayAndConfig(
    keel::u32 platformEnum,
    void* nativeDisplay,
    GraphicsApi requestedApi,
    keel::i32 alphaBits
) noexcept {
    Destroy();
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid) {
        keel::eprintln("[LinuxEglContext] Failed to load libEGL.so.1");
        return false;
    }
    if (egl.GetPlatformDisplay) {
        m_eglDisplay = egl.GetPlatformDisplay(platformEnum, nativeDisplay, nullptr);
    } else if (egl.GetPlatformDisplayExt) {
        m_eglDisplay = egl.GetPlatformDisplayExt(platformEnum, nativeDisplay, nullptr);
    }
    if (!m_eglDisplay) {
        m_eglDisplay = egl.GetDisplay(nativeDisplay);
    }
    if (!m_eglDisplay) {
        keel::eprintln("[LinuxEglContext] eglGetDisplay() failed");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (egl.Initialize(m_eglDisplay, &major, &minor) == kEglFalse) {
        keel::eprintln("[LinuxEglContext] eglInitialize() failed");
        m_eglDisplay = nullptr;
        return false;
    }

    const bool tryEs3First = (requestedApi != GraphicsApi::OpenGL3);
    m_boundEs = false;
    m_eglConfig = nullptr;

    if (tryEs3First && egl.BindApi(kEglOpenGlEsApi) == kEglTrue) {
        if (ChooseConfig(kEglOpenGlEs3Bit, alphaBits, m_eglConfig) ||
            ChooseConfig(kEglOpenGlEs2Bit, alphaBits, m_eglConfig)) {
            m_boundEs = true;
        }
    }

    if (!m_boundEs) {
        if (egl.BindApi(kEglOpenGlApi) == kEglFalse) {
            keel::eprintln("[LinuxEglContext] Failed to bind EGL OpenGL ES or Desktop OpenGL API");
            Destroy();
            return false;
        }
        if (!ChooseConfig(kEglOpenGlBit, alphaBits, m_eglConfig)) {
            keel::eprintln("[LinuxEglContext] eglChooseConfig() failed");
            Destroy();
            return false;
        }
    }

    m_nativeVisualId = 0;
    if (egl.GetConfigAttrib && m_eglConfig) {
        EGLint visId = 0;
        if (egl.GetConfigAttrib(m_eglDisplay, m_eglConfig, kEglNativeVisualId, &visId) == kEglTrue) {
            m_nativeVisualId = visId;
        }
    }
    return true;
}

bool LinuxEglContext::CreateSurfaceAndContext(
    keel::usize nativeWindow,
    keel::i32 swapInterval
) noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid || !m_eglDisplay || !m_eglConfig) {
        return false;
    }
    m_eglSurface = egl.CreateWindowSurface(m_eglDisplay, m_eglConfig, nativeWindow, nullptr);
    if (!m_eglSurface) {
        keel::eprintln("[LinuxEglContext] eglCreateWindowSurface() failed");
        return false;
    }

    if (m_boundEs) {
        const EGLint ctxAttribs[] = {kEglContextClientVersion, 3, kEglNone};
        m_eglContext = egl.CreateContext(m_eglDisplay, m_eglConfig, nullptr, ctxAttribs);
    } else {
        const EGLint ctxAttribs[] = {kEglNone};
        m_eglContext = egl.CreateContext(m_eglDisplay, m_eglConfig, nullptr, ctxAttribs);
    }
    if (!m_eglContext) {
        keel::eprintln("[LinuxEglContext] eglCreateContext() failed");
        return false;
    }

    if (egl.MakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext) == kEglFalse) {
        keel::eprintln("[LinuxEglContext] eglMakeCurrent() failed");
        return false;
    }
    SetSwapInterval(swapInterval);
    return true;
}

void LinuxEglContext::Destroy() noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid) {
        m_eglDisplay = nullptr;
        m_eglConfig = nullptr;
        m_eglSurface = nullptr;
        m_eglContext = nullptr;
        m_nativeVisualId = 0;
        return;
    }
    if (m_eglDisplay && egl.MakeCurrent) {
        (void)egl.MakeCurrent(m_eglDisplay, nullptr, nullptr, nullptr);
    }
    if (m_eglDisplay && m_eglContext && egl.DestroyContext) {
        (void)egl.DestroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = nullptr;
    }
    if (m_eglDisplay && m_eglSurface && egl.DestroySurface) {
        (void)egl.DestroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = nullptr;
    }
    if (m_eglDisplay && egl.Terminate) {
        (void)egl.Terminate(m_eglDisplay);
        m_eglDisplay = nullptr;
    }
    m_eglConfig = nullptr;
    m_nativeVisualId = 0;
}

bool LinuxEglContext::MakeCurrent() noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid || !m_eglDisplay || !m_eglSurface || !m_eglContext || !egl.MakeCurrent) {
        return true;
    }
    return egl.MakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext) == kEglTrue;
}

void LinuxEglContext::SwapBuffers() noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (egl.valid && m_eglDisplay && m_eglSurface && egl.SwapBuffers) {
        (void)egl.SwapBuffers(m_eglDisplay, m_eglSurface);
    }
}

void LinuxEglContext::SetSwapInterval(keel::i32 interval) noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (egl.valid && m_eglDisplay && egl.SwapInterval) {
        (void)egl.SwapInterval(m_eglDisplay, interval);
    }
}

bool LinuxEglContext::QuerySurfaceSize(keel::i32& outWidth, keel::i32& outHeight) const noexcept {
    const EglDispatchTable& egl = GetEglTable();
    if (!egl.valid || !m_eglDisplay || !m_eglSurface || !egl.QuerySurface) {
        return false;
    }
    EGLint w = 0;
    EGLint h = 0;
    if (egl.QuerySurface(m_eglDisplay, m_eglSurface, kEglWidth, &w) != kEglTrue ||
        egl.QuerySurface(m_eglDisplay, m_eglSurface, kEglHeight, &h) != kEglTrue) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    outWidth = w;
    outHeight = h;
    return true;
}

GlProcAddressFn LinuxEglContext::GetProcAddressFn() const noexcept {
    return GetEglTable().valid ? &LinuxEglProcLoaderThunk : nullptr;
}

#else

LinuxEglContext::~LinuxEglContext() noexcept = default;
bool LinuxEglContext::IsRuntimeAvailable() noexcept { return false; }
bool LinuxEglContext::ChooseConfig(EGLint, EGLint, EGLConfig&) noexcept { return false; }
bool LinuxEglContext::InitDisplayAndConfig(keel::u32, void*, GraphicsApi, keel::i32) noexcept { return false; }
bool LinuxEglContext::CreateSurfaceAndContext(keel::usize, keel::i32) noexcept { return false; }
void LinuxEglContext::Destroy() noexcept {}
bool LinuxEglContext::MakeCurrent() noexcept { return false; }
void LinuxEglContext::SwapBuffers() noexcept {}
void LinuxEglContext::SetSwapInterval(keel::i32) noexcept {}
bool LinuxEglContext::QuerySurfaceSize(keel::i32&, keel::i32&) const noexcept { return false; }
GlProcAddressFn LinuxEglContext::GetProcAddressFn() const noexcept { return nullptr; }

#endif

} // namespace guikit::native::linux_detail
