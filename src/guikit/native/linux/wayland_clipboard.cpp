#include "wayland_clipboard.hpp"

#include "keel/time.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace guikit::native::wl_detail {

#if defined(__linux__) && !defined(__ANDROID__)

namespace {

void EnsureSigPipeIgnored() noexcept {
    static bool s_checked = false;
    if (!s_checked) {
        struct sigaction cur{};
        if (::sigaction(SIGPIPE, nullptr, &cur) == 0 && cur.sa_handler == SIG_DFL) {
            struct sigaction ign{};
            ign.sa_handler = SIG_IGN;
            (void)::sigaction(SIGPIPE, &ign, nullptr);
        }
        s_checked = true;
    }
}

} // namespace

const wl_data_device_listener WaylandClipboard::kDataDeviceListener = {
    &WaylandClipboard::OnDataDeviceOffer,
    &WaylandClipboard::OnDataDeviceEnter,
    &WaylandClipboard::OnDataDeviceLeave,
    &WaylandClipboard::OnDataDeviceMotion,
    &WaylandClipboard::OnDataDeviceDrop,
    &WaylandClipboard::OnDataDeviceSelection
};

const wl_data_source_listener WaylandClipboard::kDataSourceListener = {
    &WaylandClipboard::OnDataSourceTarget,
    &WaylandClipboard::OnDataSourceSend,
    &WaylandClipboard::OnDataSourceCancelled,
    &WaylandClipboard::OnDataSourceDndDrop,
    &WaylandClipboard::OnDataSourceDndFinished,
    &WaylandClipboard::OnDataSourceAction
};

const wl_data_offer_listener WaylandClipboard::kDataOfferListener = {
    &WaylandClipboard::OnDataOfferMime,
    &WaylandClipboard::OnDataOfferSourceActions,
    &WaylandClipboard::OnDataOfferAction
};

void WaylandClipboard::Attach(
    const WaylandClientApi* wlApi,
    wl_display* display,
    wl_data_device_manager* dataDeviceMgr,
    wl_seat* seat
) noexcept {
    Destroy();
    m_wl = wlApi;
    m_display = display;
    m_dataDeviceMgr = dataDeviceMgr;
    if (!m_wl || !m_display || !m_dataDeviceMgr || !seat || !m_wl->ifaceDataDevice) {
        return;
    }

    EnsureSigPipeIgnored();

    wl_argument ddArgs[2]{};
    ddArgs[0].o = nullptr;
    ddArgs[1].o = reinterpret_cast<wl_object*>(seat);
    m_dataDevice = m_wl->MarshalNew<wl_data_device>(
        m_dataDeviceMgr,
        kWlDataDevMgrGetDataDevice,
        m_wl->ifaceDataDevice,
        ddArgs
    );
    if (m_dataDevice) {
        m_wl->AddListener(m_dataDevice, &kDataDeviceListener, this);
    }
}

void WaylandClipboard::DestroyOffer(wl_data_offer* offer) noexcept {
    if (!offer || !m_wl) return;
    m_wl->MarshalDestroy(offer, kWlDataOfferDestroy);
}

void WaylandClipboard::ClearPendingOffers() noexcept {
    for (const OfferEntry& e : m_pendingOffers) {
        if (e.offer && e.offer != m_selectionOffer) {
            DestroyOffer(e.offer);
        }
    }
    m_pendingOffers.clear();
}

void WaylandClipboard::Destroy() noexcept {
    if (m_wl) {
        ClearPendingOffers();
        if (m_selectionOffer) {
            DestroyOffer(m_selectionOffer);
            m_selectionOffer = nullptr;
        }
        if (m_ourDataSource) {
            m_wl->MarshalDestroy(m_ourDataSource, kWlDataSourceDestroy);
            m_ourDataSource = nullptr;
        }
        if (m_dataDevice) {
            if (m_wl->ProxyGetVersion(reinterpret_cast<wl_proxy*>(m_dataDevice)) >= 2u) {
                m_wl->MarshalDestroy(m_dataDevice, kWlDataDeviceRelease);
            } else {
                m_wl->ProxyDestroy(reinterpret_cast<wl_proxy*>(m_dataDevice));
            }
            m_dataDevice = nullptr;
        }
    }
    m_pendingOffers.clear();
    m_selectionOffer = nullptr;
    m_selectionMime.clear();
    m_ourDataSource = nullptr;
    m_dataDevice = nullptr;
    m_dataDeviceMgr = nullptr;
    m_display = nullptr;
    m_wl = nullptr;
    m_ownedLocally = false;
}

