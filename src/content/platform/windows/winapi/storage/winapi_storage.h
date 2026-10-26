#pragma once

#include "../winapi_common.h"
#include <string>
#include <vector>
#include <filesystem>

namespace Engine::Content::Platform::WinAPI::Storage {

// Drive information
struct DriveInfo {
    char letter;
    std::string type;
    std::string volumeName;
    uint64_t totalBytes;
    uint64_t freeBytes;
    bool isRemovable;
};

// Drive enumeration
class DriveEnumerator {
public:
    static std::vector<DriveInfo> EnumerateDrives();
    static DriveInfo GetDriveInfo(char driveLetter);
    static bool IsDriveReady(char driveLetter);
};

// File operations helper
class FileHelper {
public:
    static bool Exists(const std::string& path);
    static bool IsDirectory(const std::string& path);
    static bool IsFile(const std::string& path);
    
    static uint64_t GetFileSize(const std::string& path);
    static std::string GetFileAttributes(const std::string& path);
    
    static bool SetReadOnly(const std::string& path, bool readOnly);
    static bool SetHidden(const std::string& path, bool hidden);
    
    static bool Delete(const std::string& path);
    static bool Move(const std::string& from, const std::string& to);
    static bool Copy(const std::string& from, const std::string& to);
};

// Directory operations
class DirectoryHelper {
public:
    static bool Create(const std::string& path);
    static bool Remove(const std::string& path);
    static bool Exists(const std::string& path);
    
    static std::vector<std::string> GetFiles(const std::string& path);
    static std::vector<std::string> GetDirectories(const std::string& path);
    static std::vector<std::string> GetAllEntries(const std::string& path);
    
    static std::string GetCurrentDirectory();
    static bool SetCurrentDirectory(const std::string& path);
    
    static std::string GetTempPath();
};

// Volume operations
class VolumeHelper {
public:
    static std::string GetVolumeLabel(char driveLetter);
    static bool SetVolumeLabel(char driveLetter, const std::string& label);
    
    static bool FormatDrive(char driveLetter, const std::string& fileSystem = "NTFS",
                           bool quickFormat = true);
    
    static bool MountVolume(char driveLetter, const std::string& mountPoint);
    static bool DismountVolume(char driveLetter);
};

// Structured storage (COM-based, simplified wrapper)
class StructuredStorage {
public:
    static bool CreateStorage(const std::string& filepath);
    static bool OpenStorage(const std::string& filepath);
    static void CloseStorage();
    
    static bool CreateStream(const std::string& streamName);
    static bool WriteToStream(const std::string& streamName, 
                            const std::vector<uint8_t>& data);
    static std::vector<uint8_t> ReadFromStream(const std::string& streamName);
    
private:
    static void* storage_;  // IStorage*
};

}  // namespace Engine::Content::Platform::WinAPI::Storage
