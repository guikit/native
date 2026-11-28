#pragma once

#include "keel/array.h"
#include "keel/core.h"
#include "keel/string.h"
#include "wayland_protocols.hpp"

namespace guikit::native::wl_detail {

/// Encapsulates Wayland `wl_data_device` / `wl_data_source` / `wl_data_offer` UTF-8 clipboard I/O.
/// Uses `pipe2(O_CLOEXEC | O_NONBLOCK)` and `SIGPIPE`-safe writes so broken peer pipes cannot crash the host.
class WaylandClipboard {
public:
    WaylandClipboard() noexcept = default;
    ~WaylandClipboard() noexcept { Destroy(); }

    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    void Attach(
        const WaylandClientApi* wlApi,
        wl_display* display,
        wl_data_device_manager* dataDeviceMgr,
        wl_seat* seat
    ) noexcept;

    void Destroy() noexcept;

    void SetText(keel::StringRef text, keel::u32 inputSerial);
    [[nodiscard]] keel::String GetText();

private:
    struct OfferEntry {
        wl_data_offer* offer{nullptr};
        bool hasUtf8Mime{false};
        bool hasUtf8String{false};
        bool hasPlainText{false};
    };

    void DestroyOffer(wl_data_offer* offer) noexcept;
    void ClearPendingOffers() noexcept;

    static void OnDataDeviceOffer(void* data, wl_data_device*, wl_data_offer* offer);
    static void OnDataDeviceEnter(void*, wl_data_device*, keel::u32, wl_surface*, WlFixed, WlFixed, wl_data_offer*);
    static void OnDataDeviceLeave(void*, wl_data_device*);
    static void OnDataDeviceMotion(void*, wl_data_device*, keel::u32, WlFixed, WlFixed);
    static void OnDataDeviceDrop(void*, wl_data_device*);
    static void OnDataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer);

    static void OnDataSourceTarget(void*, wl_data_source*, const char*);
    static void OnDataSourceSend(void* data, wl_data_source*, const char*, keel::i32 fd);
    static void OnDataSourceCancelled(void* data, wl_data_source* src);
    static void OnDataSourceDndDrop(void*, wl_data_source*);
    static void OnDataSourceDndFinished(void*, wl_data_source*);
    static void OnDataSourceAction(void*, wl_data_source*, keel::u32);

    static void OnDataOfferMime(void* data, wl_data_offer* offer, const char* mime);
    static void OnDataOfferSourceActions(void*, wl_data_offer*, keel::u32);
    static void OnDataOfferAction(void*, wl_data_offer*, keel::u32);

    static const wl_data_device_listener kDataDeviceListener;
    static const wl_data_source_listener kDataSourceListener;
    static const wl_data_offer_listener kDataOfferListener;

    const WaylandClientApi* m_wl{nullptr};
    wl_display* m_display{nullptr};
    wl_data_device_manager* m_dataDeviceMgr{nullptr};
    wl_data_device* m_dataDevice{nullptr};
    wl_data_source* m_ourDataSource{nullptr};
    wl_data_offer* m_selectionOffer{nullptr};
    keel::String m_selectionMime{};
    keel::Array<OfferEntry> m_pendingOffers{};
    keel::String m_clipboardText{};
    bool m_ownedLocally{false};
};

} // namespace guikit::native::wl_detail
