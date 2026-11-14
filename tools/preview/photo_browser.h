#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "image/core/image_buffer.h"

namespace preview {

/// Folder-scanning image browser + non-destructive edit pipeline.
///
/// Model: `base_` holds the image after geometric edits (rotate/flip/crop);
/// `display_` is `base_` plus brightness/contrast adjustments and is what the
/// viewer renders and `Save()` writes (WYSIWYG).
class PhotoBrowser {
public:
    PhotoBrowser() = default;

    /// Canonicalizes `path`, scans its parent directory for supported image
    /// files and positions the cursor on `path`. Returns false with
    /// `status_message_` set when the file cannot be opened.
    bool Open(const std::string& path);

    bool IsOpen() const { return !files_.empty(); }
    const std::vector<std::string>& Files() const { return files_; }
    std::size_t CurrentIndex() const { return current_; }
    const std::string& CurrentPath() const;
    bool HasCurrent() const { return current_ < files_.size(); }

    /// Moves the cursor by `delta` (clamped). Does not decode.
    bool GoTo(int delta);
    bool GoToIndex(std::size_t index);

    /// Decodes the current file into the edit buffers. Safe to call on every
    /// navigation; returns false and sets `status_message_` on failure.
    bool LoadCurrent();

    bool IsLoaded() const { return base_.IsValid(); }
    const image::ImageBuffer& DisplayImage() const { return display_; }
    int ImageWidth() const { return base_.Width(); }
    int ImageHeight() const { return base_.Height(); }

    /// Monotonic counter bumped on every pixel change; viewers use it to
    /// rebuild GPU/texture uploads only when needed.
    std::uint64_t Revision() const { return revision_; }
    bool IsDirty() const { return dirty_; }

    const std::string& StatusMessage() const { return status_message_; }
    void ClearStatus() { status_message_.clear(); }

    // --- Geometric edits (mutate base_, rebuild display_) ---
    bool RotateCW();
    bool RotateCCW();
    bool FlipHorizontal();
    bool FlipVertical();
    bool Crop(int x, int y, int w, int h);

    // --- Color edits (re-applied to base_ on every change) ---
    void SetBrightness(float value);   // -100..100
    void SetContrast(float value);     // 0..200 (percent)
    float Brightness() const { return brightness_; }
    float Contrast() const { return contrast_; }
    void ResetColor();                 // brightness=0, contrast=100

    /// Drops all edits and reloads the current file from disk.
    bool Revert();

    /// Writes display_ back to the current file. Returns false on failure.
    bool Save();
    /// Writes display_ as PNG next to the source (`<stem>.png`, or
    /// `<stem>_<n>.png` when that would overwrite the source itself).
    bool ExportPng(std::string* out_path = nullptr);

private:
    void RebuildDisplay();
    void MarkEdited();
    bool RequireLoaded(const char* op) const;

    std::vector<std::string> files_;
    std::size_t current_ = 0;
    image::ImageBuffer base_;
    image::ImageBuffer display_;
    float brightness_ = 0.0f;
    float contrast_ = 100.0f;
    bool dirty_ = false;
    std::uint64_t revision_ = 0;
    std::string status_message_;
};

/// True when `path` has an extension this browser can decode.
bool IsSupportedImageExtension(const std::string& path);

/// Converts any 8-bit packed/gray ImageBuffer to RGBA8. Returns false for
/// HDR/planar formats the viewer cannot display.
bool NormalizeToRGBA8(const image::ImageBuffer& src, image::ImageBuffer& dst);

}  // namespace preview
