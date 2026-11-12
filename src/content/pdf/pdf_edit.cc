/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/pdf/pdf_internal.h"

namespace XER::Content::Pdf {
namespace {

// Finds the annot_index-th annotation on `page` (0-based).
pdf_annot* FindAnnot(fz_context* ctx, pdf_page* page, int annot_index) {
    pdf_annot* a = pdf_first_annot(ctx, page);
    for (int n = 0; a && n < annot_index; ++n)
        a = pdf_next_annot(ctx, a);
    return a;
}

}  // namespace

bool PdfDocument::DeletePage(int index, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (index < 0 || index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    return Internal::Guard(impl_->ctx, [&] {
        pdf_delete_page(impl_->ctx, impl_->pdf, index);
    }, error);
}

bool PdfDocument::DeletePageRange(int start, int end, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    return Internal::Guard(impl_->ctx, [&] {
        pdf_delete_page_range(impl_->ctx, impl_->pdf, start, end);
    }, error);
}

bool PdfDocument::InsertBlankPage(int at, float width_pt, float height_pt,
                                  std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    fz_context* ctx = impl_->ctx;
    pdf_document* pdf = impl_->pdf;
    const fz_rect mediabox{0, 0, width_pt, height_pt};

    return Internal::Guard(ctx, [&] {
        pdf_obj* resources = nullptr;
        fz_buffer* contents = nullptr;
        fz_device* dev = nullptr;
        pdf_obj* page_obj = nullptr;
        fz_var(resources); fz_var(contents); fz_var(dev);
        fz_var(page_obj);
        fz_try(ctx) {
            // pdf_page_write returns a device that writes drawing
            // operations into the contents buffer; closing it
            // immediately yields a valid empty page.
            dev = pdf_page_write(ctx, pdf, mediabox, &resources,
                                 &contents);
            fz_close_device(ctx, dev);
            page_obj = pdf_add_page(ctx, pdf, mediabox, 0, resources,
                                    contents);
            pdf_insert_page(ctx, pdf, at, page_obj);
        }
        fz_always(ctx) {
            if (dev) fz_drop_device(ctx, dev);
            if (page_obj) pdf_drop_obj(ctx, page_obj);
            if (contents) fz_drop_buffer(ctx, contents);
            if (resources) pdf_drop_obj(ctx, resources);
        }
        fz_catch(ctx) { fz_rethrow(ctx); }
    }, error);
}

bool PdfDocument::CopyPageFrom(PdfDocument& src, int src_index, int at,
                               std::string* error) {
    // Member access works across instances; scoped_lock avoids
    // deadlock when two documents graft in opposite directions.
    std::scoped_lock lock(mutex_, src.mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no destination document");
        return false;
    }
    if (!src.impl_ || !src.impl_->pdf) {
        Internal::SetError(error, "pdf: no source document");
        return false;
    }
    const int count = Internal::CountPages(src.impl_->ctx, src.impl_->doc);
    if (src_index < 0 || src_index >= count) {
        Internal::SetError(error, "pdf: source page index out of range");
        return false;
    }
    return Internal::Guard(impl_->ctx, [&] {
        pdf_graft_page(impl_->ctx, impl_->pdf, at, src.impl_->pdf,
                       src_index);
    }, error);
}

bool PdfDocument::RotatePage(int index, int degrees, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    if (degrees != 0 && degrees != 90 && degrees != 180 &&
        degrees != 270) {
        Internal::SetError(error, "pdf: rotation must be 0/90/180/270");
        return false;
    }
    fz_context* ctx = impl_->ctx;
    return Internal::Guard(ctx, [&] {
        pdf_obj* pobj = pdf_lookup_page_obj(ctx, impl_->pdf, index);
        if (!pobj) fz_throw(ctx, FZ_ERROR_GENERIC, "no such page");
        pdf_dict_put(ctx, pobj, PDF_NAME(Rotate),
                     pdf_new_int(ctx, degrees));
    }, error);
}

bool PdfDocument::AddAnnotation(int page_index, const PdfAnnotSpec& spec,
                                std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    fz_context* ctx = impl_->ctx;
    const enum pdf_annot_type type =
        pdf_annot_type_from_string(ctx, spec.type.c_str());
    if (type == PDF_ANNOT_UNKNOWN) {
        Internal::SetError(error, "pdf: unknown annotation type");
        return false;
    }

    return Internal::Guard(ctx, [&] {
        Internal::ScopedPage page(ctx, impl_->doc, page_index);
        pdf_annot* annot =
            pdf_create_annot(ctx, page.pdf(), type);
        const fz_rect r{spec.x, spec.y, spec.x + spec.w,
                        spec.y + spec.h};
        pdf_set_annot_rect(ctx, annot, r);
        if (!spec.contents.empty())
            pdf_set_annot_contents(ctx, annot, spec.contents.c_str());
        if (!spec.author.empty())
            pdf_set_annot_author(ctx, annot, spec.author.c_str());
        pdf_annot_request_resynthesis(ctx, annot);
    }, error);
}

bool PdfDocument::DeleteAnnotation(int page_index, int annot_index,
                                   std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    fz_context* ctx = impl_->ctx;
    return Internal::Guard(ctx, [&] {
        Internal::ScopedPage page(ctx, impl_->doc, page_index);
        pdf_annot* a = FindAnnot(ctx, page.pdf(), annot_index);
        if (!a) fz_throw(ctx, FZ_ERROR_GENERIC, "no such annotation");
        pdf_delete_annot(ctx, page.pdf(), a);
    }, error);
}

bool PdfDocument::SetAnnotationContents(int page_index, int annot_index,
                                        std::string_view text,
                                        std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    const std::string copy(text);
    fz_context* ctx = impl_->ctx;
    return Internal::Guard(ctx, [&] {
        Internal::ScopedPage page(ctx, impl_->doc, page_index);
        pdf_annot* a = FindAnnot(ctx, page.pdf(), annot_index);
        if (!a) fz_throw(ctx, FZ_ERROR_GENERIC, "no such annotation");
        pdf_set_annot_contents(ctx, a, copy.c_str());
    }, error);
}

}  // namespace XER::Content::Pdf
