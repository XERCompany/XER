/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * Auracast BLE Audio sink implementation.
 *
 * ── BLE advertising packet structure (from Bluetooth Core 5.2, Vol 6 Part B) ──
 *
 *   Each advertising event contains one or more AD structures:
 *     [Length (1B)] [AD_Type (1B)] [AD_Data (Length-1 bytes)]
 *
 *   Relevant AD types for Auracast:
 *     0x01  Flags                (LE General Discoverable, BR/EDR Not Supported)
 *     0x02  Incomplete 16-bit UUIDs
 *     0x03  Complete 16-bit UUIDs
 *     0x08  Shortened Local Name
 *     0x09  Complete Local Name  → broadcast_name
 *     0x16  Service Data 16-bit  → UUID (2B LE) + payload
 *                                  UUID 0x1856 = Public Broadcast Announcement
 *                                    [0] features byte (PBP Table 3.3)
 *                                    [1] metadata_length
 *                                    [2..metadata_length+1] LTV metadata
 *     0x2C  Broadcast Name (Bluetooth 5.3 extended, UTF-8, up to 248 chars)
 *
 *   Broadcast_ID (3 bytes, little-endian) for Auracast is carried inside
 *   the ACAD (Additional Controller Advertising Data) of the Periodic
 *   Advertising SyncInfo, not in the standard ADV payload. However, some
 *   implementations encode it in a vendor-specific AD (0xFF) or in AD 0x16
 *   with a Bluetooth SIG assigned Broadcast Audio Announcement UUID (0x1852).
 *
 * ── Linux HCI socket ──────────────────────────────────────────────────────────
 *
 *   socket(AF_BLUETOOTH=31, SOCK_RAW=3, BTPROTO_HCI=1)
 *   bind to struct sockaddr_hci { AF_BLUETOOTH, hci_dev, HCI_CHANNEL_RAW=0 }
 *   setsockopt HCI_FILTER (level=0): pass LE_META_EVENT only
 *   write HCI_CMD: LE_SET_SCAN_PARAM + LE_SET_SCAN_ENABLE
 *   read HCI events → parse EVT_LE_ADVERTISING_REPORT
 *
 * ── Linux BT_ISO socket (kernel 5.15+) ───────────────────────────────────────
 *
 *   socket(AF_BLUETOOTH=31, SOCK_SEQPACKET=5, BTPROTO_ISO=13)
 *   setsockopt(BT_ISO_QOS) — set BIG sync parameters
 *   connect(sockaddr_iso with source BD_ADDR + SID)
 *   → kernel triggers: LE PA Sync Create → LE BIG Create Sync
 *   read() → LC3 audio frames (one per isochronous interval, 7.5 or 10 ms)
 */
