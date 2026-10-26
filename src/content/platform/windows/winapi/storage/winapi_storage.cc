#include "winapi_storage.h"
#include <windows.h>
#include <fileapi.h>

namespace Engine::Content::Platform::WinAPI::Storage {

// ---------------------------------------------------------------------------
// Drive Enumerator
// ---------------------------------------------------------------------------

std::vector<DriveInfo> DriveEnumerator::EnumerateDrives() {
    std::vector<DriveInfo> drives;
    
    DWORD driveMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (driveMask & (1 << i)) {
            char driveLetter = 'A' + i;
            DriveInfo info = GetDriveInfo(driveLetter);
            if (info.totalBytes > 0) {
                drives.push_back(info);
            }
        }
    }
    
    return drives;
}

DriveInfo DriveEnumerator::GetDriveInfo(char driveLetter) {
    DriveInfo info;
    info.letter = driveLetter;
    
    char drivePath[4] = {driveLetter, ':', '\\', '\0'};
    
    // Get drive type
    UINT driveType = GetDriveTypeA(drivePath);
    switch (driveType) {
        case DRIVE_FIXED:
            info.type = "Fixed";
            break;
        case DRIVE_REMOVABLE:
            info.type = "Removable";
            info.isRemovable = true;
            break;
        case DRIVE_CDROM:
            info.type = "CD-ROM";
            info.isRemovable = true;
            break;
        case DRIVE_REMOTE:
            info.type = "Network";
            break;
        case DRIVE_RAMDISK:
            info.type = "RAM Disk";
            break;
        default:
            info.type = "Unknown";
            break;
    }
    
    // Get volume information
    char volumeName[MAX_PATH + 1];
    char fileSystem[MAX_PATH + 1];
    DWORD serialNumber, maxComponentLength, fileSystemFlags;
    
    if (GetVolumeInformationA(drivePath, volumeName, MAX_PATH,
                              &serialNumber, &maxComponentLength,
                              &fileSystemFlags, fileSystem, MAX_PATH)) {
        info.volumeName = volumeName;
    }
    
    // Get free space
    ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExA(drivePath, &freeBytesAvailable, 
                           &totalBytes, &totalFreeBytes)) {
        info.totalBytes = totalBytes.QuadPart;
        info.freeBytes = freeBytesAvailable.QuadPart;
    }
    
    return info;
}

bool DriveEnumerator::IsDriveReady(char driveLetter) {
    char drivePath[4] = {driveLetter, ':', '\\', '\0'};
    UINT driveType = GetDriveTypeA(drivePath);
    return driveType != DRIVE_UNKNOWN && driveType != DRIVE_NO_ROOT_DIR;
}

// ---------------------------------------------------------------------------
// File Helper
// ---------------------------------------------------------------------------

bool FileHelper::Exists(const std::string& path) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool FileHelper::IsDirectory(const std::string& path) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileHelper::IsFile(const std::string& path) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t FileHelper::GetFileSize(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (::GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        LARGE_INTEGER size;
        size.HighPart = fileInfo.nFileSizeHigh;
        size.LowPart = fileInfo.nFileSizeLow;
        return size.QuadPart;
    }
    return 0;
}

std::string FileHelper::GetFileAttributes(const std::string& path) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return "";
    }
    
    std::string result;
    if (attrs & FILE_ATTRIBUTE_READONLY) result += "R";
    if (attrs & FILE_ATTRIBUTE_HIDDEN) result += "H";
    if (attrs & FILE_ATTRIBUTE_SYSTEM) result += "S";
    if (attrs & FILE_ATTRIBUTE_ARCHIVE) result += "A";
    if (attrs & FILE_ATTRIBUTE_COMPRESSED) result += "C";
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED) result += "E";
    
    return result;
}

bool FileHelper::SetReadOnly(const std::string& path, bool readOnly) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    
    if (readOnly) {
        attrs |= FILE_ATTRIBUTE_READONLY;
    } else {
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    }
    
    return ::SetFileAttributesA(path.c_str(), attrs) != 0;
}