void WaylandClipboard::SetText(keel::StringRef text, keel::u32 inputSerial) {
    m_clipboardText = keel::String::from(text);
    m_ownedLocally = true;
    if (!m_wl || !m_display || !m_dataDeviceMgr || !m_dataDevice || inputSerial == 0u) {
        return;
    }

    EnsureSigPipeIgnored();

    if (m_ourDataSource) {
        m_wl->MarshalDestroy(m_ourDataSource, kWlDataSourceDestroy);
        m_ourDataSource = nullptr;
    }
    wl_argument createArg[1]{};
    m_ourDataSource = m_wl->MarshalNew<wl_data_source>(
        m_dataDeviceMgr,
        kWlDataDevMgrCreateSource,
        m_wl->ifaceDataSource,
        createArg
    );
    if (!m_ourDataSource) return;

    m_wl->AddListener(m_ourDataSource, &kDataSourceListener, this);
    static constexpr const char* kMimes[3] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};
    for (const char* mime : kMimes) {
        wl_argument offerArg[1]{};
        offerArg[0].s = mime;
        m_wl->MarshalVoid(m_ourDataSource, kWlDataSourceOffer, offerArg);
    }

    wl_argument selArgs[2]{};
    selArgs[0].o = reinterpret_cast<wl_object*>(m_ourDataSource);
    selArgs[1].u = inputSerial;
    m_wl->MarshalVoid(m_dataDevice, kWlDataDeviceSetSelection, selArgs);
    (void)m_wl->DisplayFlush(m_display);
}

