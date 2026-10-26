#include "winapi_audio.h"
#include <mmsystem.h>
#include <windows.h>

#pragma comment(lib, "winmm.lib")

namespace Engine::Content::Platform::WinAPI::Audio {

// ---------------------------------------------------------------------------
// Audio Device Enumerator
// ---------------------------------------------------------------------------

std::vector<AudioDeviceEnumerator::AudioDevice> 
AudioDeviceEnumerator::EnumerateOutputDevices() {
    std::vector<AudioDevice> devices;
    
    int numDevices = waveOutGetNumDevs();
    for (int i = 0; i < numDevices; ++i) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            AudioDevice device;
            device.id = std::to_string(i);
            device.name = caps.szPname;
            device.isDefault = (i == 0);  // First device is typically default
            devices.push_back(device);
        }
    }
    
    return devices;
}

std::vector<AudioDeviceEnumerator::AudioDevice> 
AudioDeviceEnumerator::EnumerateInputDevices() {
    std::vector<AudioDevice> devices;
    
    int numDevices = waveInGetNumDevs();
    for (int i = 0; i < numDevices; ++i) {
        WAVEINCAPSA caps;
        if (waveInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            AudioDevice device;
            device.id = std::to_string(i);
            device.name = caps.szPname;
            device.isDefault = (i == 0);  // First device is typically default
            devices.push_back(device);
        }
    }
    
    return devices;
}

AudioDeviceEnumerator::AudioDevice 
AudioDeviceEnumerator::GetDefaultOutputDevice() {
    auto devices = EnumerateOutputDevices();
    for (const auto& device : devices) {
        if (device.isDefault) {
            return device;
        }
    }
    return devices.empty() ? AudioDevice{} : devices[0];
}

AudioDeviceEnumerator::AudioDevice 
AudioDeviceEnumerator::GetDefaultInputDevice() {
    auto devices = EnumerateInputDevices();
    for (const auto& device : devices) {
        if (device.isDefault) {
            return device;
        }
    }
    return devices.empty() ? AudioDevice{} : devices[0];
}

// ---------------------------------------------------------------------------
// WaveOut Device
// ---------------------------------------------------------------------------

WaveOutDevice::WaveOutDevice() : hWaveOut_(NULL) {
    ZeroMemory(&waveFormat_, sizeof(waveFormat_));
}

WaveOutDevice::~WaveOutDevice() {
    Close();
}

bool WaveOutDevice::Open(int deviceID) {
    if (hWaveOut_) {
        return true;  // Already open
    }
    
    // Default format: 44.1kHz, 16-bit, stereo
    waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat_.nChannels = 2;
    waveFormat_.nSamplesPerSec = 44100;
    waveFormat_.wBitsPerSample = 16;
    waveFormat_.nBlockAlign = waveFormat_.nChannels * waveFormat_.wBitsPerSample / 8;
    waveFormat_.nAvgBytesPerSec = waveFormat_.nSamplesPerSec * waveFormat_.nBlockAlign;
    waveFormat_.cbSize = 0;
    
    MMRESULT result = waveOutOpen(&hWaveOut_, deviceID, &waveFormat_, 
                                  0, 0, CALLBACK_NULL);
    
    return result == MMSYSERR_NOERROR;
}

void WaveOutDevice::Close() {
    if (hWaveOut_) {
        waveOutClose(hWaveOut_);
        hWaveOut_ = NULL;
    }
}

bool WaveOutDevice::SetFormat(int sampleRate, int bitsPerSample, int channels) {
    if (hWaveOut_) {
        Close();  // Must close to change format
    }
    
    waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat_.nChannels = channels;
    waveFormat_.nSamplesPerSec = sampleRate;
    waveFormat_.wBitsPerSample = bitsPerSample;
    waveFormat_.nBlockAlign = channels * bitsPerSample / 8;
    waveFormat_.nAvgBytesPerSec = sampleRate * waveFormat_.nBlockAlign;
    waveFormat_.cbSize = 0;
    
    return Open(WAVE_MAPPER);
}

bool WaveOutDevice::Play(const std::vector<uint8_t>& data) {
    if (!hWaveOut_ || data.empty()) {
        return false;
    }
    
    WAVEHDR waveHeader;
    ZeroMemory(&waveHeader, sizeof(waveHeader));
    waveHeader.lpData = (LPSTR)data.data();
    waveHeader.dwBufferLength = data.size();
    waveHeader.dwFlags = 0;
    
    MMRESULT result = waveOutPrepareHeader(hWaveOut_, &waveHeader, sizeof(waveHeader));
    if (result != MMSYSERR_NOERROR) {
        return false;
    }
    
    result = waveOutWrite(hWaveOut_, &waveHeader, sizeof(waveHeader));
    if (result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(hWaveOut_, &waveHeader, sizeof(waveHeader));
        return false;
    }
    
    // Wait for playback to complete
    while (!(waveHeader.dwFlags & WHDR_DONE)) {
        Sleep(10);
    }
    
    waveOutUnprepareHeader(hWaveOut_, &waveHeader, sizeof(waveHeader));
    return true;
}