bool FileHelper::SetHidden(const std::string& path, bool hidden) {
    DWORD attrs = ::GetFileAttributesA(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    
    if (hidden) {
        attrs |= FILE_ATTRIBUTE_HIDDEN;
    } else {
        attrs &= ~FILE_ATTRIBUTE_HIDDEN;
    }
    
    return ::SetFileAttributesA(path.c_str(), attrs) != 0;
}

bool FileHelper::Delete(const std::string& path) {
    return ::DeleteFileA(path.c_str()) != 0;
}

bool FileHelper::Move(const std::string& from, const std::string& to) {
    return ::MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool FileHelper::Copy(const std::string& from, const std::string& to) {
    return ::CopyFileA(from.c_str(), to.c_str(), FALSE) != 0;
}

// ---------------------------------------------------------------------------
// Directory Helper
// ---------------------------------------------------------------------------

bool DirectoryHelper::Create(const std::string& path) {
    return CreateDirectoryA(path.c_str(), NULL) != 0;
}

bool DirectoryHelper::Remove(const std::string& path) {
    return RemoveDirectoryA(path.c_str()) != 0;
}

bool DirectoryHelper::Exists(const std::string& path) {
    return FileHelper::IsDirectory(path);
}

std::vector<std::string> DirectoryHelper::GetFiles(const std::string& path) {
    std::vector<std::string> files;
    
    std::string searchPath = path;
    if (searchPath.back() != '\\') {
        searchPath += '\\';
    }
    searchPath += "*";
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    return files;
}

std::vector<std::string> DirectoryHelper::GetDirectories(const std::string& path) {
    std::vector<std::string> dirs;
    
    std::string searchPath = path;
    if (searchPath.back() != '\\') {
        searchPath += '\\';
    }
    searchPath += "*";
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                strcmp(findData.cFileName, ".") != 0 &&
                strcmp(findData.cFileName, "..") != 0) {
                dirs.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    return dirs;
}

std::vector<std::string> DirectoryHelper::GetAllEntries(const std::string& path) {
    std::vector<std::string> entries;
    
    std::string searchPath = path;
    if (searchPath.back() != '\\') {
        searchPath += '\\';
    }
    searchPath += "*";
    
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(findData.cFileName, ".") != 0 &&
                strcmp(findData.cFileName, "..") != 0) {
                entries.push_back(findData.cFileName);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    
    return entries;
}

std::string DirectoryHelper::GetCurrentDirectory() {
    char buffer[MAX_PATH];
    DWORD len = ::GetCurrentDirectoryA(MAX_PATH, buffer);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buffer);
    }
    return "";
}

bool DirectoryHelper::SetCurrentDirectory(const std::string& path) {
    return ::SetCurrentDirectoryA(path.c_str()) != 0;
}

std::string DirectoryHelper::GetTempPath() {
    char buffer[MAX_PATH + 1];
    DWORD len = ::GetTempPathA(MAX_PATH + 1, buffer);
    if (len > 0 && len <= MAX_PATH) {
        return std::string(buffer);
    }
    return "C:\\Temp";
}

// ---------------------------------------------------------------------------
// Volume Helper
// ---------------------------------------------------------------------------

std::string VolumeHelper::GetVolumeLabel(char driveLetter) {
    char drivePath[4] = {driveLetter, ':', '\\', '\0'};
    char volumeName[MAX_PATH + 1];
    char fileSystem[MAX_PATH + 1];
    DWORD serialNumber, maxComponentLength, fileSystemFlags;
    
    if (GetVolumeInformationA(drivePath, volumeName, MAX_PATH,
                              &serialNumber, &maxComponentLength,
                              &fileSystemFlags, fileSystem, MAX_PATH)) {
        return std::string(volumeName);
    }
    
    return "";
}

bool VolumeHelper::SetVolumeLabel(char driveLetter, const std::string& label) {
    char drivePath[4] = {driveLetter, ':', '\\', '\0'};
    return ::SetVolumeLabelA(drivePath, label.c_str()) != 0;
}

bool VolumeHelper::FormatDrive(char driveLetter, const std::string& fileSystem,
                               bool quickFormat) {
    // Note: This requires elevated privileges and is a destructive operation
    // Simplified placeholder - actual implementation would use SHFormatDrive or FormatEx
    return false;
}

bool VolumeHelper::MountVolume(char driveLetter, const std::string& mountPoint) {
    // Note: Simplified placeholder
    return false;
}

bool VolumeHelper::DismountVolume(char driveLetter) {
    // Note: Simplified placeholder
    return false;
}

// ---------------------------------------------------------------------------
// Structured Storage
// ---------------------------------------------------------------------------

void* StructuredStorage::storage_ = nullptr;

bool StructuredStorage::CreateStorage(const std::string& filepath) {
    // Note: This requires COM initialization and IStorage interface
    // Simplified placeholder
    return false;
}

bool StructuredStorage::OpenStorage(const std::string& filepath) {
    // Note: This requires COM initialization and IStorage interface
    // Simplified placeholder
    return false;
}

void StructuredStorage::CloseStorage() {
    if (storage_) {
        // Release COM object
        storage_ = nullptr;
    }
}

bool StructuredStorage::CreateStream(const std::string& streamName) {
    // Note: Simplified placeholder
    return false;
}

bool StructuredStorage::WriteToStream(const std::string& streamName,
                                     const std::vector<uint8_t>& data) {
    // Note: Simplified placeholder
    return false;
}

std::vector<uint8_t> StructuredStorage::ReadFromStream(const std::string& streamName) {
    // Note: Simplified placeholder
    return std::vector<uint8_t>();
}

}  // namespace Engine::Content::Platform::WinAPI::Storage
