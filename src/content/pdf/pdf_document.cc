/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/pdf/pdf_internal.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace XER::Content::Pdf {
namespace {

// Creates and wires a fresh MuPDF context. Never throws;
// returns null on out-of-memory.
fz_context* NewContext() {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (ctx) fz_register_document_handlers(ctx);
    return ctx;
}

// Writes `meta` lookup helper: fills `dst` with the metadata string,
// or leaves it empty.
void LookupMeta(fz_context* ctx, fz_document* doc, const char* key,
                std::string* dst) {
    char buf[512];
    const int n = fz_lookup_metadata(ctx, doc, key, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[std::min(n, (int)sizeof(buf) - 1)] = 0;
        dst->assign(buf);
    }
}

// Maps a friendly metadata key to the canonical "info:X" name.
std::string MetaKey(std::string_view key) {
    static const std::pair<std::string_view, const char*> kMap[] = {
        {"title", FZ_META_INFO_TITLE},
        {"author", FZ_META_INFO_AUTHOR},
        {"subject", FZ_META_INFO_SUBJECT},
        {"keywords", FZ_META_INFO_KEYWORDS},
        {"creator", FZ_META_INFO_CREATOR},
        {"producer", FZ_META_INFO_PRODUCER},
        {"creationdate", FZ_META_INFO_CREATIONDATE},
        {"moddate", FZ_META_INFO_MODIFICATIONDATE},
        {"creation_date", FZ_META_INFO_CREATIONDATE},
        {"mod_date", FZ_META_INFO_MODIFICATIONDATE},
    };
    std::string low(key);
    for (auto& c : low) c = (char)tolower((unsigned char)c);
    for (auto& [k, v] : kMap) {
        if (low == k) return v;
    }
    return std::string(key);
}

}  // namespace

PdfDocument::~PdfDocument() = default;

PdfDocument::PdfDocument(PdfDocument&& other) noexcept {
    std::lock_guard lock(other.mutex_);
    impl_ = std::move(other.impl_);
}

PdfDocument& PdfDocument::operator=(PdfDocument&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mutex_, other.mutex_);
        impl_ = std::move(other.impl_);
    }
    return *this;
}

std::unique_ptr<PdfDocument> PdfDocument::Open(const std::string& path,
                                               const std::string& password,
                                               std::string* error) {
    auto out = std::make_unique<PdfDocument>();
    out->impl_ = std::make_unique<Impl>();
    Impl& i = *out->impl_;

    i.ctx = NewContext();
    if (!i.ctx) {
        Internal::SetError(error, "pdf: cannot allocate MuPDF context");
        return nullptr;
    }

    const bool ok = Internal::Guard(i.ctx, [&] {
        i.doc = fz_open_document(i.ctx, path.c_str());
        i.pdf = pdf_document_from_fz_document(i.ctx, i.doc);
        if (!i.pdf) {
            fz_throw(i.ctx, FZ_ERROR_GENERIC, "not a PDF document: %s",
                     path.c_str());
        }
        if (fz_needs_password(i.ctx, i.doc)) {
            const int auth = fz_authenticate_password(i.ctx, i.doc,
                                                      password.c_str());
            if (auth <= 0) {
                fz_throw(i.ctx, FZ_ERROR_GENERIC,
                         "bad or missing PDF password");
            }
        }
    }, error);
    if (!ok) return nullptr;

    i.path = path;
    return out;
}

std::unique_ptr<PdfDocument> PdfDocument::Create(std::string* error) {
    auto out = std::make_unique<PdfDocument>();
    out->impl_ = std::make_unique<Impl>();
    Impl& i = *out->impl_;

    i.ctx = NewContext();
    if (!i.ctx) {
        Internal::SetError(error, "pdf: cannot allocate MuPDF context");
        return nullptr;
    }

    const bool ok = Internal::Guard(i.ctx, [&] {
        i.pdf = pdf_create_document(i.ctx);
        i.doc = reinterpret_cast<fz_document*>(i.pdf);
    }, error);
    if (!ok) return nullptr;
    return out;
}

bool PdfDocument::IsValid() const {
    std::lock_guard lock(mutex_);
    return impl_ && impl_->doc;
}

