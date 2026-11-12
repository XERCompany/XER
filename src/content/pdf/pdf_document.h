/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Content::Pdf {

// High-level document metadata (Info dictionary + file facts).
struct PdfInfo {
    int page_count = 0;
    bool encrypted = false;
    uint64_t file_size = 0;
    std::string pdf_version;   // e.g. "1.7"
    std::string title;
    std::string author;
    std::string subject;
    std::string keywords;
    std::string creator;
    std::string producer;
    std::string creation_date;
    std::string mod_date;
};

// Geometry of a single page. Sizes are in PDF points (1/72 inch),
// origin top-left after rotation is applied.
struct PdfPageInfo {
    int index = 0;
    float width_pt = 0.0f;
    float height_pt = 0.0f;
    int rotation = 0;  // degrees clockwise: 0, 90, 180, 270
};

// One positioned text run extracted from a page.
struct PdfSpan {
    float x = 0.0f;        // origin in points, top-left origin
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float font_size = 0.0f;
    std::string text;
};

// Interactive link rectangle on a page.
struct PdfLinkInfo {
    int page = 0;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string uri;        // external URI, empty for internal links
    int target_page = -1;   // internal destination page, -1 if external/unknown
};

// Annotation summary.
struct PdfAnnotInfo {
    int page = 0;
    std::string type;       // "text", "highlight", "ink", ...
    std::string contents;
    std::string author;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Specification for creating an annotation.
struct PdfAnnotSpec {
    std::string type = "text";  // see pdf_annot_type names
    std::string contents;
    std::string author;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;  // rect, points
};

// Rendered page as an RGBA8 bitmap ("photo"). Row-major, top-down.
struct PdfBitmap {
    int width = 0;
    int height = 0;
    int stride = 0;                    // bytes per row
    std::vector<uint8_t> pixels;       // RGBA, width*height*4
};

// Destination rectangle in client pixels for HWND/HDC rendering.
struct PdfDstRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// RAII wrapper around a MuPDF document.
//
// Supports: reading (info, text, links, annotations), editing
// (metadata, page insert/delete/copy/rotate, annotations), document
// creation and saving, and rendering pages to bitmaps or directly
// into a Windows HDC/HWND.
//
// Thread safety: all public methods are serialized on an internal
// mutex, so calls on the same document may come from any thread.
// Different documents are fully independent (each owns its own
// fz_context).
class PdfDocument {
public:
    PdfDocument() = default;
    ~PdfDocument();

    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    PdfDocument(PdfDocument&&) noexcept;
    PdfDocument& operator=(PdfDocument&&) noexcept;

    // Opens an existing PDF. `password` is the user/owner password
    // for encrypted documents (empty when unencrypted).
    static std::unique_ptr<PdfDocument> Open(const std::string& path,
                                             const std::string& password = {},
                                             std::string* error = nullptr);

    // Creates a new empty PDF document (no pages yet).
    static std::unique_ptr<PdfDocument> Create(std::string* error = nullptr);

    bool IsValid() const;

    // Writes the (possibly modified) document to `path`, replacing it.
    // The in-memory document stays usable afterwards.
    bool Save(const std::string& path, std::string* error = nullptr);

    // Writes an unmodified snapshot of the document to `path`.
    bool SaveCopy(const std::string& path, std::string* error = nullptr);

    PdfInfo GetInfo(std::string* error = nullptr);
    int PageCount() const;
    PdfPageInfo GetPageInfo(int index, std::string* error = nullptr);

    // --- Reading -----------------------------------------------------

    // Plain-text extraction of one page (page indices are 0-based).
    // Whitespace/newlines follow the visual layout order.
    bool ExtractText(int page_index, std::string* text,
                     std::string* error = nullptr);

    // Positioned text spans of one page (for search/highlight UIs).
    bool ExtractTextSpans(int page_index, std::vector<PdfSpan>* spans,
                          std::string* error = nullptr);

    bool GetLinks(int page_index, std::vector<PdfLinkInfo>* links,
                  std::string* error = nullptr);

    bool GetAnnotations(int page_index, std::vector<PdfAnnotInfo>* annots,
                        std::string* error = nullptr);

    // --- Editing -----------------------------------------------------

    // Sets a document metadata key. Accepted keys: "title", "author",
    // "subject", "keywords", "creator", "producer", "creationDate",
    // "modDate" (case-insensitive).
    bool SetMetadata(const std::string& key, const std::string& value,
                     std::string* error = nullptr);

    // Removes page `index` from the page tree. Contents/resources
    // remain in the file until the next garbage-collected save.
    bool DeletePage(int index, std::string* error = nullptr);

    // Removes pages [start, end) (end may be negative -> page count).
    bool DeletePageRange(int start, int end, std::string* error = nullptr);

    // Inserts a blank page of `width_pt` x `height_pt` points at
    // position `at` (0 <= at <= page_count; negative -> append).
    bool InsertBlankPage(int at, float width_pt, float height_pt,
                         std::string* error = nullptr);

    // Copies page `src_index` from `src` into this document at `at`.
    bool CopyPageFrom(PdfDocument& src, int src_index, int at,
                      std::string* error = nullptr);

    // Sets page rotation: degrees must be one of 0, 90, 180, 270.
    bool RotatePage(int index, int degrees, std::string* error = nullptr);

    // Adds an annotation to `page_index`; returns false with `error`
    // set on failure. The annotation appears after Save().
    bool AddAnnotation(int page_index, const PdfAnnotSpec& spec,
                       std::string* error = nullptr);

    // Deletes annotation number `annot_index` (0-based, in the order
    // returned by GetAnnotations) on `page_index`.
    bool DeleteAnnotation(int page_index, int annot_index,
                          std::string* error = nullptr);

    // Replaces the contents (popup text) of an annotation.
    bool SetAnnotationContents(int page_index, int annot_index,
                               std::string_view text,
                               std::string* error = nullptr);

    // --- Rendering ---------------------------------------------------

    // Renders `page_index` to an RGBA8 bitmap at `dpi`
    // (96 dpi ~= on-screen size). Returns an empty PdfBitmap on error.
    PdfBitmap RenderPageToBitmap(int page_index, float dpi = 96.0f,
                                 bool transparent = false,
                                 std::string* error = nullptr);

    // Renders `page_index` into an arbitrary HDC, letterboxed into
    // `dst` (device pixels) preserving aspect ratio. `hdc` is an
    // opaque Windows HDC. Implemented only on Windows; other
    // platforms return false with an error.
    bool RenderPageToHdc(void* hdc, int page_index, const PdfDstRect& dst,
                         float dpi = 96.0f, bool transparent = false,
                         std::string* error = nullptr);

    // Convenience wrapper: renders into the client area of `hwnd`
    // (or into `dst` client coordinates when non-empty).
    bool RenderPageToHwnd(void* hwnd, int page_index,
                          const PdfDstRect& dst = {}, float dpi = 96.0f,
                          bool transparent = false,
                          std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_;
};

}  // namespace XER::Content::Pdf
