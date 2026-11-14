/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

// Internal plumbing shared by the pdf module implementation files.
// Not part of the public API.

#include "content/pdf/pdf_document.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#if defined(_MSC_VER)
// MuPDF's fz_try/fz_catch are setjmp/longjmp by design; C++ objects are
// only PODs at these call sites (see Guard/CountPages below).
#pragma warning(push)
#pragma warning(disable: 4611)
#endif

namespace XER::Content::Pdf {

struct PdfDocument::Impl {
    fz_context* ctx = nullptr;
    fz_document* doc = nullptr;     // always the fz_document*
    pdf_document* pdf = nullptr;    // same object, typed for pdf ops
    std::string path;

    ~Impl() {
        if (doc) fz_drop_document(ctx, doc);
        if (ctx) fz_drop_context(ctx);
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

namespace Internal {

// Runs `f` inside a MuPDF try/catch block. Returns true on success;
// on MuPDF exception returns false and fills `error` (when non-null)
// with fz_caught_message.
template <typename F>
bool Guard(fz_context* ctx, F&& f, std::string* error) {
    bool ok = false;
    fz_var(ok);
    fz_try(ctx) {
        f();
        ok = true;
    }
    fz_catch(ctx) {
        if (error) *error = fz_caught_message(ctx);
    }
    return ok;
}

// RAII for a loaded page within a Guard scope.
class ScopedPage {
public:
    ScopedPage(fz_context* ctx, fz_document* doc, int index)
        : ctx_(ctx), page_(fz_load_page(ctx, doc, index)) {}
    ~ScopedPage() { if (page_) fz_drop_page(ctx_, page_); }

    fz_page* get() const { return page_; }
    pdf_page* pdf() const { return reinterpret_cast<pdf_page*>(page_); }
    explicit operator bool() const { return page_ != nullptr; }

private:
    fz_context* ctx_;
    fz_page* page_;
};

inline void SetError(std::string* error, std::string_view msg) {
    if (error) error->assign(msg);
}

// fz_count_pages wrapped in try/catch. Returns -1 on MuPDF error.
// Use for bounds checks: never let an fz_ call longjmp past a held
// std::mutex or lock_guard.
inline int CountPages(fz_context* ctx, fz_document* doc) {
    int n = -1;
    fz_var(ctx); fz_var(doc); fz_var(n);
    fz_try(ctx) { n = fz_count_pages(ctx, doc); }
    fz_catch(ctx) { n = -1; }
    return n;
}

}  // namespace Internal
}  // namespace XER::Content::Pdf

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
