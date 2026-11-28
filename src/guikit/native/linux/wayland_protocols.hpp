#pragma once

// Self-contained Wayland C ABI, protocol interface tables, and dynamic loaders
// for libwayland-client, libwayland-egl, libwayland-cursor, and libxkbcommon.
// Zero compile-time or link-time dependency on libwayland-dev / wayland-protocols.

#include "keel/core.h"
#include "keel/dynlib.h"

namespace guikit::native::wl_detail {

// ---------------------------------------------------------------------------
// Core Wayland C ABI (<wayland-util.h>, <wayland-client-core.h>)
// ---------------------------------------------------------------------------
using WlFixed = keel::i32;

[[nodiscard]] inline keel::f32 WlFixedToFloat(WlFixed f) noexcept {
    return keel::as<keel::f32>(f) / 256.0f;
}

struct wl_object;
struct wl_proxy;
struct wl_display;
struct wl_registry;
struct wl_callback;
struct wl_compositor;
struct wl_surface;
struct wl_region;
struct wl_shm;
struct wl_shm_pool;
struct wl_buffer;
struct wl_subcompositor;
struct wl_subsurface;
struct wl_seat;
struct wl_pointer;
struct wl_keyboard;
struct wl_output;
struct wl_data_device_manager;
struct wl_data_device;
struct wl_data_source;
struct wl_data_offer;
struct wl_egl_window;
struct wl_cursor_theme;

struct xdg_wm_base;
struct xdg_positioner;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_popup;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;
struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct wp_cursor_shape_manager_v1;
struct wp_cursor_shape_device_v1;

struct wl_interface;

struct wl_message {
    const char* name;
    const char* signature;
    const wl_interface** types;
};

struct wl_interface {
    const char* name;
    keel::i32 version;
    keel::i32 method_count;
    const wl_message* methods;
    keel::i32 event_count;
    const wl_message* events;
};

struct wl_array {
    keel::usize size;
    keel::usize alloc;
    void* data;
};

union wl_argument {
    keel::i32 i;
    keel::u32 u;
    WlFixed f;
    const char* s;
    wl_object* o;
    keel::u32 n;
    wl_array* a;
    keel::i32 h;
};

static constexpr keel::u32 kWlMarshalFlagDestroy = 1u << 0u;

// Core opcodes (<wayland-client-protocol.h>)
static constexpr keel::u32 kWlDisplayGetRegistry          = 1u;
static constexpr keel::u32 kWlRegistryBind                = 0u;
static constexpr keel::u32 kWlCompositorCreateSurface     = 0u;
static constexpr keel::u32 kWlCompositorCreateRegion      = 1u;
static constexpr keel::u32 kWlSurfaceDestroy              = 0u;
static constexpr keel::u32 kWlSurfaceAttach               = 1u;
static constexpr keel::u32 kWlSurfaceDamage               = 2u;
static constexpr keel::u32 kWlSurfaceFrame                = 3u;
static constexpr keel::u32 kWlSurfaceSetOpaqueRegion      = 4u;
static constexpr keel::u32 kWlSurfaceCommit               = 6u;
static constexpr keel::u32 kWlSurfaceSetBufferScale       = 8u;
static constexpr keel::u32 kWlRegionDestroy               = 0u;
static constexpr keel::u32 kWlRegionAdd                   = 1u;
static constexpr keel::u32 kWlShmCreatePool               = 0u;
static constexpr keel::u32 kWlShmFormatArgb8888           = 0u;
static constexpr keel::u32 kWlShmPoolCreateBuffer         = 0u;
static constexpr keel::u32 kWlShmPoolDestroy              = 1u;
static constexpr keel::u32 kWlBufferDestroy               = 0u;
static constexpr keel::u32 kWlSubcompositorDestroy        = 0u;
static constexpr keel::u32 kWlSubcompositorGetSubsurface  = 1u;
static constexpr keel::u32 kWlSubsurfaceDestroy           = 0u;
static constexpr keel::u32 kWlSubsurfaceSetPosition       = 1u;
static constexpr keel::u32 kWlSubsurfacePlaceAbove        = 2u;
static constexpr keel::u32 kWlSubsurfacePlaceBelow        = 3u;
static constexpr keel::u32 kWlSubsurfaceSetSync           = 4u;
static constexpr keel::u32 kWlSubsurfaceSetDesync         = 5u;
static constexpr keel::u32 kWlSeatGetPointer              = 0u;
static constexpr keel::u32 kWlSeatGetKeyboard             = 1u;
static constexpr keel::u32 kWlSeatRelease                 = 3u;
static constexpr keel::u32 kWlSeatCapPointer              = 1u;
static constexpr keel::u32 kWlSeatCapKeyboard             = 2u;
static constexpr keel::u32 kWlPointerSetCursor            = 0u;
static constexpr keel::u32 kWlPointerRelease              = 1u;
static constexpr keel::u32 kWlPointerButtonReleased       = 0u;
static constexpr keel::u32 kWlPointerButtonPressed        = 1u;
static constexpr keel::u32 kWlPointerAxisVertical         = 0u;
static constexpr keel::u32 kWlPointerAxisHorizontal       = 1u;
static constexpr keel::u32 kWlKeyboardRelease             = 0u;
static constexpr keel::u32 kWlKeyboardKeymapXkbV1         = 1u;
static constexpr keel::u32 kWlKeyboardKeyReleased         = 0u;
static constexpr keel::u32 kWlKeyboardKeyPressed          = 1u;
static constexpr keel::u32 kWlOutputModeCurrent           = 1u;
static constexpr keel::u32 kWlOutputRelease               = 0u;
static constexpr keel::u32 kWlDataDevMgrCreateSource      = 0u;
static constexpr keel::u32 kWlDataDevMgrGetDataDevice     = 1u;
static constexpr keel::u32 kWlDataDeviceSetSelection      = 1u;
static constexpr keel::u32 kWlDataDeviceRelease           = 2u;
static constexpr keel::u32 kWlDataSourceOffer             = 0u;
static constexpr keel::u32 kWlDataSourceDestroy           = 1u;
static constexpr keel::u32 kWlDataOfferReceive            = 1u;
static constexpr keel::u32 kWlDataOfferDestroy            = 2u;

// xdg-shell opcodes & enums
static constexpr keel::u32 kXdgWmBaseDestroy              = 0u;
static constexpr keel::u32 kXdgWmBaseGetXdgSurface        = 2u;
static constexpr keel::u32 kXdgWmBasePong                 = 3u;
static constexpr keel::u32 kXdgSurfaceDestroy             = 0u;
static constexpr keel::u32 kXdgSurfaceGetToplevel         = 1u;
static constexpr keel::u32 kXdgSurfaceSetWindowGeometry   = 3u;
static constexpr keel::u32 kXdgSurfaceAckConfigure        = 4u;
static constexpr keel::u32 kXdgToplevelDestroy            = 0u;
static constexpr keel::u32 kXdgToplevelSetTitle           = 2u;
static constexpr keel::u32 kXdgToplevelSetAppId           = 3u;
static constexpr keel::u32 kXdgToplevelMove               = 5u;
static constexpr keel::u32 kXdgToplevelResize             = 6u;
static constexpr keel::u32 kXdgToplevelSetMaxSize         = 7u;
static constexpr keel::u32 kXdgToplevelSetMinSize         = 8u;
static constexpr keel::u32 kXdgToplevelSetMaximized       = 9u;
static constexpr keel::u32 kXdgToplevelUnsetMaximized     = 10u;
static constexpr keel::u32 kXdgToplevelSetFullscreen      = 11u;
static constexpr keel::u32 kXdgToplevelUnsetFullscreen    = 12u;
static constexpr keel::u32 kXdgToplevelSetMinimized       = 13u;

static constexpr keel::u32 kXdgToplevelStateMaximized     = 1u;
static constexpr keel::u32 kXdgToplevelStateFullscreen    = 2u;
static constexpr keel::u32 kXdgToplevelStateResizing      = 3u;
static constexpr keel::u32 kXdgToplevelStateActivated     = 4u;
static constexpr keel::u32 kXdgToplevelStateSuspended     = 9u;

static constexpr keel::u32 kXdgResizeEdgeTop              = 1u;
static constexpr keel::u32 kXdgResizeEdgeBottom           = 2u;
static constexpr keel::u32 kXdgResizeEdgeLeft             = 4u;
static constexpr keel::u32 kXdgResizeEdgeTopLeft          = 5u;
static constexpr keel::u32 kXdgResizeEdgeBottomLeft       = 6u;
static constexpr keel::u32 kXdgResizeEdgeRight            = 8u;
static constexpr keel::u32 kXdgResizeEdgeTopRight         = 9u;
static constexpr keel::u32 kXdgResizeEdgeBottomRight      = 10u;

// zxdg-decoration-unstable-v1 opcodes & modes
static constexpr keel::u32 kZxdgDecoMgrDestroy            = 0u;
static constexpr keel::u32 kZxdgDecoMgrGetToplevelDeco    = 1u;
static constexpr keel::u32 kZxdgToplevelDecoDestroy       = 0u;
static constexpr keel::u32 kZxdgToplevelDecoSetMode       = 1u;
static constexpr keel::u32 kZxdgDecoModeClientSide        = 1u;
static constexpr keel::u32 kZxdgDecoModeServerSide        = 2u;

// wp-viewporter opcodes
static constexpr keel::u32 kWpViewporterDestroy           = 0u;
static constexpr keel::u32 kWpViewporterGetViewport       = 1u;
static constexpr keel::u32 kWpViewportDestroy             = 0u;
static constexpr keel::u32 kWpViewportSetDestination      = 2u;

// wp-fractional-scale-v1 opcodes
static constexpr keel::u32 kWpFracScaleMgrDestroy         = 0u;
static constexpr keel::u32 kWpFracScaleMgrGetScale        = 1u;
static constexpr keel::u32 kWpFracScaleDestroy            = 0u;

// wp-cursor-shape-v1 opcodes & shapes
static constexpr keel::u32 kWpCursorShapeMgrDestroy       = 0u;
static constexpr keel::u32 kWpCursorShapeMgrGetPointer    = 1u;
static constexpr keel::u32 kWpCursorShapeDevDestroy       = 0u;
static constexpr keel::u32 kWpCursorShapeDevSetShape      = 1u;

static constexpr keel::u32 kWpCursorShapeDefault          = 1u;
static constexpr keel::u32 kWpCursorShapePointer          = 4u;
static constexpr keel::u32 kWpCursorShapeText             = 9u;
static constexpr keel::u32 kWpCursorShapeMove             = 13u;
static constexpr keel::u32 kWpCursorShapeNotAllowed       = 15u;
static constexpr keel::u32 kWpCursorShapeEResize          = 18u;
static constexpr keel::u32 kWpCursorShapeNResize          = 19u;
static constexpr keel::u32 kWpCursorShapeNeResize         = 20u;
static constexpr keel::u32 kWpCursorShapeNwResize         = 21u;
static constexpr keel::u32 kWpCursorShapeSResize          = 22u;
static constexpr keel::u32 kWpCursorShapeSeResize         = 23u;
static constexpr keel::u32 kWpCursorShapeSwResize         = 24u;
static constexpr keel::u32 kWpCursorShapeWResize          = 25u;
static constexpr keel::u32 kWpCursorShapeEwResize         = 26u;
static constexpr keel::u32 kWpCursorShapeNsResize         = 27u;
static constexpr keel::u32 kWpCursorShapeNeswResize       = 28u;
static constexpr keel::u32 kWpCursorShapeNwseResize       = 29u;

// Listener structs matching <wayland-client-protocol.h> & scanner output
struct wl_registry_listener {
    void (*global)(void*, wl_registry*, keel::u32, const char*, keel::u32);
    void (*global_remove)(void*, wl_registry*, keel::u32);
};

struct wl_callback_listener {
    void (*done)(void*, wl_callback*, keel::u32);
};

struct wl_surface_listener {
    void (*enter)(void*, wl_surface*, wl_output*);
    void (*leave)(void*, wl_surface*, wl_output*);
    void (*preferred_buffer_scale)(void*, wl_surface*, keel::i32);
    void (*preferred_buffer_transform)(void*, wl_surface*, keel::u32);
};

struct wl_seat_listener {
    void (*capabilities)(void*, wl_seat*, keel::u32);
    void (*name)(void*, wl_seat*, const char*);
};

struct wl_pointer_listener {
    void (*enter)(void*, wl_pointer*, keel::u32, wl_surface*, WlFixed, WlFixed);
    void (*leave)(void*, wl_pointer*, keel::u32, wl_surface*);
    void (*motion)(void*, wl_pointer*, keel::u32, WlFixed, WlFixed);
    void (*button)(void*, wl_pointer*, keel::u32, keel::u32, keel::u32, keel::u32);
    void (*axis)(void*, wl_pointer*, keel::u32, keel::u32, WlFixed);
    void (*frame)(void*, wl_pointer*);
    void (*axis_source)(void*, wl_pointer*, keel::u32);
    void (*axis_stop)(void*, wl_pointer*, keel::u32, keel::u32);
    void (*axis_discrete)(void*, wl_pointer*, keel::u32, keel::i32);
    void (*axis_value120)(void*, wl_pointer*, keel::u32, keel::i32);
    void (*axis_relative_direction)(void*, wl_pointer*, keel::u32, keel::u32);
};

struct wl_keyboard_listener {
    void (*keymap)(void*, wl_keyboard*, keel::u32, keel::i32, keel::u32);
    void (*enter)(void*, wl_keyboard*, keel::u32, wl_surface*, wl_array*);
    void (*leave)(void*, wl_keyboard*, keel::u32, wl_surface*);
    void (*key)(void*, wl_keyboard*, keel::u32, keel::u32, keel::u32, keel::u32);
    void (*modifiers)(void*, wl_keyboard*, keel::u32, keel::u32, keel::u32, keel::u32, keel::u32);
    void (*repeat_info)(void*, wl_keyboard*, keel::i32, keel::i32);
};

struct wl_output_listener {
    void (*geometry)(void*, wl_output*, keel::i32, keel::i32, keel::i32, keel::i32, keel::i32, const char*, const char*, keel::i32);
    void (*mode)(void*, wl_output*, keel::u32, keel::i32, keel::i32, keel::i32);
    void (*done)(void*, wl_output*);
    void (*scale)(void*, wl_output*, keel::i32);
    void (*name)(void*, wl_output*, const char*);
    void (*description)(void*, wl_output*, const char*);
};

struct wl_data_device_listener {
    void (*data_offer)(void*, wl_data_device*, wl_data_offer*);
    void (*enter)(void*, wl_data_device*, keel::u32, wl_surface*, WlFixed, WlFixed, wl_data_offer*);
    void (*leave)(void*, wl_data_device*);
    void (*motion)(void*, wl_data_device*, keel::u32, WlFixed, WlFixed);
    void (*drop)(void*, wl_data_device*);
    void (*selection)(void*, wl_data_device*, wl_data_offer*);
};

struct wl_data_source_listener {
    void (*target)(void*, wl_data_source*, const char*);
    void (*send)(void*, wl_data_source*, const char*, keel::i32);
    void (*cancelled)(void*, wl_data_source*);
    void (*dnd_drop_performed)(void*, wl_data_source*);
    void (*dnd_finished)(void*, wl_data_source*);
    void (*action)(void*, wl_data_source*, keel::u32);
};

struct wl_data_offer_listener {
    void (*offer)(void*, wl_data_offer*, const char*);
    void (*source_actions)(void*, wl_data_offer*, keel::u32);
    void (*action)(void*, wl_data_offer*, keel::u32);
};

struct xdg_wm_base_listener {
    void (*ping)(void*, xdg_wm_base*, keel::u32);
};

struct xdg_surface_listener {
    void (*configure)(void*, xdg_surface*, keel::u32);
};

struct xdg_toplevel_listener {
    void (*configure)(void*, xdg_toplevel*, keel::i32, keel::i32, wl_array*);
    void (*close)(void*, xdg_toplevel*);
    void (*configure_bounds)(void*, xdg_toplevel*, keel::i32, keel::i32);
    void (*wm_capabilities)(void*, xdg_toplevel*, wl_array*);
};

struct zxdg_toplevel_decoration_v1_listener {
    void (*configure)(void*, zxdg_toplevel_decoration_v1*, keel::u32);
};

struct wp_fractional_scale_v1_listener {
    void (*preferred_scale)(void*, wp_fractional_scale_v1*, keel::u32);
};

// Dynamic Wayland client library API
struct WaylandClientApi {
    keel::DynLib lib{};

