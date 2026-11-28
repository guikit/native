#pragma once

#include "guikit/backend/system_dialogs.hpp"
#include "guikit/core/types.hpp"
#include "keel/core.h"
#include "wayland_protocols.hpp"

namespace guikit::native::wl_detail {

/// Manages 4 invisible, synchronized `wl_subsurface` resize borders positioned immediately outside
/// a CSD Wayland `wl_surface` (`xdg_surface` window geometry remains `[0..W, 0..H]`).
/// Provides effortless edge and corner grabbing matching native GNOME/GTK/libdecor windows without
/// altering the opaque EGL main surface or requiring client-side shadow overdraw.
class WaylandResizeBorders {
public:
    static constexpr keel::i32 kOuterBorderPx = 10;
    static constexpr keel::f32 kCornerArmPx = 20.0f;

    WaylandResizeBorders() noexcept = default;
    ~WaylandResizeBorders() noexcept { Destroy(); }

    WaylandResizeBorders(const WaylandResizeBorders&) = delete;
    WaylandResizeBorders& operator=(const WaylandResizeBorders&) = delete;

    [[nodiscard]] bool Init(
        const WaylandClientApi* wl,
        const WaylandExtInterfaces* ext,
        wl_compositor* compositor,
        wl_subcompositor* subcompositor,
        wl_shm* shm,
        wp_viewporter* viewporter,
        wl_surface* parentSurface
    ) noexcept;

    void Destroy() noexcept;

    /// Synchronizes border subsurface visibility and geometry with the parent window.
    /// Must be followed by a commit on the parent surface (e.g. `eglSwapBuffers()` or `wl_surface.commit()`).
    void Sync(bool visible, keel::i32 logicalW, keel::i32 logicalH) noexcept;

    /// Returns true and writes the directional resize edge/corner if `surf` is one of the 4 border subsurfaces.
    [[nodiscard]] bool ResolveHitRegion(
        wl_surface* surf,
        Vec2 localPos,
        WindowHitRegion& outRegion
    ) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    enum Side : keel::usize {
        kTop = 0u,
        kBottom = 1u,
        kLeft = 2u,
        kRight = 3u,
        kSideCount = 4u
    };

    struct BorderSurface {
        wl_surface* surface{nullptr};
        wl_subsurface* subsurface{nullptr};
        wp_viewport* viewport{nullptr};
    };

    [[nodiscard]] bool CreateTransparentBuffer(wl_shm* shm) noexcept;

    const WaylandClientApi* m_wl{nullptr};
    wl_buffer* m_transparentBuf{nullptr};
    BorderSurface m_sides[kSideCount]{};
    bool m_initialized{false};
    bool m_visible{false};
    keel::i32 m_width{0};
    keel::i32 m_height{0};
};

} // namespace guikit::native::wl_detail
