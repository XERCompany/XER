/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::Packaging::Ui {

// Writes a BGRA8 buffer (top-down, 4 bytes/px, no padding) as an 8-bit
// RGBA PNG — zlib only, same minimal encoder pattern as
// tools/SdGenerate/sd_png_writer.cc. Used by `xer_packager screenshot`
// to dump the SoftwareRenderer frame.
bool WriteBgraPng(const std::uint8_t* bgra, int width, int height,
                  const std::string& utf8_path, std::string* error);

}  // namespace XER::Packaging::Ui
