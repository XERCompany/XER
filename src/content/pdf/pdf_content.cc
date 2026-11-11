/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/pdf/pdf_internal.h"

namespace XER::Content::Pdf {
namespace {

// Runs `page` through a structured-text device and hands the
// resulting fz_stext_page back to `f` inside the same Guard scope.
// Caller owns nothing; the stext page is dropped afterwards.
template <typename F>
bool WithTextPage(fz_context* ctx, fz_document* doc, int page_index,
                  F&& f, std::string* error) {
    return Internal::Guard(ctx, [&] {
        Internal::ScopedPage page(ctx, doc, page_index);
        fz_stext_page* stext = nullptr;
        fz_device* dev = nullptr;
        fz_var(ctx); fz_var(stext); fz_var(dev);
        fz_try(ctx) {
            stext = fz_new_stext_page(ctx, fz_bound_page(ctx, page.get()));
            dev = fz_new_stext_device(ctx, stext, nullptr);
            fz_run_page_contents(ctx, page.get(), dev, fz_identity, nullptr);
            fz_close_device(ctx, dev);
            f(stext);
        }
        fz_always(ctx) {
            if (dev) fz_drop_device(ctx, dev);
            if (stext) fz_drop_stext_page(ctx, stext);
        }
        fz_catch(ctx) { fz_rethrow(ctx); }
    }, error);
}

void AppendLineText(std::string* out, const fz_stext_line* line) {
    char utf8[8];
    for (const fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
        const int n = fz_runetochar(utf8, ch->c);
        out->append(utf8, (size_t)n);
    }
    out->push_back('\n');
}

}  // namespace

bool PdfDocument::ExtractText(int page_index, std::string* text,
                              std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc || !text) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    text->clear();
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    return WithTextPage(impl_->ctx, impl_->doc, page_index,
                        [&](const fz_stext_page* stext) {
        for (const fz_stext_block* b = stext->first_block; b; b = b->next) {
            if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (const fz_stext_line* l = b->u.t.first_line; l; l = l->next) {
                AppendLineText(text, l);
            }
            text->push_back('\n');
        }
    }, error);
}

bool PdfDocument::ExtractTextSpans(int page_index,
                                   std::vector<PdfSpan>* spans,
                                   std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc || !spans) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    spans->clear();
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    return WithTextPage(impl_->ctx, impl_->doc, page_index,
                        [&](const fz_stext_page* stext) {
        for (const fz_stext_block* b = stext->first_block; b; b = b->next) {
            if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
            for (const fz_stext_line* l = b->u.t.first_line; l; l = l->next) {
                PdfSpan span;
                // Group consecutive chars on the line into one span;
                // bounding box = union of char quads.
                std::string line_text;
                fz_rect bb = fz_empty_rect;
                float max_size = 0.0f;
                for (const fz_stext_char* ch = l->first_char; ch;
                     ch = ch->next) {
                    char utf8[8];
                    const int n = fz_runetochar(utf8, ch->c);
                    line_text.append(utf8, (size_t)n);
                    bb = fz_union_rect(bb, fz_rect_from_quad(ch->quad));
                    if (ch->size > max_size) max_size = ch->size;
                }
                if (line_text.empty()) continue;
                span.x = bb.x0;
                span.y = bb.y0;
                span.width = bb.x1 - bb.x0;
                span.height = bb.y1 - bb.y0;
                span.font_size = max_size;
                span.text = std::move(line_text);
                spans->push_back(std::move(span));
            }
        }
    }, error);
}

bool PdfDocument::GetLinks(int page_index, std::vector<PdfLinkInfo>* links,
                           std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc || !links) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    links->clear();
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    return Internal::Guard(impl_->ctx, [&] {
        Internal::ScopedPage page(impl_->ctx, impl_->doc, page_index);
        fz_link* list = nullptr;
        fz_var(impl_->ctx); fz_var(list);
        fz_try(impl_->ctx) {
            list = fz_load_links(impl_->ctx, page.get());
            for (fz_link* l = list; l; l = l->next) {
                PdfLinkInfo li;
                li.page = page_index;
                li.x = l->rect.x0;
                li.y = l->rect.y0;
                li.w = l->rect.x1 - l->rect.x0;
                li.h = l->rect.y1 - l->rect.y0;
                if (l->uri) {
                    // Internal page destinations resolve to a location.
                    li.uri = l->uri;
                    const fz_location loc = fz_resolve_link(
                        impl_->ctx, impl_->doc, l->uri, nullptr, nullptr);
                    if (loc.page >= 0) li.target_page = loc.page;
                }
                links->push_back(std::move(li));
            }
        }
        fz_always(impl_->ctx) { fz_drop_link(impl_->ctx, list); }
        fz_catch(impl_->ctx) { fz_rethrow(impl_->ctx); }
    }, error);
}

bool PdfDocument::GetAnnotations(int page_index,
                                 std::vector<PdfAnnotInfo>* annots,
                                 std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf || !annots) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    annots->clear();
    const int count = Internal::CountPages(impl_->ctx, impl_->doc);
    if (page_index < 0 || page_index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return false;
    }
    fz_context* ctx = impl_->ctx;
    return Internal::Guard(ctx, [&] {
        Internal::ScopedPage page(ctx, impl_->doc, page_index);
        pdf_page* pp = page.pdf();
        for (pdf_annot* a = pdf_first_annot(ctx, pp); a;
             a = pdf_next_annot(ctx, a)) {
            PdfAnnotInfo ai;
            ai.page = page_index;
            const enum pdf_annot_type t = pdf_annot_type(ctx, a);
            if (const char* s = pdf_string_from_annot_type(ctx, t))
                ai.type = s;
            if (const char* s = pdf_annot_contents(ctx, a))
                ai.contents = s;
            if (const char* s = pdf_annot_author(ctx, a))
                ai.author = s;
            const fz_rect r = pdf_annot_rect(ctx, a);
            ai.x = r.x0; ai.y = r.y0;
            ai.w = r.x1 - r.x0; ai.h = r.y1 - r.y0;
            annots->push_back(std::move(ai));
        }
    }, error);
}

}  // namespace XER::Content::Pdf