#include "content/device/cast/aura/aura_cast_session.h"
#include "content/device/cast/aura/aura_audio_sink.h"
#include "audio/audio_core/audio_engine.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sstream>
#include <unordered_map>

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace device::cast::aura {

// ──────────────────────────────────────────────────────────────────────────────
// Linux BLE HCI constants (inline — no libbluetooth dependency)
// ──────────────────────────────────────────────────────────────────────────────
namespace {

#if defined(__linux__)

constexpr int kAfBluetooth   = 31;
constexpr int kBtprotoHci    = 1;
constexpr int kBtprotoIso    = 13;   // Linux 5.15+
constexpr int kSockSeqpacket = 5;
constexpr int kHciChannelRaw = 0;

// HCI packet types
constexpr uint8_t kHciCommandPkt = 0x01;
constexpr uint8_t kHciEventPkt   = 0x04;

// HCI LE commands
constexpr uint16_t kHciOpLeSetScanParam  = 0x200B;
constexpr uint16_t kHciOpLeSetScanEnable = 0x200C;

// HCI events
constexpr uint8_t kEvtLeMetaEvent    = 0x3E;
constexpr uint8_t kEvtLeAdvReport    = 0x02;  // LE Advertising Report sub-event

// Bluetooth SIG UUIDs (16-bit) relevant to Auracast
constexpr uint16_t kUuidPublicBroadcastAnnouncement = 0x1856;  // PBP 1.0
constexpr uint16_t kUuidBroadcastAudioAnnouncement  = 0x1852;  // BAP (carries Broadcast_ID)
constexpr uint16_t kUuidBroadcastAudioScanService   = 0x184F;  // BASS (on sink side)

// AD Types
constexpr uint8_t kAdTypeFlags          = 0x01;
constexpr uint8_t kAdTypeUuid16Incomplete = 0x02;
constexpr uint8_t kAdTypeUuid16Complete   = 0x03;
constexpr uint8_t kAdTypeNameShort        = 0x08;
constexpr uint8_t kAdTypeNameComplete     = 0x09;
constexpr uint8_t kAdTypeServiceData16    = 0x16;
constexpr uint8_t kAdTypeBroadcastName    = 0x30;  // Bluetooth 5.3 extension

// HCI sockaddr
struct SockaddrHci {
    uint16_t hci_family;   // AF_BLUETOOTH
    uint16_t hci_dev;      // hci0 = 0
    uint16_t hci_channel;  // HCI_CHANNEL_RAW = 0
};

// HCI filter (128-bit) — select event packets and LE_META subevent
struct HciFilter {
    uint32_t type_mask;     // bit 4 = HCI_EVENT_PKT
    uint32_t event_mask[2]; // bit 0x3E = LE_META_EVENT
    uint16_t opcode;
};

// Pack a 2-byte LE16 command opcode header + parameter total length.
struct HciCmdHdr {
    uint8_t  pkt_type;    // 0x01 = HCI_COMMAND_PKT
    uint16_t opcode;      // OGF|OCF little-endian
    uint8_t  plen;        // parameter total length
} __attribute__((packed));

// LE Set Scan Parameters (HCI_LE_Set_Scan_Parameters, Vol 4 Part E §7.8.10)
struct LeSetScanParam {
    uint8_t  scan_type;       // 0=passive, 1=active
    uint16_t scan_interval;   // N × 0.625 ms; default 0x0010 (10 ms)
    uint16_t scan_window;     // N × 0.625 ms; must be ≤ interval; default 0x0010
    uint8_t  own_addr_type;   // 0=public
    uint8_t  filter_policy;   // 0=accept all
} __attribute__((packed));

// LE Set Scan Enable (§7.8.11)
struct LeSetScanEnable {
    uint8_t enable;       // 0=off, 1=on
    uint8_t filter_dup;   // 0=allow duplicates, 1=filter
} __attribute__((packed));

// ──────────────────────────────────────────────────────────────────────────────
// Helper: format BD_ADDR as "AA:BB:CC:DD:EE:FF" (addr is stored little-endian)
static std::string FormatBdAddr(const uint8_t addr[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    return buf;
}

// Helper: write exactly |len| bytes to fd.
static bool HciWrite(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        const ssize_t n = write(fd, p, len);
        if (n <= 0) return false;
        p   += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

// Helper: send a simple HCI command with a plain struct payload.
template<typename T>
static bool HciSendCmd(int fd, uint16_t opcode, const T& params) {
    HciCmdHdr hdr;
    hdr.pkt_type = kHciCommandPkt;
    hdr.opcode   = opcode;
    hdr.plen     = static_cast<uint8_t>(sizeof(T));
    if (!HciWrite(fd, &hdr, sizeof(hdr))) return false;
    return HciWrite(fd, &params, sizeof(T));
}

// ──────────────────────────────────────────────────────────────────────────────
// Parse AD structures from a raw advertising data buffer.
// Returns true and populates |dev| if this is an Auracast source.
static bool ParseAdvData(const uint8_t* data, uint8_t data_len,
                         AuraCastDevice& dev) {
    bool is_auracast = false;
    size_t i = 0;
    while (i < data_len) {
        const uint8_t ad_len  = data[i];
        if (ad_len == 0 || i + ad_len >= data_len) break;
        const uint8_t ad_type = data[i + 1];
        const uint8_t* ad_data = data + i + 2;
        const uint8_t  ad_dlen = ad_len - 1;

        switch (ad_type) {
        case kAdTypeNameComplete:
        case kAdTypeNameShort:
            dev.broadcast_name.assign(
                reinterpret_cast<const char*>(ad_data), ad_dlen);
            break;

        case kAdTypeBroadcastName:  // 0x30 — Bluetooth 5.3 Broadcast Name
            dev.broadcast_name.assign(
                reinterpret_cast<const char*>(ad_data), ad_dlen);
            break;

        case kAdTypeServiceData16:
            if (ad_dlen >= 2) {
                const uint16_t uuid =
                    static_cast<uint16_t>(ad_data[0]) |
                    (static_cast<uint16_t>(ad_data[1]) << 8);  // LE16

                if (uuid == kUuidPublicBroadcastAnnouncement && ad_dlen >= 3) {
                    // PBP spec: [UUID16 2B][Features 1B][Metadata_Length 1B][...]
                    dev.features  = PbpFeatures(ad_data[2]);
                    is_auracast   = true;
                }
                if (uuid == kUuidBroadcastAudioAnnouncement && ad_dlen >= 5) {
                    // BAP: [UUID16 2B][Broadcast_ID 3B LE]
                    dev.broadcast_id =
                        static_cast<uint32_t>(ad_data[2]) |
                        (static_cast<uint32_t>(ad_data[3]) << 8) |
                        (static_cast<uint32_t>(ad_data[4]) << 16);
                }
            }
            break;

        default:
            break;
        }
        i += ad_len + 1;
    }
    return is_auracast;
}

#endif  // __linux__
}  // namespace

// ──────────────────────────────────────────────────────────────────────────────

AuraCastSession& AuraCastSession::Instance() {
    static AuraCastSession inst;
    return inst;
}

// ──────────────────────────────────────────────────────────────────────────────
// ScanForSources — BLE passive scan via raw HCI socket
// ──────────────────────────────────────────────────────────────────────────────
std::vector<AuraCastDevice> AuraCastSession::ScanForSources(
    int timeout_ms, int hci_dev) const {
    (void)hci_dev;
    std::vector<AuraCastDevice> found;

#if defined(__linux__)
    // Open a raw HCI socket.
    const int fd = socket(kAfBluetooth, SOCK_RAW, kBtprotoHci);
    if (fd < 0) return found;

    // Bind to the specified HCI device (hci0 by default).
    SockaddrHci sa{};
    sa.hci_family  = static_cast<uint16_t>(kAfBluetooth);
    sa.hci_dev     = static_cast<uint16_t>(hci_dev);
    sa.hci_channel = static_cast<uint16_t>(kHciChannelRaw);
    if (bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        close(fd); return found;
    }

    // Set HCI filter: only HCI_EVENT_PKT, only LE_META_EVENT.
    // type_mask bit 4 = HCI_EVENT_PKT; event_mask[1] bit 30 = 0x3E/2 - 32 = bit 30.
    HciFilter flt{};
    flt.type_mask  = 1u << kHciEventPkt;     // event packets
    // LE_META_EVENT = 0x3E = 62.  event_mask[1] covers events 32-63.
    flt.event_mask[1] = 1u << (kEvtLeMetaEvent - 32);
    flt.opcode     = 0;
    setsockopt(fd, kBtprotoHci, /*HCI_FILTER=*/2,
               &flt, static_cast<socklen_t>(sizeof(flt)));

    // LE Set Scan Parameters: passive, 10 ms interval & window, public addr.
    {
        LeSetScanParam p{};
        p.scan_type     = 0;      // passive — do not send SCAN_REQ
        p.scan_interval = 0x0010; // 10 ms
        p.scan_window   = 0x0010; // 10 ms
        p.own_addr_type = 0;      // public
        p.filter_policy = 0;      // accept all
        HciSendCmd(fd, kHciOpLeSetScanParam, p);
    }

    // LE Set Scan Enable: on, allow duplicates (to catch all sources).
    {
        LeSetScanEnable p{};
        p.enable     = 1;
        p.filter_dup = 0;
        HciSendCmd(fd, kHciOpLeSetScanEnable, p);
    }

    // Map BD_ADDR → device to deduplicate.
    std::unordered_map<std::string, AuraCastDevice> seen;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        // Poll with a short timeout so we can check the deadline.
        pollfd pfd{ fd, POLLIN, 0 };
        const int ms_left = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (ms_left <= 0) break;
        const int r = poll(&pfd, 1, std::min(ms_left, 50));
        if (r <= 0) continue;

        // Read one HCI event.
        uint8_t buf[260];
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 4) continue;

        // buf[0] = packet type (0x04 = HCI_EVENT_PKT)
        // buf[1] = event code
        // buf[2] = parameter total length
        // buf[3] = LE subevent code (for LE_META_EVENT)
        if (buf[0] != kHciEventPkt)   continue;
        if (buf[1] != kEvtLeMetaEvent) continue;
        if (buf[3] != kEvtLeAdvReport) continue;

        // LE Advertising Report:
        // buf[4]  = num_reports (usually 1)
        // Each report:
        //   [0]  event_type  (0=ADV_IND, 2=ADV_NONCONN_IND, ...)
        //   [1]  addr_type   (0=public, 1=random)
        //   [2..7] BD_ADDR   (6 bytes little-endian)
        //   [8]  data_length
        //   [9..9+data_length-1] AD data
        //   [last] RSSI (int8_t)
        const uint8_t num = buf[4];
        size_t off = 5;
        for (uint8_t rep = 0; rep < num && off + 9 < static_cast<size_t>(n); ++rep) {
            const uint8_t  ev_type   = buf[off + 0];
            const uint8_t  addr_type = buf[off + 1];
            const uint8_t* addr      = buf + off + 2;   // 6 bytes
            const uint8_t  data_len  = buf[off + 8];
            const uint8_t* ad_data   = buf + off + 9;
            (void)ev_type;

            if (off + 9 + data_len >= static_cast<size_t>(n)) break;
            const int8_t rssi = static_cast<int8_t>(buf[off + 9 + data_len]);
            off += 9 + data_len + 1;

            AuraCastDevice dev;
            std::memcpy(dev.bd_addr.data(), addr, 6);
            dev.bd_addr_type = addr_type;
            dev.addr_str     = FormatBdAddr(addr);
            dev.rssi         = rssi;

            if (!ParseAdvData(ad_data, data_len, dev)) continue;  // not Auracast

            // Keep the entry with the best (highest) RSSI.
            auto it = seen.find(dev.addr_str);
            if (it == seen.end() || rssi > it->second.rssi)
                seen[dev.addr_str] = std::move(dev);
        }
    }

    // Disable scan.
    {
        LeSetScanEnable p{ 0, 0 };
        HciSendCmd(fd, kHciOpLeSetScanEnable, p);
    }
    close(fd);

    found.reserve(seen.size());
    for (auto& [addr, dev] : seen) found.push_back(std::move(dev));
#endif  // __linux__

    return found;
}

// ──────────────────────────────────────────────────────────────────────────────
// SyncToSource — attach as BLE Audio sink via BT_ISO socket (Linux 5.15+)
// ──────────────────────────────────────────────────────────────────────────────
bool AuraCastSession::SyncToSource(const AuraCastDevice& source,
                                    SinkCallback          state_cb,
                                    AudioFrameCallback    audio_cb) {
    (void)source;
    Detach();  // Clean up any previous session.

    state_cb_  = std::move(state_cb);
    audio_cb_  = std::move(audio_cb);

#if defined(__linux__)
    // Create a BT_ISO socket.
    // BTPROTO_ISO = 13 was added in Linux 5.15 for LE isochronous channels.
    const int fd = socket(kAfBluetooth, kSockSeqpacket, kBtprotoIso);
    if (fd < 0) {
        state_.store(AuraCastState::kError);
        if (state_cb_) state_cb_(AuraCastState::kError,
                                  "BT_ISO socket unavailable (kernel < 5.15?)");
        return false;
    }

    // sockaddr_iso layout (from linux/bluetooth/iso.h):
    //   sa_family_t   iso_family;       // AF_BLUETOOTH
    //   bdaddr_t      iso_bdaddr;       // source BD_ADDR
    //   uint8_t       iso_bdaddr_type;  // 0=public, 1=random
    //   uint8_t       iso_sid;          // advertising SID (0xFF = any)
    struct {
        uint16_t family;
        uint8_t  bdaddr[6];
        uint8_t  bdaddr_type;
        uint8_t  sid;
    } __attribute__((packed)) sa_iso{};
    sa_iso.family      = static_cast<uint16_t>(kAfBluetooth);
    std::memcpy(sa_iso.bdaddr, source.bd_addr.data(), 6);
    sa_iso.bdaddr_type = source.bd_addr_type;
    sa_iso.sid         = 0xFF;  // accept any SID

    state_.store(AuraCastState::kSyncing);
    if (state_cb_) state_cb_(AuraCastState::kSyncing, {});

    if (connect(fd, reinterpret_cast<sockaddr*>(&sa_iso),
                static_cast<socklen_t>(sizeof(sa_iso))) < 0) {
        close(fd);
        const std::string err = std::string("BIG Create Sync failed: ") +
                                strerror(errno);
        state_.store(AuraCastState::kError);
        if (state_cb_) state_cb_(AuraCastState::kError, err);
        return false;
    }

    iso_fd_ = fd;
    state_.store(AuraCastState::kStreaming);
    if (state_cb_) state_cb_(AuraCastState::kStreaming, {});

    // Start the receive thread using the project's IOThreadPool.
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue([this]() {
        constexpr size_t kBufSize = 4096;
        uint8_t buf[kBufSize];
        while (iso_fd_ >= 0 &&
               state_.load() == AuraCastState::kStreaming) {
            const ssize_t n = read(iso_fd_, buf, kBufSize);
            if (n <= 0) break;
            if (audio_cb_)
                audio_cb_(0 /* BIS index */, buf, static_cast<size_t>(n));
        }
        state_.store(AuraCastState::kIdle);
        if (state_cb_) state_cb_(AuraCastState::kIdle, {});
    });

    return true;
#else
    state_.store(AuraCastState::kError);
    if (state_cb_) state_cb_(AuraCastState::kError, "BLE ISO not supported on this platform");
    return false;
#endif
}

// ──────────────────────────────────────────────────────────────────────────────

void AuraCastSession::Detach() {
#if defined(__linux__)
    if (iso_fd_ >= 0) {
        state_.store(AuraCastState::kDisconnected);
        const int fd = iso_fd_;
        iso_fd_ = -1;
        close(fd);  // triggers read() → EBADF → rx_thread exits
    }
#endif
    if (audio_sink_) audio_sink_->Stop();
    state_.store(AuraCastState::kIdle);
}

// ──────────────────────────────────────────────────────────────────────────────
// SyncWithAudio — high-level API: BLE sync + full audio pipeline
// ──────────────────────────────────────────────────────────────────────────────
bool AuraCastSession::SyncWithAudio(
    const AuraCastDevice& source,
    SinkCallback          state_cb,
    XER::Audio::Core::AudioEngine* engine) {

    // Build Lc3Params from the discovered source (or use sane defaults).
    Lc3Params lc3;
    lc3.sampling_freq_hz  = source.bis_list.empty()
        ? 48000u : source.bis_list[0].sampling_freq_hz;
    lc3.frame_duration_us = source.bis_list.empty()
        ? 10000u : source.bis_list[0].frame_duration_us;
    lc3.octets_per_frame  = source.bis_list.empty()
        ? 120u : source.bis_list[0].octets_per_frame;
    lc3.channel_count     = source.num_bis > 0
        ? source.num_bis : static_cast<uint8_t>(2);

    // Create and initialize the audio sink.
    auto sink = std::make_unique<AuraAudioSink>(lc3.channel_count);
    if (!sink->Initialize(lc3, static_cast<int>(lc3.sampling_freq_hz)))
        return false;

    if (!sink->AttachEngine(engine)) return false;
    if (!sink->Start()) return false;

    audio_sink_ = std::move(sink);

    // Route BIS frames to the sink.
    return SyncToSource(source, std::move(state_cb),
                        audio_sink_->GetFrameCallback());
}

}  // namespace device::cast::aura
