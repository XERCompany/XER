/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * AuraCastSession — Bluetooth LE Audio Auracast sink (receiver) session.
 *
 * Protocol stack (from Wikipedia / Bluetooth SIG specs):
 *
 *   ┌──────────────────────────────────────────────────────────────────┐
 *   │  Application (XER)                                               │
 *   ├──────────────────────────────────────────────────────────────────┤
 *   │  Public Broadcast Profile (PBP 1.0)  ← service UUID 0x1856      │
 *   │  Basic Audio Profile (BAP 1.0.1)     ← BIG Create Sync          │
 *   ├──────────────────────────────────────────────────────────────────┤
 *   │  Bluetooth LE 5.2+  (2.4 GHz, 40 × 2 MHz channels)             │
 *   │  HCI raw socket  — AF_BLUETOOTH / BTPROTO_HCI (Linux kernel 3.4+│
 *   │  BT_ISO socket   — BTPROTO_ISO            (Linux kernel 5.15+)  │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * Discovery flow:
 *   1. Open HCI raw socket, configure LE passive scan.
 *   2. Collect ADV_IND / ADV_NONCONN_IND events.
 *   3. Parse AD structures; look for:
 *        AD type 0x16 (Service Data, UUID16) with UUID = 0x1856 → Auracast source.
 *        AD type 0x09 (Complete Local Name) → broadcast name.
 *        AD type 0x16 data[2..4] → 3-byte Broadcast_ID.
 *   4. Return list of discovered AuraCastDevice.
 *
 * Sync flow (receive audio):
 *   1. Create a BT_ISO socket (BTPROTO_ISO = 13).
 *   2. setsockopt BT_ISO_QOS with broadcaster parameters.
 *   3. connect() to the source BD_ADDR → kernel performs PA sync → BIG Create Sync.
 *   4. When connected, read() produces interleaved LC3 audio frames.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "content/device/cast/aura/aura_cast_device.h"
#include "content/device/cast/aura/aura_audio_sink.h"
#include "async_io/io_thread_pool.h"

namespace XER::Audio::Core { class AudioEngine; }

namespace device::cast::aura {

using SinkCallback = std::function<void(AuraCastState state,
                                        const std::string& error)>;
// Called for each received LC3 audio frame payload.
using AudioFrameCallback = std::function<void(uint8_t bis_index,
                                               const uint8_t* data,
                                               size_t len)>;

// AuraCastSession acts as a **Bluetooth LE Audio Auracast sink** (receiver).
//
// It does NOT use TCP/SSL or mDNS — those belong to the Google Cast protocol.
// Auracast is a BLE broadcast standard with no pairing and no TCP at all.
//
// Audio pipeline:
//   BT_ISO read() → AudioFrameCallback
//       → AuraAudioSink::OnBisFrame()
//           → AuraLc3Decoder (liblc3 dlopen)
//           → AudioSampleRateConverter
//           → AudioInterleaveProcessor
//           → AudioFIFOQueue
//           → AudioEngine::ProcessAudio()
class AuraCastSession {
public:
    static AuraCastSession& Instance();

    // ── Discovery ───────────────────────────────────────────────────────────

    // Perform a BLE passive scan for Auracast sources.
    // Uses a raw HCI socket (BTPROTO_HCI) with LE_SET_SCAN_ENABLE.
    // Looks for AD Service Data with UUID 0x1856 (Public Broadcast Announcement).
    // Blocks up to |timeout_ms| and returns all unique sources found.
    std::vector<AuraCastDevice> ScanForSources(int timeout_ms = 4000,
                                                int hci_dev   = 0) const;

    // Alias for backwards compat with DeviceManager.
    std::vector<AuraCastDevice> DiscoverDevices(int timeout_ms = 4000) const {
        return ScanForSources(timeout_ms);
    }

    // ── Sync to source (become a BLE Audio sink) ────────────────────────────

    // Low-level: sync and deliver raw LC3 frames to |audio_cb|.
    bool SyncToSource(const AuraCastDevice& source,
                      SinkCallback          state_cb,
                      AudioFrameCallback    audio_cb);

    // High-level: sync to |source|, decode LC3 and feed into AudioEngine.
    // If |engine| is null, an internal engine is created automatically.
    // Use GetAudioSink() to control volume, read stats, etc.
    bool SyncWithAudio(const AuraCastDevice& source,
                       SinkCallback          state_cb,
                       XER::Audio::Core::AudioEngine* engine = nullptr);

    // Detach from the current BIG (sends BIG Terminate Sync to the kernel).
    void Detach();

    // ── Audio sink access ────────────────────────────────────────────────────
    // Valid only after SyncWithAudio(); null otherwise.
    AuraAudioSink* GetAudioSink() { return audio_sink_.get(); }

    // ── State ────────────────────────────────────────────────────────────────
    AuraCastState GetState() const { return state_.load(); }

    // Stubs for DeviceManager compatibility.
    bool Connect(const AuraCastDevice& dev, SinkCallback cb) {
        return SyncToSource(dev, std::move(cb), nullptr);
    }
    void Disconnect() { Detach(); }

private:
    AuraCastSession() = default;

    int iso_fd_ = -1;
    std::atomic<AuraCastState> state_{ AuraCastState::kIdle };
    SinkCallback       state_cb_;
    AudioFrameCallback audio_cb_;
    std::thread        rx_thread_;

    // Integrated audio pipeline (populated by SyncWithAudio).
    std::unique_ptr<AuraAudioSink> audio_sink_;
};

}  // namespace device::cast::aura
