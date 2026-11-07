#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace XER::engine::bridge::skia {
    // Begins a new PDF document. Returns an opaque ID or handle.
    int PDFBeginDocument(const std::string& author, const std::string& title);
    // Begins a new page in the given document. Returns a Canvas ID.
    int PDFBeginPage(int doc_id, float width, float height);
    // Ends the current page.
    bool PDFEndPage(int doc_id);
    // Finalizes the document and serializes it to bytes.
    bool PDFEndDocument(int doc_id, std::vector<uint8_t>* out_bytes);
}