keel::String WaylandClipboard::GetText() {
    if (m_ourDataSource != nullptr || m_ownedLocally) {
        return m_clipboardText.clone();
    }
    if (!m_wl || !m_display || !m_selectionOffer || m_selectionMime.empty()) {
        return {};
    }

    int pipeFds[2]{-1, -1}; // keel-lint: ignore
    if (::pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0) {
        return {};
    }

    wl_argument recvArgs[2]{};
    recvArgs[0].s = m_selectionMime.c_str();
    recvArgs[1].h = pipeFds[1];
    m_wl->MarshalVoid(m_selectionOffer, kWlDataOfferReceive, recvArgs);
    ::close(pipeFds[1]);
    (void)m_wl->DisplayFlush(m_display);

    keel::String result{};
    const keel::u64 deadlineMs = keel::monotonic_ms() + 150u;
    char buf[1024]{};
    for (;;) {
        const keel::u64 now = keel::monotonic_ms();
        if (now >= deadlineMs) break;
        struct pollfd pfd{};
        pfd.fd = pipeFds[0];
        pfd.events = POLLIN;
        const keel::i32 rc = ::poll(&pfd, 1, keel::as<keel::i32>(deadlineMs - now));
        if (rc <= 0) break;
        const keel::isize n = ::read(pipeFds[0], buf, sizeof(buf));
        if (n > 0) {
            result.append(keel::StringRef(buf, keel::as<keel::usize>(n)));
            if (result.size() >= 1024u * 1024u) break;
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
    ::close(pipeFds[0]);
    return result;
}

void WaylandClipboard::OnDataDeviceOffer(void* data, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (!self || !self->m_wl || !offer) return;
    OfferEntry entry{};
    entry.offer = offer;
    self->m_pendingOffers.push_back(entry);
    self->m_wl->AddListener(offer, &kDataOfferListener, self);
}

void WaylandClipboard::OnDataDeviceEnter(void*, wl_data_device*, keel::u32, wl_surface*, WlFixed, WlFixed, wl_data_offer*) {}
void WaylandClipboard::OnDataDeviceLeave(void*, wl_data_device*) {}
void WaylandClipboard::OnDataDeviceMotion(void*, wl_data_device*, keel::u32, WlFixed, WlFixed) {}
void WaylandClipboard::OnDataDeviceDrop(void*, wl_data_device*) {}

void WaylandClipboard::OnDataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (!self) return;
    if (self->m_selectionOffer && self->m_selectionOffer != offer) {
        self->DestroyOffer(self->m_selectionOffer);
    }
    self->m_selectionOffer = offer;
    self->m_selectionMime.clear();

    if (offer) {
        if (!self->m_ourDataSource) {
            self->m_ownedLocally = false;
        }
        for (keel::usize i = 0u; i < self->m_pendingOffers.size(); ++i) {
            if (self->m_pendingOffers[i].offer == offer) {
                const OfferEntry& e = self->m_pendingOffers[i];
                if (e.hasUtf8Mime) self->m_selectionMime = keel::String("text/plain;charset=utf-8");
                else if (e.hasUtf8String) self->m_selectionMime = keel::String("UTF8_STRING");
                else if (e.hasPlainText) self->m_selectionMime = keel::String("text/plain");
                self->m_pendingOffers.erase(i);
                break;
            }
        }
    }
    self->ClearPendingOffers();
}

void WaylandClipboard::OnDataSourceTarget(void*, wl_data_source*, const char*) {}

void WaylandClipboard::OnDataSourceSend(void* data, wl_data_source*, const char*, keel::i32 fd) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (fd < 0) return;
    EnsureSigPipeIgnored();
    if (self && !self->m_clipboardText.empty()) {
        const char* ptr = self->m_clipboardText.c_str();
        keel::usize remaining = self->m_clipboardText.size();
        while (remaining > 0u) {
            const keel::isize written = ::write(fd, ptr, remaining);
            if (written <= 0) {
                if (written < 0 && errno == EINTR) continue;
                break;
            }
            ptr += written;
            remaining -= keel::as<keel::usize>(written);
        }
    }
    ::close(fd);
}

void WaylandClipboard::OnDataSourceCancelled(void* data, wl_data_source* src) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (!self || !self->m_wl) return;
    if (src == self->m_ourDataSource) {
        self->m_wl->MarshalDestroy(self->m_ourDataSource, kWlDataSourceDestroy);
        self->m_ourDataSource = nullptr;
        self->m_ownedLocally = false;
    }
}

void WaylandClipboard::OnDataSourceDndDrop(void*, wl_data_source*) {}
void WaylandClipboard::OnDataSourceDndFinished(void*, wl_data_source*) {}
void WaylandClipboard::OnDataSourceAction(void*, wl_data_source*, keel::u32) {}

void WaylandClipboard::OnDataOfferMime(void* data, wl_data_offer* offer, const char* mime) {
    auto* self = static_cast<WaylandClipboard*>(data);
    if (!self || !offer || !mime) return;
    const keel::StringRef m(mime);
    for (OfferEntry& e : self->m_pendingOffers) {
        if (e.offer == offer) {
            if (m == "text/plain;charset=utf-8") e.hasUtf8Mime = true;
            else if (m == "UTF8_STRING") e.hasUtf8String = true;
            else if (m == "text/plain") e.hasPlainText = true;
            break;
        }
    }
}

void WaylandClipboard::OnDataOfferSourceActions(void*, wl_data_offer*, keel::u32) {}
void WaylandClipboard::OnDataOfferAction(void*, wl_data_offer*, keel::u32) {}

#else

void WaylandClipboard::Attach(const WaylandClientApi*, wl_display*, wl_data_device_manager*, wl_seat*) noexcept {}
void WaylandClipboard::Destroy() noexcept {}
void WaylandClipboard::SetText(keel::StringRef, keel::u32) {}
keel::String WaylandClipboard::GetText() { return {}; }

#endif

} // namespace guikit::native::wl_detail
