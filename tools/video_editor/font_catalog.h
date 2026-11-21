// FontCatalog — the system's installed font families, enumerated once
// through SkFontMgr and cached. Drives the title editor's font picker
// and the Font Manager dialog. Header stays Skia-free (pure STL) so
// UI callers don't need the Skia include paths; the .cc compiles the
// enumeration only when the build links Skia (XER_VE_HAS_SKIA) and
// reports Available() == false otherwise.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::VideoEditor {

struct FontStyleInfo {
    int weight = 400;        // DirectWrite range 100..900
    bool italic = false;
    std::string name;        // "Regular", "Bold Italic", ...
};

struct FontFamilyInfo {
    std::string name;
    std::vector<FontStyleInfo> styles;
};

class FontCatalog {
public:
    // False when the core was built without Skia — enumeration and
    // preview then return empty/no-op results.
    static bool Available();

    // Every installed family with its styles, sorted by name. Cached —
    // the first call pays the enumeration cost.
    static const std::vector<FontFamilyInfo>& Families();
    static std::vector<std::string> FamilyNames();
    static int FamilyCount() {
        return static_cast<int>(Families().size());
    }

    // Styles of one family (empty vector when unknown).
    static const std::vector<FontStyleInfo>& StylesOf(
        const std::string& family);

    // Renders one preview line of `utf8` in (family, weight, italic)
    // into packed BGRA8 STRAIGHT-alpha pixels sized to the text.
    // White glyphs on transparent — the caller tints/composites.
    // Returns false when Skia is unavailable or rendering fails.
    static bool RenderPreview(const std::string& family, int weight,
                              bool italic, const std::string& utf8,
                              float size_px,
                              std::vector<std::uint8_t>* bgra,
                              int* width, int* height);
};

}  // namespace XER::VideoEditor
