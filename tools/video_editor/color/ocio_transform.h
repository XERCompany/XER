// Display color management via OpenColorIO. The compositor produces
// Rec.709-ish BGRA; the monitor transform maps that through OCIO into
// the display space (sRGB by default). Config comes from
// assets/color.yaml — source colorspace, display, view transform —
// plus an optional external .ocio config path.
//
// Degrades gracefully: without OpenColorIO (or a failing config) every
// entry point no-ops and frames pass through unchanged.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace XER::VideoEditor::Color {

// assets/color.yaml:
//   Color:
//     OcioConfig: ""            # path to .ocio ("" = OCIO builtin)
//     Source: "Linear Rec.709 (sRGB)"
//     Display: "sRGB"
//     View: "Standard"          # display view transform
//     Enabled: true
struct OcioConfig {
    std::string config_path;
    std::string source = "Linear Rec.709 (sRGB)";
    std::string display = "sRGB";
    std::string view = "Standard";
    bool enabled = true;
};

bool LoadOcioConfig(const std::filesystem::path& yaml_path,
                    OcioConfig* out, std::string* error);

// In-place BGRA8 transform (B,G,R,A bytes; alpha preserved). Rows may
// be stride-packed — pass stride_bytes == width*4 for tight frames.
// Returns false when OCIO is unavailable/failed — the frame is left
// untouched (identity).
bool ApplyDisplayTransform(std::uint8_t* bgra, int width, int height,
                           int stride_bytes, const OcioConfig& cfg);

// Cheap availability probe — false when the OpenColorIO runtime is
// absent or the given config cannot be created (cached per config).
bool OcioAvailable(const OcioConfig& cfg);

// ---- named profiles (Color tab / ocio:<name> clip effects) -------------
// assets/color.yaml additionally carries a Profiles sequence. Each entry
// is either a LUT look (Lut: path to .cube/.3dl relative to the yaml)
// or a colorspace conversion (Source→Display/View through the config):
//   Color:
//     Profiles:
//       - Name: "Teal Orange"
//         Lut: "luts/teal_orange.cube"
//       - Name: "Log to Rec709"
//         Source: "ACEScc"      # optional; default = OcioConfig.source
//         Display: "sRGB"       # optional; default = OcioConfig.display
//         View: "Standard"      # optional; default = OcioConfig.view
struct OcioProfile {
    std::string name;
    std::string lut_path;   // "" = colorspace path, not LUT
    std::string source;     // "" = inherit cfg.source
    std::string display;    // "" = inherit cfg.display
    std::string view;       // "" = inherit cfg.view
};

// Parses the optional Profiles: sequence of `yaml_path`. Missing/absent
// section is NOT an error — returns true with an empty list. Only fails
// on missing file or malformed yaml.
bool LoadOcioProfiles(const std::filesystem::path& yaml_path,
                      std::vector<OcioProfile>* out, std::string* error);

// Process-wide profile table, filled by Boot() from color.yaml and
// appended by the LUT manager's scans. Raw access is only safe on the
// single-threaded boot/UI paths — writers mutating AFTER engines may
// be running must go through MutateOcioProfiles(), and readers on
// engine threads must use FindOcioProfileCopy() (both lock the same
// internal mutex; a raw pointer taken from this vector could dangle
// across a concurrent rescan).
std::vector<OcioProfile>& SharedOcioProfiles();

// Finds a profile by name (case-insensitive). nullptr when unknown.
// Single-threaded callers only (boot/tests/UI) — see the note above.
const OcioProfile* FindOcioProfile(const std::string& name);

// Runs `mutate` under the profile-table mutex — the write path for
// dynamic registrations (the LUT manager's rescan erases its previous
// entries and appends the fresh set here).
void MutateOcioProfiles(
    const std::function<void(std::vector<OcioProfile>&)>& mutate);

// Thread-safe copy-out find for engine threads (the compositor): the
// matching profile is copied out under the lock, so a concurrent
// MutateOcioProfiles can never invalidate the returned data.
// Returns false when the name is unknown (`out` untouched).
bool FindOcioProfileCopy(const std::string& name, OcioProfile* out);

// Applies a named profile in place on BGRA8 — LUT via OCIO
// FileTransform, or colorspace via ColorSpaceTransform/DisplayView.
// Relative lut_path resolves against `assets_dir`. Returns false (frame
// untouched) when OCIO is absent, the profile is unknown, or the
// transform fails to build (cached per profile+config).
bool ApplyProfileTransform(std::uint8_t* bgra, int width, int height,
                           int stride_bytes, const OcioProfile& profile,
                           const OcioConfig& cfg,
                           const std::filesystem::path& assets_dir);

}  // namespace XER::VideoEditor::Color
