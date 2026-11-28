#include "linux_egl_context.hpp"

#include "keel/format.h"
#include "keel/memory.h"

namespace guikit::native::linux_detail {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

static constexpr keel::i32 kEglFalse = 0;
static constexpr keel::i32 kEglTrue = 1;
static constexpr keel::i32 kEglNone = 0x3038;
static constexpr keel::i32 kEglRedSize = 0x3024;
static constexpr keel::i32 kEglGreenSize = 0x3023;
static constexpr keel::i32 kEglBlueSize = 0x3022;
static constexpr keel::i32 kEglAlphaSize = 0x3021;
static constexpr keel::i32 kEglStencilSize = 0x3026;
static constexpr keel::i32 kEglNativeVisualId = 0x302E;
static constexpr keel::i32 kEglSurfaceType = 0x3033;
static constexpr keel::i32 kEglWindowBit = 0x0004;
static constexpr keel::i32 kEglRenderableType = 0x3040;
static constexpr keel::i32 kEglOpenGlEs2Bit = 0x0004;
static constexpr keel::i32 kEglOpenGlEs3Bit = 0x0040;
static constexpr keel::i32 kEglOpenGlBit = 0x0008;
static constexpr keel::i32 kEglHeight = 0x3056;
static constexpr keel::i32 kEglWidth = 0x3057;
static constexpr keel::i32 kEglContextClientVersion = 0x3098;
static constexpr keel::u32 kEglOpenGlEsApi = 0x30A0u;
static constexpr keel::u32 kEglOpenGlApi = 0x30A2u;

using EglGetProcAddressFn = void*(*(*)(const char*))(const char*);
using RawEglProcAddr = void*(*)(const char*);
using PfnEglGetProcAddr = RawEglProcAddr(*)(const char*);

PfnEglGetProcAddr g_activeLinuxEglGetProcAddress = nullptr;

void* LinuxEglProcLoaderThunk(const char* procName) {
    if (!g_activeLinuxEglGetProcAddress || !procName) {
        return nullptr;
    }
    return reinterpret_cast<void*>(g_activeLinuxEglGetProcAddress(procName));
}

} // namespace

LinuxEglContext::~LinuxEglContext() noexcept {
    Destroy();
}

bool LinuxEglContext::IsRuntimeAvailable() noexcept {
    LinuxEglContext probe;
    return probe.LoadLibrary();
}

bool LinuxEglContext::LoadLibrary() noexcept {
    if (m_lib.is_open()) {
        return true;
    }
    auto opened = keel::DynLib::open("libEGL.so.1");
    if (!opened) {
        opened = keel::DynLib::open("libEGL.so");
    }
    if (!opened) {
        return false;
    }
    m_lib = keel::move(*opened);

    m_getDisplay = m_lib.symbol<PfnGetDisplay>("eglGetDisplay");
    m_getPlatformDisplay = m_lib.symbol<PfnGetPlatformDisplay>("eglGetPlatformDisplay");
    m_initialize = m_lib.symbol<PfnInitialize>("eglInitialize");
    m_terminate = m_lib.symbol<PfnTerminate>("eglTerminate");
    m_bindApi = m_lib.symbol<PfnBindApi>("eglBindAPI");
    m_chooseConfig = m_lib.symbol<PfnChooseConfig>("eglChooseConfig");
    m_getConfigAttrib = m_lib.symbol<PfnGetConfigAttrib>("eglGetConfigAttrib");
    m_createWindowSurface = m_lib.symbol<PfnCreateWindowSurface>("eglCreateWindowSurface");
    m_destroySurface = m_lib.symbol<PfnDestroySurface>("eglDestroySurface");
    m_createContext = m_lib.symbol<PfnCreateContext>("eglCreateContext");
    m_destroyContext = m_lib.symbol<PfnDestroyContext>("eglDestroyContext");
    m_makeCurrent = m_lib.symbol<PfnMakeCurrent>("eglMakeCurrent");
    m_swapBuffers = m_lib.symbol<PfnSwapBuffers>("eglSwapBuffers");
    m_swapInterval = m_lib.symbol<PfnSwapInterval>("eglSwapInterval");
    m_querySurface = m_lib.symbol<PfnQuerySurface>("eglQuerySurface");
    m_getProcAddress = m_lib.symbol<PfnGetProcAddress>("eglGetProcAddress");

    if (m_getProcAddress) {
        void* rawExt = reinterpret_cast<void*>(m_getProcAddress("eglGetPlatformDisplayEXT"));
        if (rawExt) {
            keel::mem_copy(&m_getPlatformDisplayExt, &rawExt, sizeof(m_getPlatformDisplayExt));
        }
    }

    return m_getDisplay && m_initialize && m_terminate && m_bindApi && m_chooseConfig &&
           m_createWindowSurface && m_destroySurface && m_createContext &&
           m_destroyContext && m_makeCurrent && m_swapBuffers && m_getProcAddress;
}

