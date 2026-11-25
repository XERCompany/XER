/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace XER::Packaging {

// DLL names pulled from one PE/COFF image.
struct PeImportInfo {
    // Data directory 1 (IMAGE_DIRECTORY_ENTRY_IMPORT).
    std::vector<std::string> imports;
    // Data directory 13 (IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT).
    std::vector<std::string> delay_imports;
};

// Minimal PE import-table reader — the in-process equivalent of the
// llvm/Object-based tools/pe_deps scanner (xer_pe_deps.exe) used by
// xer_stage_runtime_dlls(TARGET ...). Kept dependency-free so
// xer_packaging can run inside any host (UI app, CLI, tests) without
// linking LLVM.
//
// Handles PE32 and PE32+, and both delay-load name encodings (RVA and
// the legacy absolute-VA form gated by the Attributes grAttrs bit).
// Returns false and fills `error` when the file is unreadable or is not
// a valid PE image; a truncated/malformed table yields whatever names
// could be read plus a warning appended to `warnings` (may be null).
bool ReadPeImports(const std::filesystem::path& pe_path,
                   PeImportInfo* out, std::string* error,
                   std::vector<std::string>* warnings = nullptr);

}  // namespace XER::Packaging