    using PfnDisplayConnect = wl_display*(*)(const char*);
    using PfnDisplayDisconnect = void(*)(wl_display*);
    using PfnDisplayGetFd = keel::i32(*)(wl_display*);
    using PfnDisplayFlush = keel::i32(*)(wl_display*);
    using PfnDisplayRoundtrip = keel::i32(*)(wl_display*);
    using PfnDisplayDispatchPending = keel::i32(*)(wl_display*);
    using PfnDisplayPrepareRead = keel::i32(*)(wl_display*);
    using PfnDisplayCancelRead = void(*)(wl_display*);
    using PfnDisplayReadEvents = keel::i32(*)(wl_display*);
    using PfnDisplayGetError = keel::i32(*)(wl_display*);
    using PfnProxyMarshalArrayFlags = wl_proxy*(*)(wl_proxy*, keel::u32, const wl_interface*, keel::u32, keel::u32, wl_argument*);
    using PfnProxyMarshalArray = void(*)(wl_proxy*, keel::u32, wl_argument*);
    using PfnProxyMarshalArrayCtorVer = wl_proxy*(*)(wl_proxy*, keel::u32, wl_argument*, const wl_interface*, keel::u32);
    using PfnProxyDestroy = void(*)(wl_proxy*);
    using PfnProxyAddListener = keel::i32(*)(wl_proxy*, void (**)(void), void*);
    using PfnProxyGetVersion = keel::u32(*)(wl_proxy*);

