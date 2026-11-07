#include "wrapper/skia/skia_engine_bridge_pdf.h"

#if ENGINE_HAS_SKIA_BRIDGE
#if __has_include("include/docs/SkPDFDocument.h") || __has_include(<include/docs/SkPDFDocument.h>)
#define ENGINE_SKIA_HAS_PDF 1
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkDocument.h"
#include "include/core/SkStream.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkString.h"
#include <unordered_map>
#include <mutex>
#include <memory>
#endif
#endif

namespace XER::engine::bridge::skia {

#if ENGINE_HAS_SKIA_BRIDGE && defined(ENGINE_SKIA_HAS_PDF)

    struct PdfSession {
        SkDynamicMemoryWStream stream;
        sk_sp<SkDocument> document;
        SkCanvas* current_canvas = nullptr;
    };

    static std::unordered_map<int, std::unique_ptr<PdfSession>> g_pdf_sessions;
    static int g_pdf_next_id = 1;
    static std::mutex g_pdf_mutex;

    int PDFBeginDocument(const std::string& author, const std::string& title) {
        std::lock_guard<std::mutex> lock(g_pdf_mutex);
        auto session = std::make_unique<PdfSession>();

        SkPDF::Metadata metadata;
        metadata.fAuthor = author.c_str();
        metadata.fTitle = title.c_str();

        session->document = SkPDF::MakeDocument(&session->stream, metadata);
        if (!session->document) return -1;

        int id = g_pdf_next_id++;
        g_pdf_sessions[id] = std::move(session);
        return id;
    }

    int PDFBeginPage(int doc_id, float width, float height) {
        std::lock_guard<std::mutex> lock(g_pdf_mutex);
        auto it = g_pdf_sessions.find(doc_id);
        if (it == g_pdf_sessions.end()) return -1;
        
        it->second->current_canvas = it->second->document->beginPage(width, height);
        return doc_id; // Temporary: returning doc_id as "canvas ID" for future abstraction
    }

    bool PDFEndPage(int doc_id) {
        std::lock_guard<std::mutex> lock(g_pdf_mutex);
        auto it = g_pdf_sessions.find(doc_id);
        if (it == g_pdf_sessions.end() || !it->second->document) return false;
        
        it->second->document->endPage();
        it->second->current_canvas = nullptr;
        return true;
    }

    bool PDFEndDocument(int doc_id, std::vector<uint8_t>* out_bytes) {
        std::lock_guard<std::mutex> lock(g_pdf_mutex);
        auto it = g_pdf_sessions.find(doc_id);
        if (it == g_pdf_sessions.end() || !it->second->document) return false;
        
        it->second->document->close();
        
        if (out_bytes) {
            sk_sp<SkData> data = it->second->stream.detachAsData();
            if (data && data->size() > 0) {
                out_bytes->assign(data->bytes(), data->bytes() + data->size());
            }
        }
        
        g_pdf_sessions.erase(it);
        return true;
    }

#else

    int PDFBeginDocument(const std::string& author, const std::string& title) { return -1; }
    int PDFBeginPage(int doc_id, float width, float height) { return -1; }
    bool PDFEndPage(int doc_id) { return false; }
    bool PDFEndDocument(int doc_id, std::vector<uint8_t>* out_bytes) { return false; }

#endif

}
