#pragma once

#include "../winapi_common.h"
#include <string>
#include <vector>

namespace Engine::Content::Platform::WinAPI::Audio {

// Audio device enumeration
class AudioDeviceEnumerator {
public:
    struct AudioDevice {
        std::string id;
        std::string name;
        bool isDefault;
    };
    
    static std::vector<AudioDevice> EnumerateOutputDevices();
    static std::vector<AudioDevice> EnumerateInputDevices();
    static AudioDevice GetDefaultOutputDevice();
    static AudioDevice GetDefaultInputDevice();
};

// Basic WaveOut wrapper (simplified)
class WaveOutDevice {
public:
    WaveOutDevice();
    ~WaveOutDevice();
    
    bool Open(int deviceID = WAVE_MAPPER);
    void Close();
    
    bool IsOpen() const { return hWaveOut_ != NULL; }
    
    // Format configuration
    bool SetFormat(int sampleRate, int bitsPerSample, int channels);
    
    // Playback
    bool Play(const std::vector<uint8_t>& data);
    void Stop();
    void Pause();
    void Resume();
    
private:
    HWAVEOUT hWaveOut_;
    WAVEFORMATEX waveFormat_;
};

// Basic WaveIn wrapper (simplified)
class WaveInDevice {
public:
    WaveInDevice();
    ~WaveInDevice();
    
    bool Open(int deviceID = WAVE_MAPPER);
    void Close();
    
    bool IsOpen() const { return hWaveIn_ != NULL; }
    
    // Format configuration
    bool SetFormat(int sampleRate, int bitsPerSample, int channels);
    
    // Recording
    bool StartRecording();
    void StopRecording();
    std::vector<uint8_t> GetRecordedData();
    
private:
    HWAVEIN hWaveIn_;
    WAVEFORMATEX waveFormat_;
    std::vector<uint8_t> recordedData_;
};

// Multimedia helper functions
class MultimediaHelper {
public:
    static bool PlaySound(const std::string& filepath);
    static bool PlaySystemSound(const std::string& soundName);
    static void StopAllSounds();
    
    static int GetVolume();
    static void SetVolume(int volume);  // 0-100
};

}  // namespace Engine::Content::Platform::WinAPI::Audio