    PfnDisplayConnect DisplayConnect{nullptr};
    PfnDisplayDisconnect DisplayDisconnect{nullptr};
    PfnDisplayGetFd DisplayGetFd{nullptr};
    PfnDisplayFlush DisplayFlush{nullptr};
    PfnDisplayRoundtrip DisplayRoundtrip{nullptr};
    PfnDisplayDispatchPending DisplayDispatchPending{nullptr};
    PfnDisplayPrepareRead DisplayPrepareRead{nullptr};
    PfnDisplayCancelRead DisplayCancelRead{nullptr};
    PfnDisplayReadEvents DisplayReadEvents{nullptr};
    PfnDisplayGetError DisplayGetError{nullptr};
    PfnProxyMarshalArrayFlags ProxyMarshalArrayFlags{nullptr};
    PfnProxyMarshalArray ProxyMarshalArray{nullptr};
    PfnProxyMarshalArrayCtorVer ProxyMarshalArrayCtorVer{nullptr};
    PfnProxyDestroy ProxyDestroy{nullptr};
    PfnProxyAddListener ProxyAddListener{nullptr};
    PfnProxyGetVersion ProxyGetVersion{nullptr};

    // Core interfaces resolved directly from libwayland-client.so.0
    const wl_interface* ifaceRegistry{nullptr};
    const wl_interface* ifaceCallback{nullptr};
    const wl_interface* ifaceCompositor{nullptr};
    const wl_interface* ifaceSurface{nullptr};
    const wl_interface* ifaceRegion{nullptr};
    const wl_interface* ifaceShm{nullptr};
    const wl_interface* ifaceShmPool{nullptr};
    const wl_interface* ifaceBuffer{nullptr};
    const wl_interface* ifaceSubcompositor{nullptr};
    const wl_interface* ifaceSubsurface{nullptr};
    const wl_interface* ifaceSeat{nullptr};
    const wl_interface* ifacePointer{nullptr};
    const wl_interface* ifaceKeyboard{nullptr};
    const wl_interface* ifaceOutput{nullptr};
    const wl_interface* ifaceDataDeviceManager{nullptr};
    const wl_interface* ifaceDataDevice{nullptr};
    const wl_interface* ifaceDataSource{nullptr};
    const wl_interface* ifaceDataOffer{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libwayland-client.so.0");
        if (!opened) opened = keel::DynLib::open("libwayland-client.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        DisplayConnect = lib.symbol<PfnDisplayConnect>("wl_display_connect");
        DisplayDisconnect = lib.symbol<PfnDisplayDisconnect>("wl_display_disconnect");
        DisplayGetFd = lib.symbol<PfnDisplayGetFd>("wl_display_get_fd");
        DisplayFlush = lib.symbol<PfnDisplayFlush>("wl_display_flush");
        DisplayRoundtrip = lib.symbol<PfnDisplayRoundtrip>("wl_display_roundtrip");
        DisplayDispatchPending = lib.symbol<PfnDisplayDispatchPending>("wl_display_dispatch_pending");
        DisplayPrepareRead = lib.symbol<PfnDisplayPrepareRead>("wl_display_prepare_read");
        DisplayCancelRead = lib.symbol<PfnDisplayCancelRead>("wl_display_cancel_read");
        DisplayReadEvents = lib.symbol<PfnDisplayReadEvents>("wl_display_read_events");
        DisplayGetError = lib.symbol<PfnDisplayGetError>("wl_display_get_error");
        ProxyMarshalArrayFlags = lib.symbol<PfnProxyMarshalArrayFlags>("wl_proxy_marshal_array_flags");
        ProxyMarshalArray = lib.symbol<PfnProxyMarshalArray>("wl_proxy_marshal_array");
        ProxyMarshalArrayCtorVer = lib.symbol<PfnProxyMarshalArrayCtorVer>("wl_proxy_marshal_array_constructor_versioned");
        ProxyDestroy = lib.symbol<PfnProxyDestroy>("wl_proxy_destroy");
        ProxyAddListener = lib.symbol<PfnProxyAddListener>("wl_proxy_add_listener");
        ProxyGetVersion = lib.symbol<PfnProxyGetVersion>("wl_proxy_get_version");

        ifaceRegistry = lib.symbol<const wl_interface*>("wl_registry_interface");
        ifaceCallback = lib.symbol<const wl_interface*>("wl_callback_interface");
        ifaceCompositor = lib.symbol<const wl_interface*>("wl_compositor_interface");
        ifaceSurface = lib.symbol<const wl_interface*>("wl_surface_interface");
        ifaceRegion = lib.symbol<const wl_interface*>("wl_region_interface");
        ifaceShm = lib.symbol<const wl_interface*>("wl_shm_interface");
        ifaceShmPool = lib.symbol<const wl_interface*>("wl_shm_pool_interface");
        ifaceBuffer = lib.symbol<const wl_interface*>("wl_buffer_interface");
        ifaceSubcompositor = lib.symbol<const wl_interface*>("wl_subcompositor_interface");
        ifaceSubsurface = lib.symbol<const wl_interface*>("wl_subsurface_interface");
        ifaceSeat = lib.symbol<const wl_interface*>("wl_seat_interface");
        ifacePointer = lib.symbol<const wl_interface*>("wl_pointer_interface");
        ifaceKeyboard = lib.symbol<const wl_interface*>("wl_keyboard_interface");
        ifaceOutput = lib.symbol<const wl_interface*>("wl_output_interface");
        ifaceDataDeviceManager = lib.symbol<const wl_interface*>("wl_data_device_manager_interface");
        ifaceDataDevice = lib.symbol<const wl_interface*>("wl_data_device_interface");
        ifaceDataSource = lib.symbol<const wl_interface*>("wl_data_source_interface");
        ifaceDataOffer = lib.symbol<const wl_interface*>("wl_data_offer_interface");

        const bool hasMarshal = ProxyMarshalArrayFlags || (ProxyMarshalArray && ProxyMarshalArrayCtorVer);
        return DisplayConnect && DisplayDisconnect && DisplayGetFd && DisplayFlush &&
               DisplayRoundtrip && DisplayDispatchPending && DisplayPrepareRead &&
               DisplayCancelRead && DisplayReadEvents && hasMarshal && ProxyDestroy &&
               ProxyAddListener && ProxyGetVersion && ifaceRegistry && ifaceCallback &&
               ifaceCompositor && ifaceSurface && ifaceRegion && ifaceSeat &&
               ifacePointer && ifaceKeyboard && ifaceOutput;
    }

    wl_proxy* Marshal(
        void* proxy,
        keel::u32 opcode,
        const wl_interface* outIface,
        keel::u32 version,
        keel::u32 flags,
        wl_argument* args
    ) const noexcept {
        auto* p = reinterpret_cast<wl_proxy*>(proxy);
        if (ProxyMarshalArrayFlags) {
            return ProxyMarshalArrayFlags(p, opcode, outIface, version, flags, args);
        }
        wl_proxy* res = nullptr;
        if (outIface) {
            res = ProxyMarshalArrayCtorVer(p, opcode, args, outIface, version);
        } else {
            ProxyMarshalArray(p, opcode, args);
        }
        if ((flags & kWlMarshalFlagDestroy) != 0u) {
            ProxyDestroy(p);
        }
        return res;
    }

    void MarshalVoid(void* proxy, keel::u32 opcode, wl_argument* args = nullptr) const noexcept {
        auto* p = reinterpret_cast<wl_proxy*>(proxy);
        (void)Marshal(p, opcode, nullptr, ProxyGetVersion(p), 0u, args);
    }

    void MarshalDestroy(void* proxy, keel::u32 opcode) const noexcept {
        if (!proxy) return;
        auto* p = reinterpret_cast<wl_proxy*>(proxy);
        (void)Marshal(p, opcode, nullptr, ProxyGetVersion(p), kWlMarshalFlagDestroy, nullptr);
    }

    template <typename T>
    T* MarshalNew(void* proxy, keel::u32 opcode, const wl_interface* iface, wl_argument* args, keel::u32 versionOverride = 0u) const noexcept {
        auto* p = reinterpret_cast<wl_proxy*>(proxy);
        const keel::u32 ver = (versionOverride > 0u) ? versionOverride : ProxyGetVersion(p);
        return reinterpret_cast<T*>(Marshal(p, opcode, iface, ver, 0u, args));
    }

    template <typename Listener>
    void AddListener(void* proxy, const Listener* listener, void* data) const noexcept {
        (void)ProxyAddListener(
            reinterpret_cast<wl_proxy*>(proxy),
            reinterpret_cast<void (**)(void)>(const_cast<Listener*>(listener)),
            data
        );
    }
};

struct WaylandEglApi {
    keel::DynLib lib{};

