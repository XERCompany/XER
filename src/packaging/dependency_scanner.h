/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include "packaging/package_spec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace XER::Packaging {

// In-process port of the tools/pe_deps walk: BFS over the PE import +
// delay-import tables of `exe`, resolving each DLL name first in the
// exe's own directory, then in each of `search_dirs` in order. Every
// resolved DLL is rescanned transitively. Names resolving nowhere are
// recorded in `unresolved` (OS-provided or genuinely missing).
//
// `search_dirs` may be empty — then only the exe dir is searched.
// Returns false only when `exe` itself cannot be read/parsed.
bool ScanDependencies(const std::filesystem::path& exe,
                      const std::vector<std::filesystem::path>& search_dirs,
                      DependencyScanResult* out, std::string* error);

}  // namespace XER::Packaging
