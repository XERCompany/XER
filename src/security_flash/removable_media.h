#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "security_flash/security_flash.h"

namespace XER::SecurityFlash::Detail {

// Normalizes "E", "E:", "E:\\", "e:\\dir" into "E:\\".
// Returns false when the input is not a drive specification.
bool NormalizeDriveRoot(std::string_view input, std::string* root_out);

// Queries drive info for a normalized root ("E:\\"). Never fails on
// missing media — ready=false is reported instead.
bool QueryDriveInfo(const std::string& root,
		    DriveInfo* info_out,
		    std::string* error_out);

// All mounted fixed + removable local drives.
std::vector<DriveInfo> EnumerateLocalDrives();

// Resolves the volume serial of the volume containing `path`
// (any file or directory on the volume). Used for token binding.
bool VolumeSerialForPath(const std::string& path,
			 std::uint32_t* serial_out,
			 std::string* error_out);

}  // namespace XER::SecurityFlash::Detail