    using PfnCreate = wl_egl_window*(*)(wl_surface*, keel::i32, keel::i32);
    using PfnDestroy = void(*)(wl_egl_window*);
    using PfnResize = void(*)(wl_egl_window*, keel::i32, keel::i32, keel::i32, keel::i32);

    PfnCreate Create{nullptr};
    PfnDestroy Destroy{nullptr};
    PfnResize Resize{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libwayland-egl.so.1");
        if (!opened) opened = keel::DynLib::open("libwayland-egl.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        Create = lib.symbol<PfnCreate>("wl_egl_window_create");
        Destroy = lib.symbol<PfnDestroy>("wl_egl_window_destroy");
        Resize = lib.symbol<PfnResize>("wl_egl_window_resize");
        return Create && Destroy && Resize;
    }
};

struct wl_cursor_image {
    keel::u32 width;
    keel::u32 height;
    keel::u32 hotspot_x;
    keel::u32 hotspot_y;
    keel::u32 delay;
};

struct wl_cursor {
    keel::u32 image_count;
    wl_cursor_image** images;
    char* name;
};

struct WaylandCursorApi {
    keel::DynLib lib{};

    using PfnThemeLoad = wl_cursor_theme*(*)(const char*, keel::i32, wl_shm*);
    using PfnThemeDestroy = void(*)(wl_cursor_theme*);
    using PfnThemeGetCursor = wl_cursor*(*)(wl_cursor_theme*, const char*);
    using PfnImageGetBuffer = wl_buffer*(*)(wl_cursor_image*);

