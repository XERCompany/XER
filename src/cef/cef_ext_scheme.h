// cef_ext_scheme.h — xer-ext:// scheme for .xexrj extension packages.
//
// Extensions render their UI inside the editor through a custom CEF
// scheme. URLs look like xer-ext://<extension-id>/<member-path> and
// every byte is served straight out of the verified TZ archive held in
// memory — no extraction to disk, no way for a page to reach files the
// archive does not declare. The registry lives in the browser process;
// the extension host (tools/video_editor) registers archives after
// they pass TzVerify, unregisters on unload.
//
// When XER_HAS_CEF=0 the registry still works (the host uses it to
// resolve xer-ext:// URLs for diagnostics); only the CEF hooks are
// compiled out.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace XER::Tz { class TzArchive; }

namespace XER::Cef {

// Registers a verified archive under an extension id (the manifest
// [extension].id). Re-registering the same id replaces the mapping.
// Not thread-safe against a CEF request racing Unregister — call from
// the UI thread while no extension page is loading.
void ExtSchemeRegister(const std::string& ext_id,
                       std::shared_ptr<Tz::TzArchive> archive);
void ExtSchemeUnregister(const std::string& ext_id);
// Returns the registered archive or nullptr.
std::shared_ptr<Tz::TzArchive> ExtSchemeLookup(const std::string& ext_id);
// Builds the canonical URL for a member: xer-ext://<id>/<member>.
std::string ExtSchemeUrl(const std::string& ext_id,
                         const std::string& member);

// Called from XERCefApp::OnContextInitialized — registers the scheme
// handler factory for xer-ext on the global request context. No-op
// without XER_HAS_CEF.
void RegisterExtSchemeHandler();

}  // namespace XER::Cef
