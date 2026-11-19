/**
 * CdmContext — the handle the pipeline uses to reach the decryptor
 * (design doc: lives behind the CDM promise/session machinery; the
 * decoder asks for a Decryptor via GetDecryptor()).
 */
#pragma once

#include "media/cdm/decryptor.h"

namespace XER::Media {

class CdmContext {
public:
    virtual ~CdmContext() = default;
    virtual Decryptor* GetDecryptor() = 0;
    virtual bool is_valid() const = 0;
};

// Simple owning context for the embedded (in-process) case.
class LocalCdmContext final : public CdmContext {
public:
    explicit LocalCdmContext(Decryptor* decryptor) : decryptor_(decryptor) {}
    Decryptor* GetDecryptor() override { return decryptor_; }
    bool is_valid() const override { return decryptor_ != nullptr; }

private:
    Decryptor* decryptor_;  // non-owning; host owns the CDM
};

}  // namespace XER::Media