    PfnThemeLoad ThemeLoad{nullptr};
    PfnThemeDestroy ThemeDestroy{nullptr};
    PfnThemeGetCursor ThemeGetCursor{nullptr};
    PfnImageGetBuffer ImageGetBuffer{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libwayland-cursor.so.0");
        if (!opened) opened = keel::DynLib::open("libwayland-cursor.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        ThemeLoad = lib.symbol<PfnThemeLoad>("wl_cursor_theme_load");
        ThemeDestroy = lib.symbol<PfnThemeDestroy>("wl_cursor_theme_destroy");
        ThemeGetCursor = lib.symbol<PfnThemeGetCursor>("wl_cursor_theme_get_cursor");
        ImageGetBuffer = lib.symbol<PfnImageGetBuffer>("wl_cursor_image_get_buffer");
        return ThemeLoad && ThemeDestroy && ThemeGetCursor && ImageGetBuffer;
    }
};

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

struct XkbApi {
    keel::DynLib lib{};

    static constexpr keel::u32 kContextNoFlags = 0u;
    static constexpr keel::u32 kKeymapFormatTextV1 = 1u;
    static constexpr keel::u32 kKeymapCompileNoFlags = 0u;
    static constexpr keel::u32 kStateModsEffective = 1u << 3u;

    using PfnContextNew = xkb_context*(*)(keel::u32);
    using PfnContextUnref = void(*)(xkb_context*);
    using PfnKeymapNewFromString = xkb_keymap*(*)(xkb_context*, const char*, keel::u32, keel::u32);
    using PfnKeymapUnref = void(*)(xkb_keymap*);
    using PfnKeymapKeyRepeats = keel::i32(*)(xkb_keymap*, keel::u32);
    using PfnStateNew = xkb_state*(*)(xkb_keymap*);
    using PfnStateUnref = void(*)(xkb_state*);
    using PfnStateUpdateMask = keel::u32(*)(xkb_state*, keel::u32, keel::u32, keel::u32, keel::u32, keel::u32, keel::u32);
    using PfnStateKeyGetOneSym = keel::u32(*)(xkb_state*, keel::u32);
    using PfnStateKeyGetUtf8 = keel::i32(*)(xkb_state*, keel::u32, char*, keel::usize);
    using PfnStateModNameIsActive = keel::i32(*)(xkb_state*, const char*, keel::u32);