void WaveOutDevice::Stop() {
    if (hWaveOut_) {
        waveOutReset(hWaveOut_);
    }
}

void WaveOutDevice::Pause() {
    if (hWaveOut_) {
        waveOutPause(hWaveOut_);
    }
}

void WaveOutDevice::Resume() {
    if (hWaveOut_) {
        waveOutRestart(hWaveOut_);
    }
}

// ---------------------------------------------------------------------------
// WaveIn Device
// ---------------------------------------------------------------------------

WaveInDevice::WaveInDevice() : hWaveIn_(NULL) {
    ZeroMemory(&waveFormat_, sizeof(waveFormat_));
}

WaveInDevice::~WaveInDevice() {
    Close();
}

bool WaveInDevice::Open(int deviceID) {
    if (hWaveIn_) {
        return true;  // Already open
    }
    
    // Default format: 44.1kHz, 16-bit, stereo
    waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat_.nChannels = 2;
    waveFormat_.nSamplesPerSec = 44100;
    waveFormat_.wBitsPerSample = 16;
    waveFormat_.nBlockAlign = waveFormat_.nChannels * waveFormat_.wBitsPerSample / 8;
    waveFormat_.nAvgBytesPerSec = waveFormat_.nSamplesPerSec * waveFormat_.nBlockAlign;
    waveFormat_.cbSize = 0;
    
    MMRESULT result = waveInOpen(&hWaveIn_, deviceID, &waveFormat_, 
                                 0, 0, CALLBACK_NULL);
    
    return result == MMSYSERR_NOERROR;
}

void WaveInDevice::Close() {
    if (hWaveIn_) {
        waveInClose(hWaveIn_);
        hWaveIn_ = NULL;
    }
    recordedData_.clear();
}

bool WaveInDevice::SetFormat(int sampleRate, int bitsPerSample, int channels) {
    if (hWaveIn_) {
        Close();  // Must close to change format
    }
    
    waveFormat_.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat_.nChannels = channels;
    waveFormat_.nSamplesPerSec = sampleRate;
    waveFormat_.wBitsPerSample = bitsPerSample;
    waveFormat_.nBlockAlign = channels * bitsPerSample / 8;
    waveFormat_.nAvgBytesPerSec = sampleRate * waveFormat_.nBlockAlign;
    waveFormat_.cbSize = 0;
    
    return Open(WAVE_MAPPER);
}

bool WaveInDevice::StartRecording() {
    if (!hWaveIn_) {
        return false;
    }
    
    recordedData_.clear();
    MMRESULT result = waveInStart(hWaveIn_);
    return result == MMSYSERR_NOERROR;
}

void WaveInDevice::StopRecording() {
    if (hWaveIn_) {
        waveInStop(hWaveIn_);
    }
}

std::vector<uint8_t> WaveInDevice::GetRecordedData() {
    // Note: This is a simplified implementation
    // Full implementation would use buffers and callbacks
    return recordedData_;
}

// ---------------------------------------------------------------------------
// Multimedia Helper
// ---------------------------------------------------------------------------

bool MultimediaHelper::PlaySound(const std::string& filepath) {
    return ::PlaySoundA(filepath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
}

bool MultimediaHelper::PlaySystemSound(const std::string& soundName) {
    if (soundName == "SystemExclamation") {
        return ::PlaySoundA("SystemExclamation", NULL, SND_ALIAS | SND_ASYNC);
    } else if (soundName == "SystemAsterisk") {
        return ::PlaySoundA("SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC);
    } else if (soundName == "SystemQuestion") {
        return ::PlaySoundA("SystemQuestion", NULL, SND_ALIAS | SND_ASYNC);
    } else if (soundName == "SystemHand") {
        return ::PlaySoundA("SystemHand", NULL, SND_ALIAS | SND_ASYNC);
    }
    return false;
}

void MultimediaHelper::StopAllSounds() {
    ::PlaySoundA(NULL, NULL, SND_PURGE);
}

int MultimediaHelper::GetVolume() {
    // Note: This requires more complex implementation with mixer API
    // Simplified version returns 50%
    return 50;
}

void MultimediaHelper::SetVolume(int volume) {
    // Note: This requires more complex implementation with mixer API
    // Clamp volume to 0-100 range
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    // TODO: Implement actual volume control via mixer API
}

}  // namespace Engine::Content::Platform::WinAPI::Audio
