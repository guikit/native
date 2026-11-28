#include "wayland_resize_borders.hpp"

#include "keel/math.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace guikit::native::wl_detail {

#if defined(__linux__) && !defined(__ANDROID__)

bool WaylandResizeBorders::CreateTransparentBuffer(wl_shm* shm) noexcept {
    if (!m_wl || !shm || !m_wl->ifaceShmPool || !m_wl->ifaceBuffer) {
        return false;
    }

    // Create a 1x1 fully transparent ARGB8888 wl_shm buffer (4 zero bytes)
    const keel::i32 fd = ::memfd_create("guikit-wl-border", MFD_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    if (::ftruncate(fd, 4) != 0) {
        ::close(fd);
        return false;
    }

    wl_argument poolArgs[3]{};
    poolArgs[0].o = nullptr;
    poolArgs[1].h = fd;
    poolArgs[2].i = 4;
    wl_shm_pool* pool = m_wl->MarshalNew<wl_shm_pool>(
        shm,
        kWlShmCreatePool,
        m_wl->ifaceShmPool,
        poolArgs
    );
    ::close(fd);
    if (!pool) {
        return false;
    }

    wl_argument bufArgs[6]{};
    bufArgs[0].o = nullptr;
    bufArgs[1].i = 0; // offset
    bufArgs[2].i = 1; // width
    bufArgs[3].i = 1; // height
    bufArgs[4].i = 4; // stride
    bufArgs[5].u = kWlShmFormatArgb8888;
    m_transparentBuf = m_wl->MarshalNew<wl_buffer>(
        pool,
        kWlShmPoolCreateBuffer,
        m_wl->ifaceBuffer,
        bufArgs
    );
    m_wl->MarshalDestroy(pool, kWlShmPoolDestroy);
    return m_transparentBuf != nullptr;
}

bool WaylandResizeBorders::Init(
    const WaylandClientApi* wl,
    const WaylandExtInterfaces* ext,
    wl_compositor* compositor,
    wl_subcompositor* subcompositor,
    wl_shm* shm,
    wp_viewporter* viewporter,
    wl_surface* parentSurface
) noexcept {
    Destroy();
    if (!wl || !ext || !compositor || !subcompositor || !shm || !viewporter || !parentSurface ||
        !wl->ifaceSurface || !wl->ifaceSubsurface || !wl->ifaceShmPool || !wl->ifaceBuffer) {
        return false;
    }

    m_wl = wl;
    if (!CreateTransparentBuffer(shm)) {
        Destroy();
        return false;
    }

    for (keel::usize i = 0u; i < kSideCount; ++i) {
        wl_argument surfArg[1]{};
        m_sides[i].surface = m_wl->MarshalNew<wl_surface>(
            compositor,
            kWlCompositorCreateSurface,
            m_wl->ifaceSurface,
            surfArg
        );
        if (!m_sides[i].surface) {
            Destroy();
            return false;
        }

        wl_argument subArgs[3]{};
        subArgs[0].o = nullptr;
        subArgs[1].o = reinterpret_cast<wl_object*>(m_sides[i].surface);
        subArgs[2].o = reinterpret_cast<wl_object*>(parentSurface);
        m_sides[i].subsurface = m_wl->MarshalNew<wl_subsurface>(
            subcompositor,
            kWlSubcompositorGetSubsurface,
            m_wl->ifaceSubsurface,
            subArgs
        );
        if (!m_sides[i].subsurface) {
            Destroy();
            return false;
        }

        // Stack behind main surface and keep synchronized with parent commits
        wl_argument belowArg[1]{};
        belowArg[0].o = reinterpret_cast<wl_object*>(parentSurface);
        m_wl->MarshalVoid(m_sides[i].subsurface, kWlSubsurfacePlaceBelow, belowArg);
        m_wl->MarshalVoid(m_sides[i].subsurface, kWlSubsurfaceSetSync);

        wl_argument vpArgs[2]{};
        vpArgs[0].o = nullptr;
        vpArgs[1].o = reinterpret_cast<wl_object*>(m_sides[i].surface);
        m_sides[i].viewport = m_wl->MarshalNew<wp_viewport>(
            viewporter,
            kWpViewporterGetViewport,
            &ext->viewport,
            vpArgs
        );
        if (!m_sides[i].viewport) {
            Destroy();
            return false;
        }
    }

    m_initialized = true;
    m_visible = false;
    m_width = 0;
    m_height = 0;
    return true;
}

void WaylandResizeBorders::Destroy() noexcept {
    if (m_wl) {
        for (keel::usize i = 0u; i < kSideCount; ++i) {
            if (m_sides[i].viewport) {
                m_wl->MarshalDestroy(m_sides[i].viewport, kWpViewportDestroy);
                m_sides[i].viewport = nullptr;
            }
            if (m_sides[i].subsurface) {
                m_wl->MarshalDestroy(m_sides[i].subsurface, kWlSubsurfaceDestroy);
                m_sides[i].subsurface = nullptr;
            }
            if (m_sides[i].surface) {
                m_wl->MarshalDestroy(m_sides[i].surface, kWlSurfaceDestroy);
                m_sides[i].surface = nullptr;
            }
        }
        if (m_transparentBuf) {
            m_wl->MarshalDestroy(m_transparentBuf, kWlBufferDestroy);
            m_transparentBuf = nullptr;
        }
    }
    for (keel::usize i = 0u; i < kSideCount; ++i) {
        m_sides[i] = {};
    }
    m_transparentBuf = nullptr;
    m_wl = nullptr;
    m_initialized = false;
    m_visible = false;
    m_width = 0;
    m_height = 0;
}

void WaylandResizeBorders::Sync(bool visible, keel::i32 logicalW, keel::i32 logicalH) noexcept {
    if (!m_initialized || !m_wl || !m_transparentBuf) {
        return;
    }

    const bool wantVisible = visible && logicalW > 0 && logicalH > 0;
    if (!wantVisible) {
        if (!m_visible) {
            return;
        }
        for (keel::usize i = 0u; i < kSideCount; ++i) {
            wl_argument dstArgs[2]{};
            dstArgs[0].i = -1;
            dstArgs[1].i = -1;
            m_wl->MarshalVoid(m_sides[i].viewport, kWpViewportSetDestination, dstArgs);

            wl_argument attachArgs[3]{};
            attachArgs[0].o = nullptr;
            attachArgs[1].i = 0;
            attachArgs[2].i = 0;
            m_wl->MarshalVoid(m_sides[i].surface, kWlSurfaceAttach, attachArgs);
            m_wl->MarshalVoid(m_sides[i].surface, kWlSurfaceCommit);
        }
        m_visible = false;
        return;
    }

    if (m_visible && m_width == logicalW && m_height == logicalH) {
        return;
    }

    constexpr keel::i32 B = kOuterBorderPx;
    const keel::i32 geom[kSideCount][4] = {
        {-B,       -B,       logicalW + 2 * B, B},        // kTop
        {-B,       logicalH, logicalW + 2 * B, B},        // kBottom
        {-B,       0,        B,                logicalH}, // kLeft
        {logicalW, 0,        B,                logicalH}  // kRight
    };

    const bool attachBuffer = !m_visible;
    for (keel::usize i = 0u; i < kSideCount; ++i) {
        wl_argument posArgs[2]{};
        posArgs[0].i = geom[i][0];
        posArgs[1].i = geom[i][1];
        m_wl->MarshalVoid(m_sides[i].subsurface, kWlSubsurfaceSetPosition, posArgs);

        if (attachBuffer) {
            wl_argument attachArgs[3]{};
            attachArgs[0].o = reinterpret_cast<wl_object*>(m_transparentBuf);
            attachArgs[1].i = 0;
            attachArgs[2].i = 0;
            m_wl->MarshalVoid(m_sides[i].surface, kWlSurfaceAttach, attachArgs);
        }

        wl_argument dstArgs[2]{};
        dstArgs[0].i = geom[i][2];
        dstArgs[1].i = geom[i][3];
        m_wl->MarshalVoid(m_sides[i].viewport, kWpViewportSetDestination, dstArgs);

        m_wl->MarshalVoid(m_sides[i].surface, kWlSurfaceCommit);
    }

    m_visible = true;
    m_width = logicalW;
    m_height = logicalH;
}

bool WaylandResizeBorders::ResolveHitRegion(
    wl_surface* surf,
    Vec2 localPos,
    WindowHitRegion& outRegion
) const noexcept {
    if (!m_initialized || !m_visible || !surf || m_width <= 0 || m_height <= 0) {
        return false;
    }

    const keel::f32 w = keel::as<keel::f32>(m_width);
    const keel::f32 h = keel::as<keel::f32>(m_height);
    const keel::f32 b = keel::as<keel::f32>(kOuterBorderPx);
    const keel::f32 corner = keel::min(kCornerArmPx, keel::min(w, h) * 0.25f);

    if (surf == m_sides[kTop].surface) {
        if (localPos.x < b + corner) {
            outRegion = WindowHitRegion::ResizeTopLeft;
        } else if (localPos.x >= (w + b) - corner) {
            outRegion = WindowHitRegion::ResizeTopRight;
        } else {
            outRegion = WindowHitRegion::ResizeTop;
        }
        return true;
    }
    if (surf == m_sides[kBottom].surface) {
        if (localPos.x < b + corner) {
            outRegion = WindowHitRegion::ResizeBottomLeft;
        } else if (localPos.x >= (w + b) - corner) {
            outRegion = WindowHitRegion::ResizeBottomRight;
        } else {
            outRegion = WindowHitRegion::ResizeBottom;
        }
        return true;
    }
    if (surf == m_sides[kLeft].surface) {
        if (localPos.y < corner) {
            outRegion = WindowHitRegion::ResizeTopLeft;
        } else if (localPos.y >= h - corner) {
            outRegion = WindowHitRegion::ResizeBottomLeft;
        } else {
            outRegion = WindowHitRegion::ResizeLeft;
        }
        return true;
    }
    if (surf == m_sides[kRight].surface) {
        if (localPos.y < corner) {
            outRegion = WindowHitRegion::ResizeTopRight;
        } else if (localPos.y >= h - corner) {
            outRegion = WindowHitRegion::ResizeBottomRight;
        } else {
            outRegion = WindowHitRegion::ResizeRight;
        }
        return true;
    }

    return false;
}

#else

bool WaylandResizeBorders::CreateTransparentBuffer(wl_shm*) noexcept { return false; }
bool WaylandResizeBorders::Init(const WaylandClientApi*, const WaylandExtInterfaces*, wl_compositor*, wl_subcompositor*, wl_shm*, wp_viewporter*, wl_surface*) noexcept { return false; }
void WaylandResizeBorders::Destroy() noexcept {}
void WaylandResizeBorders::Sync(bool, keel::i32, keel::i32) noexcept {}
bool WaylandResizeBorders::ResolveHitRegion(wl_surface*, Vec2, WindowHitRegion&) const noexcept { return false; }

#endif

} // namespace guikit::native::wl_detail