    PfnContextNew ContextNew{nullptr};
    PfnContextUnref ContextUnref{nullptr};
    PfnKeymapNewFromString KeymapNewFromString{nullptr};
    PfnKeymapUnref KeymapUnref{nullptr};
    PfnKeymapKeyRepeats KeymapKeyRepeats{nullptr};
    PfnStateNew StateNew{nullptr};
    PfnStateUnref StateUnref{nullptr};
    PfnStateUpdateMask StateUpdateMask{nullptr};
    PfnStateKeyGetOneSym StateKeyGetOneSym{nullptr};
    PfnStateKeyGetUtf8 StateKeyGetUtf8{nullptr};
    PfnStateModNameIsActive StateModNameIsActive{nullptr};

    bool Load() noexcept {
        if (lib.is_open()) return true;
        auto opened = keel::DynLib::open("libxkbcommon.so.0");
        if (!opened) opened = keel::DynLib::open("libxkbcommon.so");
        if (!opened) return false;
        lib = keel::move(*opened);

        ContextNew = lib.symbol<PfnContextNew>("xkb_context_new");
        ContextUnref = lib.symbol<PfnContextUnref>("xkb_context_unref");
        KeymapNewFromString = lib.symbol<PfnKeymapNewFromString>("xkb_keymap_new_from_string");
        KeymapUnref = lib.symbol<PfnKeymapUnref>("xkb_keymap_unref");
        KeymapKeyRepeats = lib.symbol<PfnKeymapKeyRepeats>("xkb_keymap_key_repeats");
        StateNew = lib.symbol<PfnStateNew>("xkb_state_new");
        StateUnref = lib.symbol<PfnStateUnref>("xkb_state_unref");
        StateUpdateMask = lib.symbol<PfnStateUpdateMask>("xkb_state_update_mask");
        StateKeyGetOneSym = lib.symbol<PfnStateKeyGetOneSym>("xkb_state_key_get_one_sym");
        StateKeyGetUtf8 = lib.symbol<PfnStateKeyGetUtf8>("xkb_state_key_get_utf8");
        StateModNameIsActive = lib.symbol<PfnStateModNameIsActive>("xkb_state_mod_name_is_active");
        return ContextNew && ContextUnref && KeymapNewFromString && KeymapUnref &&
               StateNew && StateUnref && StateUpdateMask && StateKeyGetOneSym &&
               StateKeyGetUtf8 && StateModNameIsActive;
    }
};

// Runtime-linked Wayland extension interface definitions (identical layout to wayland-scanner 1.24).
// All cross-references to core wl_surface/wl_seat/wl_pointer/wl_output interfaces are patched in Init().
struct WaylandExtInterfaces {
    // xdg-shell (v1..v7)
    const wl_interface* xdgTypes[26]{};
    wl_message xdgWmBaseReq[4]{};
    wl_message xdgWmBaseEv[1]{};
    wl_message xdgPosReq[10]{};
    wl_message xdgSurfReq[5]{};
    wl_message xdgSurfEv[1]{};
    wl_message xdgTopReq[14]{};
    wl_message xdgTopEv[4]{};
    wl_message xdgPopReq[3]{};
    wl_message xdgPopEv[3]{};
    wl_interface xdgWmBase{};
    wl_interface xdgPositioner{};
    wl_interface xdgSurface{};
    wl_interface xdgToplevel{};
    wl_interface xdgPopup{};