bool PdfDocument::Save(const std::string& path, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    Impl& i = *impl_;

    // MuPDF streams read page contents lazily from the source file, so
    // overwriting the open file in place is unsafe: write to a sibling
    // temp file, then atomically replace.
    std::filesystem::path dst = path;
    std::filesystem::path final_path = dst;
    const bool in_place =
        !i.path.empty() &&
        std::filesystem::weakly_canonical(dst) ==
            std::filesystem::weakly_canonical(std::filesystem::path(i.path));
    if (in_place) dst = path + ".xertmp";

    bool ok = Internal::Guard(i.ctx, [&] {
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        opts.do_garbage = 3;  // renumber + dedup: drops deleted content
        pdf_save_document(i.ctx, i.pdf, dst.string().c_str(), &opts);
    }, error);

    if (!ok) {
        if (in_place) {
            std::error_code ec;
            std::filesystem::remove(dst, ec);
        }
        return false;
    }
    if (in_place) {
        std::error_code ec;
        std::filesystem::rename(dst, final_path, ec);
        if (ec) {
            Internal::SetError(error, "pdf: rename failed: " + ec.message());
            std::filesystem::remove(dst, ec);
            return false;
        }
        i.path = path;
    }
    return true;
}

bool PdfDocument::SaveCopy(const std::string& path, std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->pdf) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    return Internal::Guard(impl_->ctx, [&] {
        pdf_save_snapshot(impl_->ctx, impl_->pdf, path.c_str());
    }, error);
}

PdfInfo PdfDocument::GetInfo(std::string* error) {
    std::lock_guard lock(mutex_);
    PdfInfo info;
    if (!impl_ || !impl_->doc) {
        Internal::SetError(error, "pdf: no document");
        return info;
    }
    Impl& i = *impl_;
    fz_context* ctx = i.ctx;
    fz_document* doc = i.doc;

    info.page_count = Internal::CountPages(ctx, doc);
    if (i.pdf) {
        info.encrypted = (i.pdf->crypt != nullptr);
        if (i.pdf->version > 0) {
            info.pdf_version = std::to_string(i.pdf->version / 10) + "." +
                               std::to_string(i.pdf->version % 10);
        }
    }
    if (!i.path.empty()) {
        std::error_code ec;
        info.file_size = (uint64_t)std::filesystem::file_size(i.path, ec);
    }

    auto meta = [&](const char* key) {
        std::string v;
        fz_var(ctx); fz_var(doc); fz_var(v);
        fz_try(ctx) { LookupMeta(ctx, doc, key, &v); }
        fz_catch(ctx) { fz_report_error(ctx); }
        return v;
    };
    info.title = meta(FZ_META_INFO_TITLE);
    info.author = meta(FZ_META_INFO_AUTHOR);
    info.subject = meta(FZ_META_INFO_SUBJECT);
    info.keywords = meta(FZ_META_INFO_KEYWORDS);
    info.creator = meta(FZ_META_INFO_CREATOR);
    info.producer = meta(FZ_META_INFO_PRODUCER);
    info.creation_date = meta(FZ_META_INFO_CREATIONDATE);
    info.mod_date = meta(FZ_META_INFO_MODIFICATIONDATE);
    return info;
}

int PdfDocument::PageCount() const {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc) return 0;
    const int n = Internal::CountPages(impl_->ctx, impl_->doc);
    return n < 0 ? 0 : n;
}

PdfPageInfo PdfDocument::GetPageInfo(int index, std::string* error) {
    std::lock_guard lock(mutex_);
    PdfPageInfo info;
    info.index = index;
    if (!impl_ || !impl_->doc) {
        Internal::SetError(error, "pdf: no document");
        return info;
    }
    fz_context* ctx = impl_->ctx;
    fz_document* doc = impl_->doc;
    const int count = Internal::CountPages(ctx, doc);
    if (index < 0 || index >= count) {
        Internal::SetError(error, "pdf: page index out of range");
        return info;
    }

    fz_page* page = nullptr;
    fz_var(ctx); fz_var(doc); fz_var(page); fz_var(info);
    fz_try(ctx) {
        page = fz_load_page(ctx, doc, index);
        const fz_rect r = fz_bound_page(ctx, page);
        info.width_pt = r.x1 - r.x0;
        info.height_pt = r.y1 - r.y0;
        // Rotation is stored in the page dict.
        if (impl_->pdf) {
            pdf_obj* pobj = pdf_lookup_page_obj(ctx, impl_->pdf, index);
            if (pobj) {
                info.rotation = pdf_dict_get_int(
                    ctx, pobj, PDF_NAME(Rotate));
            }
        }
    }
    fz_catch(ctx) {
        if (error) *error = fz_caught_message(ctx);
        fz_report_error(ctx);
    }
    if (page) fz_drop_page(ctx, page);
    return info;
}

bool PdfDocument::SetMetadata(const std::string& key,
                              const std::string& value,
                              std::string* error) {
    std::lock_guard lock(mutex_);
    if (!impl_ || !impl_->doc) {
        Internal::SetError(error, "pdf: no document");
        return false;
    }
    const std::string k = MetaKey(key);
    return Internal::Guard(impl_->ctx, [&] {
        fz_set_metadata(impl_->ctx, impl_->doc, k.c_str(), value.c_str());
    }, error);
}

}  // namespace XER::Content::Pdf
