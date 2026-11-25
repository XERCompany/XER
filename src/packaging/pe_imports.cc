/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "packaging/pe_imports.h"

#include <cstdint>
#include <cstring>
#include <fstream>

namespace XER::Packaging {

namespace {

// Bounds-checked little-endian reads over the loaded image.
class PeView {
public:
    explicit PeView(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

    std::size_t Size() const { return data_.size(); }

    bool U16(std::size_t off, std::uint16_t* out) const {
        if (off + 2 > data_.size()) return false;
        *out = static_cast<std::uint16_t>(data_[off]) |
               static_cast<std::uint16_t>(data_[off + 1] << 8);
        return true;
    }
    bool U32(std::size_t off, std::uint32_t* out) const {
        if (off + 4 > data_.size()) return false;
        *out = static_cast<std::uint32_t>(data_[off]) |
               (static_cast<std::uint32_t>(data_[off + 1]) << 8) |
               (static_cast<std::uint32_t>(data_[off + 2]) << 16) |
               (static_cast<std::uint32_t>(data_[off + 3]) << 24);
        return true;
    }
    bool U64(std::size_t off, std::uint64_t* out) const {
        std::uint32_t lo = 0, hi = 0;
        if (!U32(off, &lo) || !U32(off + 4, &hi)) return false;
        *out = static_cast<std::uint64_t>(lo) |
               (static_cast<std::uint64_t>(hi) << 32);
        return true;
    }
    // NUL-terminated string at file offset `off`, truncated at `max_len`.
    bool CString(std::size_t off, std::size_t max_len, std::string* out) const {
        if (off >= data_.size()) return false;
        const std::size_t end =
            (off + max_len <= data_.size()) ? off + max_len : data_.size();
        std::size_t nul = off;
        while (nul < end && data_[nul] != 0) ++nul;
        if (nul == end) return false;  // unterminated inside the image
        out->assign(reinterpret_cast<const char*>(data_.data() + off),
                    nul - off);
        return true;
    }

private:
    std::vector<std::uint8_t> data_;
};

struct Section {
    std::uint32_t va = 0;
    std::uint32_t raw_size = 0;
    std::uint32_t raw_ptr = 0;
    std::uint32_t virtual_size = 0;
};

// RVA -> file offset. Image data can extend past SizeOfRawData on
// disk-padded sections, so the span check uses max(VirtualSize, RawSize)
// like the loader does.
bool RvaToOffset(const std::vector<Section>& sections, std::uint32_t rva,
                 std::size_t* out) {
    for (const Section& s : sections) {
        const std::uint32_t span =
            s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size;
        if (rva >= s.va && rva < s.va + span) {
            *out = static_cast<std::size_t>(s.raw_ptr) + (rva - s.va);
            return true;
        }
    }
    return false;
}

constexpr std::size_t kMaxDllName = 260;
// Sanity caps: a malformed table must not spin forever.
constexpr int kMaxDescriptors = 4096;

}  // namespace

bool ReadPeImports(const std::filesystem::path& pe_path, PeImportInfo* out,
                   std::string* error, std::vector<std::string>* warnings) {
    if (out == nullptr) {
        if (error != nullptr) *error = "out parameter is null";
        return false;
    }
    out->imports.clear();
    out->delay_imports.clear();

    std::ifstream in(pe_path, std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot open " + pe_path.string();
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    const PeView pe(std::move(bytes));

    std::uint16_t mz = 0;
    if (!pe.U16(0, &mz) || mz != 0x5A4D /* "MZ" */) {
        if (error != nullptr)
            *error = pe_path.string() + ": not a PE image (no MZ)";
        return false;
    }
    std::uint32_t lfanew = 0;
    if (!pe.U32(0x3C, &lfanew) ||
        lfanew + 4 + 20 > pe.Size()) {
        if (error != nullptr)
            *error = pe_path.string() + ": truncated DOS/PE header";
        return false;
    }
    std::uint32_t sig = 0;
    if (!pe.U32(lfanew, &sig) || sig != 0x00004550 /* "PE\0\0" */) {
        if (error != nullptr)
            *error = pe_path.string() + ": missing PE signature";
        return false;
    }

    const std::size_t coff = lfanew + 4;
    std::uint16_t num_sections = 0, opt_size = 0;
    if (!pe.U16(coff + 2, &num_sections) ||
        !pe.U16(coff + 16, &opt_size)) {
        if (error != nullptr)
            *error = pe_path.string() + ": truncated COFF header";
        return false;
    }
    const std::size_t opt = coff + 20;
    if (opt + opt_size > pe.Size() || opt_size < 96) {
        if (error != nullptr)
            *error = pe_path.string() + ": truncated optional header";
        return false;
    }

    std::uint16_t magic = 0;
    pe.U16(opt, &magic);
    const bool pe32plus = (magic == 0x20B);
    if (magic != 0x10B && !pe32plus) {
        if (error != nullptr)
            *error = pe_path.string() + ": unknown optional-header magic";
        return false;
    }
    // NumberOfRvaAndSizes sits at offset 92 (PE32) / 108 (PE32+);
    // directory[0] starts at 96 / 112.
    const std::size_t dd_count_off = opt + (pe32plus ? 108 : 92);
    const std::size_t dd_off = opt + (pe32plus ? 112 : 96);
    std::uint32_t num_dirs = 0;
    pe.U32(dd_count_off, &num_dirs);
    if (num_dirs > 16) num_dirs = 16;
    // Clamp to what physically fits inside the optional header.
    const std::uint32_t fits =
        static_cast<std::uint32_t>((opt + opt_size - dd_off) / 8);
    if (num_dirs > fits) num_dirs = fits;

    std::uint64_t image_base = 0;
    if (pe32plus) {
        pe.U64(opt + 24, &image_base);
    } else {
        std::uint32_t b = 0;
        pe.U32(opt + 28, &b);
        image_base = b;
    }

    // Section headers follow the optional header.
    const std::size_t sec_off = opt + opt_size;
    std::vector<Section> sections;
    sections.reserve(num_sections);
    for (std::uint16_t i = 0; i < num_sections; ++i) {
        const std::size_t s = sec_off + static_cast<std::size_t>(i) * 40;
        if (s + 40 > pe.Size()) {
            if (warnings != nullptr)
                warnings->push_back(pe_path.string() +
                                    ": truncated section table");
            break;
        }
        Section sec;
        pe.U32(s + 8, &sec.virtual_size);
        pe.U32(s + 12, &sec.va);
        pe.U32(s + 16, &sec.raw_size);
        pe.U32(s + 20, &sec.raw_ptr);
        sections.push_back(sec);
    }

    auto dir_rva = [&](std::uint32_t index, std::uint32_t* rva,
                       std::uint32_t* size) -> bool {
        if (index >= num_dirs) return false;
        return pe.U32(dd_off + index * 8, rva) &&
               pe.U32(dd_off + index * 8 + 4, size);
    };

    // ---- Import directory (index 1): 20-byte descriptors, zero-
    // terminated. Name RVA at +12. ----
    {
        std::uint32_t rva = 0, sz = 0;
        if (dir_rva(1, &rva, &sz) && rva != 0) {
            std::size_t off = 0;
            if (!RvaToOffset(sections, rva, &off)) {
                if (warnings != nullptr)
                    warnings->push_back(
                        pe_path.string() + ": import directory RVA "
                        "maps outside the image");
            } else {
                for (int i = 0; i < kMaxDescriptors; ++i, off += 20) {
                    std::uint32_t ilt = 0, name_rva = 0, iat = 0;
                    if (!pe.U32(off, &ilt) || !pe.U32(off + 12, &name_rva) ||
                        !pe.U32(off + 16, &iat)) {
                        if (warnings != nullptr)
                            warnings->push_back(
                                pe_path.string() + ": truncated import "
                                "descriptor table");
                        break;
                    }
                    if (ilt == 0 && name_rva == 0 && iat == 0) break;
                    std::size_t name_off = 0;
                    std::string name;
                    if (RvaToOffset(sections, name_rva, &name_off) &&
                        pe.CString(name_off, kMaxDllName, &name)) {
                        out->imports.push_back(std::move(name));
                    } else if (warnings != nullptr) {
                        warnings->push_back(
                            pe_path.string() + ": unreadable import name");
                    }
                }
            }
        }
    }

    // ---- Delay-import directory (index 13): 32-byte descriptors.
    // DllNameRVA at +4 is an RVA only when Attributes bit0 is set;
    // pre-VS2015 images store absolute VAs — subtract ImageBase. ----
    {
        std::uint32_t rva = 0, sz = 0;
        if (dir_rva(13, &rva, &sz) && rva != 0) {
            std::size_t off = 0;
            if (!RvaToOffset(sections, rva, &off)) {
                if (warnings != nullptr)
                    warnings->push_back(
                        pe_path.string() + ": delay-import directory "
                        "RVA maps outside the image");
            } else {
                for (int i = 0; i < kMaxDescriptors; ++i, off += 32) {
                    std::uint32_t attrs = 0, name_va = 0;
                    if (!pe.U32(off, &attrs) || !pe.U32(off + 4, &name_va)) {
                        if (warnings != nullptr)
                            warnings->push_back(
                                pe_path.string() + ": truncated "
                                "delay-import table");
                        break;
                    }
                    if (attrs == 0 && name_va == 0) break;
                    std::uint64_t name_rva64 = name_va;
                    if ((attrs & 1u) == 0) {
                        if (name_va < image_base) continue;  // bogus VA
                        name_rva64 = name_va - image_base;
                    }
                    if (name_rva64 == 0 ||
                        name_rva64 > 0xFFFFFFFFull) {
                        break;
                    }
                    std::size_t name_off = 0;
                    std::string name;
                    if (RvaToOffset(sections,
                                    static_cast<std::uint32_t>(name_rva64),
                                    &name_off) &&
                        pe.CString(name_off, kMaxDllName, &name)) {
                        out->delay_imports.push_back(std::move(name));
                    } else if (warnings != nullptr) {
                        warnings->push_back(
                            pe_path.string() +
                            ": unreadable delay-import name");
                    }
                }
            }
        }
    }

    return true;
}

}  // namespace XER::Packaging