    // zxdg-decoration-unstable-v1
    const wl_interface* decoTypes[3]{};
    wl_message decoMgrReq[2]{};
    wl_message decoTopReq[3]{};
    wl_message decoTopEv[1]{};
    wl_interface decoManager{};
    wl_interface decoToplevel{};

    // wp-viewporter
    const wl_interface* vpTypes[6]{};
    wl_message viewporterReq[2]{};
    wl_message viewportReq[3]{};
    wl_interface viewporter{};
    wl_interface viewport{};

    // wp-fractional-scale-v1
    const wl_interface* fracTypes[3]{};
    wl_message fracMgrReq[2]{};
    wl_message fracScaleReq[1]{};
    wl_message fracScaleEv[1]{};
    wl_interface fracScaleManager{};
    wl_interface fracScale{};

    // wp-cursor-shape-v1
    const wl_interface* curShapeTypes[6]{};
    wl_message curShapeMgrReq[3]{};
    wl_message curShapeDevReq[2]{};
    wl_interface curShapeManager{};
    wl_interface curShapeDevice{};

    void Init(const WaylandClientApi& wl) noexcept {
        // --- xdg-shell ---
        for (keel::usize i = 0u; i < 26u; ++i) xdgTypes[i] = nullptr;
        xdgTypes[4]  = &xdgPositioner;
        xdgTypes[5]  = &xdgSurface;
        xdgTypes[6]  = wl.ifaceSurface;
        xdgTypes[7]  = &xdgToplevel;
        xdgTypes[8]  = &xdgPopup;
        xdgTypes[9]  = &xdgSurface;
        xdgTypes[10] = &xdgPositioner;
        xdgTypes[11] = &xdgToplevel;
        xdgTypes[12] = wl.ifaceSeat;
        xdgTypes[16] = wl.ifaceSeat;
        xdgTypes[18] = wl.ifaceSeat;
        xdgTypes[21] = wl.ifaceOutput;
        xdgTypes[22] = wl.ifaceSeat;
        xdgTypes[24] = &xdgPositioner;

        xdgWmBaseReq[0] = {"destroy", "", xdgTypes + 0};
        xdgWmBaseReq[1] = {"create_positioner", "n", xdgTypes + 4};
        xdgWmBaseReq[2] = {"get_xdg_surface", "no", xdgTypes + 5};
        xdgWmBaseReq[3] = {"pong", "u", xdgTypes + 0};
        xdgWmBaseEv[0]  = {"ping", "u", xdgTypes + 0};
        xdgWmBase = {"xdg_wm_base", 6, 4, xdgWmBaseReq, 1, xdgWmBaseEv};

        xdgPosReq[0] = {"destroy", "", xdgTypes + 0};
        xdgPosReq[1] = {"set_size", "ii", xdgTypes + 0};
        xdgPosReq[2] = {"set_anchor_rect", "iiii", xdgTypes + 0};
        xdgPosReq[3] = {"set_anchor", "u", xdgTypes + 0};
        xdgPosReq[4] = {"set_gravity", "u", xdgTypes + 0};
        xdgPosReq[5] = {"set_constraint_adjustment", "u", xdgTypes + 0};
        xdgPosReq[6] = {"set_offset", "ii", xdgTypes + 0};
        xdgPosReq[7] = {"set_reactive", "3", xdgTypes + 0};
        xdgPosReq[8] = {"set_parent_size", "3ii", xdgTypes + 0};
        xdgPosReq[9] = {"set_parent_configure", "3u", xdgTypes + 0};
        xdgPositioner = {"xdg_positioner", 6, 10, xdgPosReq, 0, nullptr};

        xdgSurfReq[0] = {"destroy", "", xdgTypes + 0};
        xdgSurfReq[1] = {"get_toplevel", "n", xdgTypes + 7};
        xdgSurfReq[2] = {"get_popup", "n?oo", xdgTypes + 8};
        xdgSurfReq[3] = {"set_window_geometry", "iiii", xdgTypes + 0};
        xdgSurfReq[4] = {"ack_configure", "u", xdgTypes + 0};
        xdgSurfEv[0]  = {"configure", "u", xdgTypes + 0};
        xdgSurface = {"xdg_surface", 6, 5, xdgSurfReq, 1, xdgSurfEv};

        xdgTopReq[0]  = {"destroy", "", xdgTypes + 0};
        xdgTopReq[1]  = {"set_parent", "?o", xdgTypes + 11};
        xdgTopReq[2]  = {"set_title", "s", xdgTypes + 0};
        xdgTopReq[3]  = {"set_app_id", "s", xdgTypes + 0};
        xdgTopReq[4]  = {"show_window_menu", "ouii", xdgTypes + 12};
        xdgTopReq[5]  = {"move", "ou", xdgTypes + 16};
        xdgTopReq[6]  = {"resize", "ouu", xdgTypes + 18};
        xdgTopReq[7]  = {"set_max_size", "ii", xdgTypes + 0};
        xdgTopReq[8]  = {"set_min_size", "ii", xdgTypes + 0};
        xdgTopReq[9]  = {"set_maximized", "", xdgTypes + 0};
        xdgTopReq[10] = {"unset_maximized", "", xdgTypes + 0};
        xdgTopReq[11] = {"set_fullscreen", "?o", xdgTypes + 21};
        xdgTopReq[12] = {"unset_fullscreen", "", xdgTypes + 0};
        xdgTopReq[13] = {"set_minimized", "", xdgTypes + 0};
        xdgTopEv[0]   = {"configure", "iia", xdgTypes + 0};
        xdgTopEv[1]   = {"close", "", xdgTypes + 0};
        xdgTopEv[2]   = {"configure_bounds", "4ii", xdgTypes + 0};
        xdgTopEv[3]   = {"wm_capabilities", "5a", xdgTypes + 0};
        xdgToplevel = {"xdg_toplevel", 6, 14, xdgTopReq, 4, xdgTopEv};

        xdgPopReq[0] = {"destroy", "", xdgTypes + 0};
        xdgPopReq[1] = {"grab", "ou", xdgTypes + 22};
        xdgPopReq[2] = {"reposition", "3ou", xdgTypes + 24};
        xdgPopEv[0]  = {"configure", "iiii", xdgTypes + 0};
        xdgPopEv[1]  = {"popup_done", "", xdgTypes + 0};
        xdgPopEv[2]  = {"repositioned", "3u", xdgTypes + 0};
        xdgPopup = {"xdg_popup", 6, 3, xdgPopReq, 3, xdgPopEv};

        // --- zxdg-decoration-unstable-v1 ---
        decoTypes[0] = nullptr;
        decoTypes[1] = &decoToplevel;
        decoTypes[2] = &xdgToplevel;
        decoMgrReq[0] = {"destroy", "", decoTypes + 0};
        decoMgrReq[1] = {"get_toplevel_decoration", "no", decoTypes + 1};
        decoManager = {"zxdg_decoration_manager_v1", 1, 2, decoMgrReq, 0, nullptr};

        decoTopReq[0] = {"destroy", "", decoTypes + 0};
        decoTopReq[1] = {"set_mode", "u", decoTypes + 0};
        decoTopReq[2] = {"unset_mode", "", decoTypes + 0};
        decoTopEv[0]  = {"configure", "u", decoTypes + 0};
        decoToplevel = {"zxdg_toplevel_decoration_v1", 1, 3, decoTopReq, 1, decoTopEv};

        // --- wp-viewporter ---
        for (keel::usize i = 0u; i < 4u; ++i) vpTypes[i] = nullptr;
        vpTypes[4] = &viewport;
        vpTypes[5] = wl.ifaceSurface;
        viewporterReq[0] = {"destroy", "", vpTypes + 0};
        viewporterReq[1] = {"get_viewport", "no", vpTypes + 4};
        viewporter = {"wp_viewporter", 1, 2, viewporterReq, 0, nullptr};

        viewportReq[0] = {"destroy", "", vpTypes + 0};
        viewportReq[1] = {"set_source", "ffff", vpTypes + 0};
        viewportReq[2] = {"set_destination", "ii", vpTypes + 0};
        viewport = {"wp_viewport", 1, 3, viewportReq, 0, nullptr};

        // --- wp-fractional-scale-v1 ---
        fracTypes[0] = nullptr;
        fracTypes[1] = &fracScale;
        fracTypes[2] = wl.ifaceSurface;
        fracMgrReq[0] = {"destroy", "", fracTypes + 0};
        fracMgrReq[1] = {"get_fractional_scale", "no", fracTypes + 1};
        fracScaleManager = {"wp_fractional_scale_manager_v1", 1, 2, fracMgrReq, 0, nullptr};

        fracScaleReq[0] = {"destroy", "", fracTypes + 0};
        fracScaleEv[0]  = {"preferred_scale", "u", fracTypes + 0};
        fracScale = {"wp_fractional_scale_v1", 1, 1, fracScaleReq, 1, fracScaleEv};

        // --- wp-cursor-shape-v1 ---
        curShapeTypes[0] = nullptr;
        curShapeTypes[1] = nullptr;
        curShapeTypes[2] = &curShapeDevice;
        curShapeTypes[3] = wl.ifacePointer;
        curShapeTypes[4] = &curShapeDevice;
        curShapeTypes[5] = nullptr;
        curShapeMgrReq[0] = {"destroy", "", curShapeTypes + 0};
        curShapeMgrReq[1] = {"get_pointer", "no", curShapeTypes + 2};
        curShapeMgrReq[2] = {"get_tablet_tool_v2", "no", curShapeTypes + 4};
        curShapeManager = {"wp_cursor_shape_manager_v1", 1, 3, curShapeMgrReq, 0, nullptr};

        curShapeDevReq[0] = {"destroy", "", curShapeTypes + 0};
        curShapeDevReq[1] = {"set_shape", "uu", curShapeTypes + 0};
        curShapeDevice = {"wp_cursor_shape_device_v1", 1, 2, curShapeDevReq, 0, nullptr};
    }
};

} // namespace guikit::native::wl_detail
