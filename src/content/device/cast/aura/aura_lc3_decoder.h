/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * LC3 (Low Complexity Communication Codec) decoder for Auracast BLE frames.
 *
 * LC3 is the mandatory codec for Bluetooth LE Audio (Bluetooth SIG assigned
 * codec_id = 0x06). It produces PCM float samples from isochronous BLE frames
 * received via BT_ISO sockets.
 *
 * Each BIS frame contains exactly one LC3 frame with fixed parameters
 * negotiated during BIG Create Sync:
 *   - Sampling frequency: 8/16/24/32/44.1/48 kHz
 *   - Frame duration:     7500 µs or 10000 µs
 *   - Octets per codec frame: 26–400
 *
 * On Linux, liblc3 (https://github.com/google/liblc3) is available via
 * apt install liblc3-dev. We use dlopen to avoid a hard build dependency.
 *
 * Fallback (no liblc3): raw LC3 bytes are stored verbatim in the output
 * buffer so the caller can pass them on to an external decoder.
 *
 * Output feeds into XER::Audio::Core::AudioFIFOQueue and the project
 * AudioEngine pipeline (audio_engine.h, audio_buffer_manager.h).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "content/device/cast/aura/aura_cast_device.h"

namespace XER::Audio::Core { class AudioBuffer; struct AudioFrameInfo; }

namespace device::cast::aura {

// Codec parameters derived from the BIG BASE announcement.
struct Lc3Params {
    uint32_t sampling_freq_hz   = 48000;
    uint16_t frame_duration_us  = 10000;  // 7500 or 10000 µs
    uint8_t  channel_count      = 2;      // stereo (2 BIS)
    uint16_t octets_per_frame   = 120;    // per channel per frame
};

// Decoded PCM output: planar float32, one vector per channel.
struct Lc3DecodedFrame {
    uint32_t sample_rate  = 48000;
    uint8_t  channels     = 2;
    size_t   frame_count  = 0;        // PCM frames (samples per channel)
    std::vector<std::vector<float>> planar;  // planar[ch][sample_index]
};

// AuraLc3Decoder decodes a single BIS LC3 frame payload into PCM.
//
// Usage:
//   AuraLc3Decoder dec;
//   dec.Initialize(params);
//   // In AudioFrameCallback:
//   Lc3DecodedFrame out = dec.Decode(bis_index, data, len);
//   // Push out.planar[0/1] into AudioFIFOQueue.
class AuraLc3Decoder {
public:
    AuraLc3Decoder();
    ~AuraLc3Decoder();

    // Set up decoder for given Auracast stream parameters.
    // Tries to dlopen("liblc3.so.1"); falls back to passthrough mode.
    bool Initialize(const Lc3Params& params);
    bool IsInitialized() const { return initialized_; }
    bool HasHardwareDecoder() const { return lc3_lib_ != nullptr; }
    const Lc3Params& GetParams() const { return params_; }

    // Decode one LC3 payload (|len| == params.octets_per_frame).
    // |bis_index| selects the channel (0=left, 1=right for stereo).
    // Returns decoded PCM; on decode error returns silence (not empty).
    Lc3DecodedFrame Decode(uint8_t bis_index, const uint8_t* data, size_t len);

    // Convenience: decode a stereo frame where left and right are supplied
    // as two separate BIS payloads of equal length.
    Lc3DecodedFrame DecodeStereo(const uint8_t* left, const uint8_t* right,
                                  size_t frame_bytes);

    void Reset();

private:
    bool     initialized_  = false;
    Lc3Params params_;
    void*    lc3_lib_      = nullptr;  // dlopen handle

    // liblc3 function pointers (loaded dynamically).
    // Signatures mirror google/liblc3 public API.
    using PfnLc3FrameSamples  = int(*)(int dt_us, int sr_hz);
    using PfnLc3DecoderSize   = unsigned(*)(int dt_us, int sr_hz);
    using PfnLc3SetupDecoder  = void*(*)(int dt_us, int sr_hz, int sr_pcm_hz,
                                          void* mem);
    using PfnLc3Decode        = int(*)(void* decoder, const void* in, int nbytes,
                                        int fmt, void* pcm, int frame_samples);

    PfnLc3FrameSamples  pfn_lc3_frame_samples_  = nullptr;
    PfnLc3DecoderSize   pfn_lc3_decoder_size_    = nullptr;
    PfnLc3SetupDecoder  pfn_lc3_setup_decoder_   = nullptr;
    PfnLc3Decode        pfn_lc3_decode_          = nullptr;

    // Per-channel decoder state (allocated from liblc3).
    std::vector<std::vector<uint8_t>> decoder_mem_;  // raw memory for each channel
    std::vector<void*>                decoders_;      // pointers into decoder_mem_

    int pcm_samples_per_frame_ = 0;  // = lc3_frame_samples(dt_us, sr_hz)

    bool LoadLc3Library();
    Lc3DecodedFrame SilenceFrame(uint8_t channels) const;
};

}  // namespace device::cast::aura