bool LinuxEglContext::ChooseConfig(EGLint renderableBit, EGLint alphaBits, EGLConfig& outCfg) noexcept {
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
        if (m_chooseConfig(m_eglDisplay, attribs, &cfg, 1, &numConfigs) == kEglTrue && numConfigs > 0 && cfg) {
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
    if (!LoadLibrary()) {
        keel::eprintln("[LinuxEglContext] Failed to load libEGL.so.1");
        return false;
    }
    if (m_getPlatformDisplay) {
        m_eglDisplay = m_getPlatformDisplay(platformEnum, nativeDisplay, nullptr);
    } else if (m_getPlatformDisplayExt) {
        m_eglDisplay = m_getPlatformDisplayExt(platformEnum, nativeDisplay, nullptr);
    }
    if (!m_eglDisplay) {
        m_eglDisplay = m_getDisplay(nativeDisplay);
    }
    if (!m_eglDisplay) {
        keel::eprintln("[LinuxEglContext] eglGetDisplay() failed");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (m_initialize(m_eglDisplay, &major, &minor) == kEglFalse) {
        keel::eprintln("[LinuxEglContext] eglInitialize() failed");
        m_eglDisplay = nullptr;
        return false;
    }

    const bool tryEs3First = (requestedApi != GraphicsApi::OpenGL3);
    m_boundEs = false;
    m_eglConfig = nullptr;

    if (tryEs3First && m_bindApi(kEglOpenGlEsApi) == kEglTrue) {
        if (ChooseConfig(kEglOpenGlEs3Bit, alphaBits, m_eglConfig) ||
            ChooseConfig(kEglOpenGlEs2Bit, alphaBits, m_eglConfig)) {
            m_boundEs = true;
        }
    }

    if (!m_boundEs) {
        if (m_bindApi(kEglOpenGlApi) == kEglFalse) {
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
    if (m_getConfigAttrib && m_eglConfig) {
        EGLint visId = 0;
        if (m_getConfigAttrib(m_eglDisplay, m_eglConfig, kEglNativeVisualId, &visId) == kEglTrue) {
            m_nativeVisualId = visId;
        }
    }
    return true;
}

bool LinuxEglContext::CreateSurfaceAndContext(
    keel::usize nativeWindow,
    keel::i32 swapInterval
) noexcept {
    if (!m_eglDisplay || !m_eglConfig) {
        return false;
    }
    m_eglSurface = m_createWindowSurface(m_eglDisplay, m_eglConfig, nativeWindow, nullptr);
    if (!m_eglSurface) {
        keel::eprintln("[LinuxEglContext] eglCreateWindowSurface() failed");
        return false;
    }

    if (m_boundEs) {
        const EGLint ctxAttribs[] = {kEglContextClientVersion, 3, kEglNone};
        m_eglContext = m_createContext(m_eglDisplay, m_eglConfig, nullptr, ctxAttribs);
    } else {
        const EGLint ctxAttribs[] = {kEglNone};
        m_eglContext = m_createContext(m_eglDisplay, m_eglConfig, nullptr, ctxAttribs);
    }
    if (!m_eglContext) {
        keel::eprintln("[LinuxEglContext] eglCreateContext() failed");
        return false;
    }

    g_activeLinuxEglGetProcAddress = m_getProcAddress;
    if (m_makeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext) == kEglFalse) {
        keel::eprintln("[LinuxEglContext] eglMakeCurrent() failed");
        return false;
    }
    SetSwapInterval(swapInterval);
    return true;
}

void LinuxEglContext::Destroy() noexcept {
    if (m_eglDisplay && m_makeCurrent) {
        (void)m_makeCurrent(m_eglDisplay, nullptr, nullptr, nullptr);
    }
    if (m_eglDisplay && m_eglContext && m_destroyContext) {
        (void)m_destroyContext(m_eglDisplay, m_eglContext);
        m_eglContext = nullptr;
    }
    if (m_eglDisplay && m_eglSurface && m_destroySurface) {
        (void)m_destroySurface(m_eglDisplay, m_eglSurface);
        m_eglSurface = nullptr;
    }
    if (m_eglDisplay && m_terminate) {
        (void)m_terminate(m_eglDisplay);
        m_eglDisplay = nullptr;
    }
    m_eglConfig = nullptr;
    m_nativeVisualId = 0;
}

bool LinuxEglContext::MakeCurrent() noexcept {
    if (!m_eglDisplay || !m_eglSurface || !m_eglContext || !m_makeCurrent) {
        return true;
    }
    g_activeLinuxEglGetProcAddress = m_getProcAddress;
    return m_makeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext) == kEglTrue;
}

void LinuxEglContext::SwapBuffers() noexcept {
    if (m_eglDisplay && m_eglSurface && m_swapBuffers) {
        (void)m_swapBuffers(m_eglDisplay, m_eglSurface);
    }
}

void LinuxEglContext::SetSwapInterval(keel::i32 interval) noexcept {
    if (m_eglDisplay && m_swapInterval) {
        (void)m_swapInterval(m_eglDisplay, interval);
    }
}

bool LinuxEglContext::QuerySurfaceSize(keel::i32& outWidth, keel::i32& outHeight) const noexcept {
    if (!m_eglDisplay || !m_eglSurface || !m_querySurface) {
        return false;
    }
    EGLint w = 0;
    EGLint h = 0;
    if (m_querySurface(m_eglDisplay, m_eglSurface, kEglWidth, &w) != kEglTrue ||
        m_querySurface(m_eglDisplay, m_eglSurface, kEglHeight, &h) != kEglTrue) {
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
    return m_getProcAddress ? &LinuxEglProcLoaderThunk : nullptr;
}

#else

LinuxEglContext::~LinuxEglContext() noexcept = default;
bool LinuxEglContext::IsRuntimeAvailable() noexcept { return false; }
bool LinuxEglContext::LoadLibrary() noexcept { return false; }
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
