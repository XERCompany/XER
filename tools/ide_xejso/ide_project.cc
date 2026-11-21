// ide_project.cc — see ide_project.h for the contract.
#include "ide_project.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "tz/tz.h"

#ifdef _WIN32
#include <windows.h>
#endif

// worker_pipe + the JSON-lines ext_* protocol live in the VideoEditor
// core (VE.ExtensionsRuntime.exe is spawned sibling-only — it must sit
// beside IDEXEJSO.exe for Test to work).
#include "worker_pipe.h"
#include <json/json.h>

#include "ide_format.h"
#include "ide_preview_widget.h"   // IdePreviewSelftest
#ifdef _WIN32
#include "code_editor_widget.h"   // headless tokenizer for selftest
#endif

namespace XER::IdeXejso {
namespace {

namespace fs = std::filesystem;

// ---- UTF-8 path plumbing --------------------------------------------------

fs::path U8Path(const std::string& utf8) {
    return fs::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

std::string U8(const fs::path& p) {
    // generic_u8string() is std::u8string in C++20 — copy via char range.
    const auto s = p.generic_u8string();
    return {s.begin(), s.end()};
}

#ifdef _WIN32
std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1,
                                      nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    out.pop_back();
    return out;
}
#endif

std::string SanitizeId(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (const unsigned char c : raw) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-') {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == ' ' || c == '/' || c == '\\') {
            out.push_back('-');
        }
    }
    while (!out.empty() && (out.front() == '-' || out.front() == '.')) {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "extension";
    return out;
}

// ---- scaffold templates ----------------------------------------------------

std::string ManifestToml(const std::string& id, const std::string& name,
                         bool with_native) {
    std::ostringstream out;
    out << "[extension]\n"
        << "id = \"" << id << "\"\n"
        << "name = \"" << name << "\"\n"
        << "version = \"0.1\"\n"
        << "entry = \"main.js\"\n"
        << "ui = \"static/index.html\"\n"
        << "\n"
        << "[files]\n"
        << "requires = [\"main.js\", \"scripts/helper.js\", "
           "\"static/index.html\"]\n"
        << "\n"
        << "[native]\n"
        << "dlls = []\n"
        << "lua = []\n"
        << "py = []\n";
    if (with_native) {
        // [native] must stay all-arrays — the extension host rejects
        // scalar values in this section. The JIT probe symbol lives
        // under [pack] instead (ignored by the host AND ztzmake).
        out << "sources = [\"native/hello.cc\"]\n";
    }
    out << "\n"
        << "[pack]\n"
        << "keep_sources = false\n"
        << "# IDE hint: the symbol `Test` ext_calls after ext_load.\n"
        << "probe = \"ve_ext_probe\"\n";
    return out.str();
}

std::string MainJs(const std::string& id) {
    std::ostringstream out;
    out << "// " << id << " — extension entry point. Runs inside the\n"
        << "// editor's V8 via the xer_ext module (see the demo in\n"
        << "// tools/video_editor/assets/extensions_src/demo).\n"
        << "const ext = NativeImport(\"xer_ext\");\n"
        << "ext.log(\"" << id << " extension starting, id=\" + ext.id());\n"
        << "ext.setStatus(\"" << id << " loaded from .xexrj\");\n"
        << "ext.log(\"helper.js: \" +\n"
        << "        ext.readFile(\"scripts/helper.js\").trim());\n"
        << "// Uncomment to render the extension UI into the editor's\n"
        << "// Web tab via CEF:\n"
        << "// ext.openUi();\n";
    return out.str();
}

std::string HelperJs() {
    return "// scripts/helper.js — declared in [files] requires.\n"
           "function helperName() { return \"helper\"; }\n";
}

std::string IndexHtml(const std::string& name) {
    std::ostringstream out;
    out << "<!doctype html>\n"
        << "<html><head><meta charset=\"utf-8\"><title>" << name
        << "</title>\n"
        << "<style>body{font-family:sans-serif;background:#181818;"
           "color:#ddd;padding:24px}</style>\n"
        << "</head><body>\n"
        << "<h2>" << name << "</h2>\n"
        << "<p>Extension UI served from the .xexrj via the xer-ext:// "
           "scheme.</p>\n"
        << "</body></html>\n";
    return out.str();
}

// Freestanding JIT payload — mirrors the demo contract (tools/
// video_editor/assets/extensions_src/demo/native/hello.cc): only ve_*
// symbols may stay undefined, C linkage on the exports, extension-
// owned storage for ext_call results.
std::string NativeHello(const std::string& id) {
    std::ostringstream out;
    out << "// native/hello.cc — JIT payload for " << id << ".\n"
        << "// ztzmake compiles this into mobjective/hello.xsjo inside\n"
        << "// the .xexrj; VE.ExtensionsRuntime JIT-links it and binds\n"
        << "// the ve_* imports. Keep the TU freestanding (no libc).\n"
        << "#if defined(__has_include)\n"
        << "#  if __has_include(\"ve.core.h\")\n"
        << "#    include \"ve.core.h\"\n"
        << "#    define XEJSO_HAVE_VE_CORE 1\n"
        << "#  endif\n"
        << "#endif\n"
        << "\n"
        << "#if !defined(XEJSO_HAVE_VE_CORE)\n"
        << "#include <stdint.h>\n"
        << "#ifdef __cplusplus\n"
        << "extern \"C\" {\n"
        << "#endif\n"
        << "#define VE_CORE_API_VERSION 2\n"
        << "enum { VE_OK = 0, VE_ERR_GENERIC = 1 };\n"
        << "typedef struct ve_ctx ve_ctx;\n"
        << "void ve_log(ve_ctx* ctx, const char* message);\n"
        << "void ve_set_status(ve_ctx* ctx, const char* message);\n"
        << "#ifdef __cplusplus\n"
        << "}\n"
        << "#endif\n"
        << "#endif  // !XEJSO_HAVE_VE_CORE\n"
        << "\n"
        << "extern \"C\" {\n"
        << "\n"
        << "int ve_ext_api_version(void) { return VE_CORE_API_VERSION; }\n"
        << "\n"
        << "int ve_ext_entry(ve_ctx* ctx) {\n"
        << "    ve_set_status(ctx, \"" << id << " native module "
           "JIT-loaded\");\n"
        << "    ve_log(ctx, \"hello from " << id
        << " mobjective/hello.xsjo\");\n"
        << "    return VE_OK;\n"
        << "}\n"
        << "\n"
        << "void ve_ext_shutdown(ve_ctx* ctx) {\n"
        << "    ve_log(ctx, \"" << id << " native module unloading\");\n"
        << "}\n"
        << "\n"
        << "// The IDE's Test button ext_calls this after ext_load\n"
        << "// (declared via [pack] probe in manifest.toml). Result\n"
        << "// must reference extension-owned storage.\n"
        << "const char* ve_ext_probe(ve_ctx* ctx, "
        << "const char* args_json) {\n"
        << "    (void)ctx;\n"
        << "    (void)args_json;\n"
        << "    return \"{\\\"ok\\\":true,\\\"probe\\\":\\\"" << id
        << "\\\"}\";\n"
        << "}\n"
        << "\n"
        << "}  // extern \"C\"\n";
    return out.str();
}

// ---- tiny TOML helpers (same line-based subset ztzmake uses) ---------

std::string TomlUnquote(const std::string& s, bool* ok) {
    *ok = s.size() >= 2 && s.front() == '"' && s.back() == '"';
    if (!*ok) return {};
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 2 < s.size()) {
            const char n = s[i + 1];
            if (n == '"' || n == '\\') { out.push_back(n); ++i; continue; }
            if (n == 'n') { out.push_back('\n'); ++i; continue; }
            if (n == 't') { out.push_back('\t'); ++i; continue; }
        }
        out.push_back(c);
    }
    return out;
}

// ---- JSON-line helpers ----------------------------------------------------

std::string JsonLine(const Json::Value& v) {
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, v);
}

bool ParseJsonLine(const std::string& line, Json::Value* out) {
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    return Json::parseFromStream(r, in, out, &errs) && out->isObject();
}

}  // namespace

// ---- environment ------------------------------------------------------------

std::string ExeDirUtf8() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) != 0) {
        return U8(fs::path(buf).parent_path());
    }
#endif
    std::error_code ec;
    return U8(fs::current_path(ec));
}

std::string FindSiblingExe(const char* name) {
    const std::string dir = ExeDirUtf8();
    if (dir.empty() || name == nullptr) return {};
    std::error_code ec;
    const fs::path p = U8Path(dir) / name;
    return fs::is_regular_file(p, ec) ? U8(p) : std::string{};
}

// ---- scaffold ---------------------------------------------------------------

bool ScaffoldExtension(const std::string& dir, const std::string& ext_id,
                       const std::string& display_name, bool with_native,
                       std::string* error) {
    const std::string id = SanitizeId(ext_id);
    const std::string name =
        display_name.empty() ? id : display_name;
    std::error_code ec;
    const fs::path root = U8Path(dir);
    if (fs::exists(root / "manifest.toml", ec)) {
        if (error != nullptr) {
            *error = "manifest.toml already exists in " + dir;
        }
        return false;
    }
    fs::create_directories(root / "scripts", ec);
    fs::create_directories(root / "static", ec);
    fs::create_directories(root / "native", ec);
    if (ec) {
        if (error != nullptr) *error = "cannot create " + dir;
        return false;
    }
    const auto write = [&](const fs::path& rel, const std::string& text) {
        std::ofstream out(root / rel, std::ios::binary | std::ios::trunc);
        out << text;
        return static_cast<bool>(out);
    };
    bool ok = write("manifest.toml", ManifestToml(id, name, with_native));
    ok = ok && write("main.js", MainJs(id));
    ok = ok && write(fs::path("scripts") / "helper.js", HelperJs());
    ok = ok && write(fs::path("static") / "index.html", IndexHtml(name));
    if (with_native) {
        ok = ok && write(fs::path("native") / "hello.cc", NativeHello(id));
    }
    if (!ok && error != nullptr) {
        *error = "failed writing scaffold files under " + dir;
    }
    return ok;
}

// ---- file listing -------------------------------------------------------------

std::vector<FileItem> ListProjectFiles(
    const std::string& root,
    const std::vector<std::string>& collapsed) {
    std::vector<FileItem> out;
    const fs::path base = U8Path(root);
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return out;
    const std::set<std::string> closed(collapsed.begin(),
                                       collapsed.end());

    // DFS emitting each level dirs-first then name-sorted.
    const std::function<void(const fs::path&, const std::string&, int)>
        walk = [&](const fs::path& dir, const std::string& rel,
                   int depth) {
            std::vector<fs::directory_entry> kids;
            std::error_code ec2;
            for (const auto& de : fs::directory_iterator(dir, ec2)) {
                kids.push_back(de);
            }
            std::sort(kids.begin(), kids.end(),
                      [](const fs::directory_entry& a,
                         const fs::directory_entry& b) {
                          std::error_code e1, e2;
                          const bool da = a.is_directory(e1);
                          const bool db = b.is_directory(e2);
                          if (da != db) return da;
                          return a.path().filename()
                                     .generic_u8string() <
                                 b.path().filename()
                                     .generic_u8string();
                      });
            for (const auto& de : kids) {
                const bool is_dir = de.is_directory(ec2);
                const std::string name = U8(de.path().filename());
                const std::string child_rel =
                    rel.empty() ? name : rel + "/" + name;
                FileItem item;
                item.rel_path = is_dir ? child_rel + "/" : child_rel;
                item.is_dir = is_dir;
                item.depth = depth;
                out.push_back(item);
                if (is_dir && !closed.count(child_rel)) {
                    walk(de.path(), child_rel, depth + 1);
                }
            }
        };
    walk(base, "", 0);
    return out;
}

// ---- file io ------------------------------------------------------------------

bool ReadTextFile(const std::string& path, std::string* out,
                  std::string* error) {
    std::ifstream in(U8Path(path), std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot open " + path;
        return false;
    }
    *out = std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    return true;
}

bool WriteTextFile(const std::string& path, const std::string& text,
                   std::string* error) {
    std::error_code ec;
    const fs::path p = U8Path(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) *error = "cannot write " + path;
        return false;
    }
    out << text;
    if (!out) {
        if (error != nullptr) *error = "write failed on " + path;
        return false;
    }
    return true;
}

// ---- editor tabs --------------------------------------------------------------

int EditorTabs::Open(const std::string& rel) {
    const int found = Find(rel);
    if (found >= 0) {
        active = found;
        return found;
    }
    Tab t;
    t.rel_path = rel;
    tabs.push_back(std::move(t));
    active = static_cast<int>(tabs.size()) - 1;
    return active;
}

bool EditorTabs::Close(int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) {
        return false;
    }
    tabs.erase(tabs.begin() + index);
    if (tabs.empty()) {
        active = -1;
    } else if (active > index) {
        --active;
    } else if (active == index) {
        active = std::min(index, static_cast<int>(tabs.size()) - 1);
    }
    return true;
}

int EditorTabs::Find(const std::string& rel) const {
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i].rel_path == rel) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EditorTabs::Activate(int index) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) {
        return false;
    }
    active = index;
    return true;
}

bool EditorTabs::SetDirty(int index, bool dirty) {
    if (index < 0 || index >= static_cast<int>(tabs.size())) {
        return false;
    }
    tabs[static_cast<std::size_t>(index)].dirty = dirty;
    return true;
}

bool EditorTabs::AnyDirty() const {
    return std::any_of(tabs.begin(), tabs.end(),
                       [](const Tab& t) { return t.dirty; });
}

// ---- build-log diagnostics -----------------------------------------------------

bool ParseDiagnostic(const std::string& log_line, DiagnosticRef* out) {
    const std::string& l = log_line;
    const std::size_t n = l.size();
    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (l[i] != ':' ||
            !std::isdigit(static_cast<unsigned char>(l[i + 1]))) {
            continue;
        }
        // Digits for the line number.
        std::size_t j = i + 1;
        while (j < n &&
               std::isdigit(static_cast<unsigned char>(l[j]))) {
            ++j;
        }
        const int line_no = std::atoi(l.substr(i + 1, j - i - 1).c_str());
        if (line_no <= 0) continue;
        int col_no = 0;
        std::size_t tail = j;
        if (j < n && l[j] == ':' && j + 1 < n &&
            std::isdigit(static_cast<unsigned char>(l[j + 1]))) {
            std::size_t k = j + 1;
            while (k < n &&
                   std::isdigit(static_cast<unsigned char>(l[k]))) {
                ++k;
            }
            col_no = std::atoi(l.substr(j + 1, k - j - 1).c_str());
            tail = k;
        }
        // The char after the location must end it (':' or ws or EOS).
        if (tail < n && l[tail] != ':' && l[tail] != ' ' &&
            l[tail] != '\t' && l[tail] != ')' && l[tail] != ',') {
            continue;
        }
        // The path is the run of non-whitespace before the ':'.
        std::size_t start = i;
        while (start > 0) {
            const char p = l[start - 1];
            if (std::isspace(static_cast<unsigned char>(p)) ||
                p == '"' || p == '\'' || p == '(' || p == '[' ||
                p == '<') {
                break;
            }
            --start;
        }
        const std::string path = l.substr(start, i - start);
        // Require it to look like a file path (separators or an
        // extension) — "note: 3:4" must not match.
        if (path.empty() ||
            (path.find('/') == std::string::npos &&
             path.find('\\') == std::string::npos &&
             path.find('.') == std::string::npos)) {
            continue;
        }
        if (out != nullptr) {
            out->path = path;
            out->line = line_no;
            out->col = col_no;
            const std::string tail_s = l.substr(tail);
            out->is_error =
                tail_s.find("error") != std::string::npos ||
                tail_s.find("fatal") != std::string::npos;
        }
        return true;
    }
    return false;
}

// ---- manifest lite ------------------------------------------------------------

bool ParseManifestLite(const std::string& text, ManifestInfo* out) {
    std::istringstream in(text);
    std::string line, section;
    bool any = false;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const auto e = line.find_last_not_of(" \t\r");
        const std::string s = line.substr(b, e - b + 1);
        if (s.front() == '[' && s.back() == ']') {
            section = s.substr(1, s.size() - 2);
            continue;
        }
        const auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        const auto kb = key.find_first_not_of(" \t");
        const auto ke = key.find_last_not_of(" \t");
        const auto vb = val.find_first_not_of(" \t");
        const auto ve = val.find_last_not_of(" \t");
        if (kb == std::string::npos || vb == std::string::npos) continue;
        key = key.substr(kb, ke - kb + 1);
        val = val.substr(vb, ve - vb + 1);
        bool ok = false;
        const std::string v = TomlUnquote(val, &ok);
        if (!ok) continue;
        if (section == "extension") {
            if (key == "id") { out->id = v; any = true; }
            else if (key == "name") out->name = v;
            else if (key == "entry") out->entry = v;
        } else if (section == "pack" && key == "probe") {
            out->probe = v;
        }
    }
    return any;
}

// ---- process spawn ------------------------------------------------------------

bool RunProcessCapture(const std::string& exe,
                       const std::vector<std::string>& args,
                       unsigned long timeout_ms, std::string* output,
                       int* exit_code, const LogFn& on_line) {
#ifndef _WIN32
    (void)exe; (void)args; (void)timeout_ms;
    (void)output; (void)exit_code; (void)on_line;
    return false;
#else
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE read_h = nullptr, write_h = nullptr;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) return false;
    SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + Widen(exe) + L"\"";
    for (const std::string& a : args) {
        cmd += L" \"" + Widen(a) + L"\"";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_h;
    si.hStdError = write_h;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    const BOOL spawned = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_h);
    if (!spawned) {
        CloseHandle(read_h);
        return false;
    }
    std::string captured;
    std::string carry;
    char buf[4096];
    DWORD got = 0;
    const auto emit = [&]() {
        if (!on_line) return;
        for (;;) {
            const auto nl = carry.find('\n');
            if (nl == std::string::npos) break;
            std::string line = carry.substr(0, nl);
            carry.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) on_line(line);
        }
    };
    while (ReadFile(read_h, buf, sizeof(buf), &got, nullptr) && got > 0) {
        captured.append(buf, got);
        if (on_line) {
            carry.append(buf, got);
            emit();
        }
    }
    if (on_line && !carry.empty()) {
        if (carry.back() == '\r') carry.pop_back();
        on_line(carry);
    }
    CloseHandle(read_h);
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (output != nullptr) *output = std::move(captured);
    if (exit_code != nullptr) {
        *exit_code = static_cast<int>(code);
    }
    return true;
#endif
}

// ---- build ----------------------------------------------------------------------

bool BuildExtension(const std::string& project_dir,
                    std::string* out_xexrj, const LogFn& log) {
    const std::string ztz = FindSiblingExe("ztzmake.exe");
    if (ztz.empty()) {
        if (log) log("build: ztzmake.exe not found beside IDEXEJSO.exe");
        return false;
    }
    const fs::path root = U8Path(project_dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        if (log) log("build: project dir missing: " + project_dir);
        return false;
    }
    const std::string dir_name = U8(root.filename());
    const fs::path out_path =
        root.parent_path() / (dir_name + ".xexrj");
    if (log) {
        log("build: ztzmake \"" + project_dir + "\" -o \"" +
            U8(out_path) + "\"");
    }
    std::string captured;
    int code = 1;
    const bool ran = RunProcessCapture(
        ztz, {project_dir, "-o", U8(out_path)}, 300000, &captured,
        &code,
        log ? LogFn([&log](const std::string& l) { log("  " + l); })
            : LogFn{});
    if (!ran) {
        if (log) log("build: failed to spawn ztzmake.exe");
        return false;
    }
    if (code != 0) {
        if (log) {
            log("build: ztzmake exited " + std::to_string(code));
        }
        return false;
    }
    if (fs::is_regular_file(out_path, ec)) {
        if (out_xexrj != nullptr) *out_xexrj = U8(out_path);
        if (log) log("build: wrote " + U8(out_path));
        return true;
    }
    if (log) log("build: ztzmake exited 0 but no archive appeared");
    return false;
}

// ---- test (VE.ExtensionsRuntime over worker_pipe) -------------------------------

#ifdef _WIN32
// One ext-runtime request round trip — the same envelope the editor's
// ExtRoundTrip uses: {"id":N,"kind":...} -> {"id":N,"ok":bool, ...}.
bool ExtRoundTrip(XER::VideoEditor::WorkerPipe& pipe, Json::Value& req,
                  int timeout_ms, Json::Value* reply,
                  std::string* err) {
    const std::uint64_t id = pipe.NextId();
    req["id"] = Json::UInt64(id);
    std::string line;
    if (!pipe.RoundTrip(id, JsonLine(req), timeout_ms, &line)) {
        if (err != nullptr) *err = "transport failure / timeout";
        return false;
    }
    if (!ParseJsonLine(line, reply)) {
        if (err != nullptr) *err = "malformed reply line";
        return false;
    }
    if (!reply->get("ok", false).asBool()) {
        if (err != nullptr) {
            *err = reply->get("err", "worker error").asString();
        }
        return false;
    }
    return true;
}

// Answers parked ve_call host_call requests with the IDE's stub reply
// (the reverse channel contract — see worker_pipe.h).
void DrainHostCalls(XER::VideoEditor::WorkerPipe& pipe,
                    const LogFn& log) {
    std::string line;
    while (pipe.PopCallbackLine(&line)) {
        Json::Value req;
        if (!ParseJsonLine(line, &req) ||
            req.get("kind", "").asString() != "host_call") {
            continue;
        }
        const auto req_id = req.get("req_id", 0).asUInt64();
        const std::string name = req.get("name", "").asString();
        if (log) {
            log("  host_call: " + name + " (no project host in IDE)");
        }
        Json::Value rsp(Json::objectValue);
        rsp["req_id"] = Json::UInt64(req_id);
        rsp["ok"] = true;
        // The service-result contract is a JSON string — report the
        // missing host INSIDE the result like an editor dispatch
        // failure would.
        rsp["result"] =
            "{\"ok\":false,\"error\":\"IDE: no project host\"}";
        pipe.WriteCallbackLine(JsonLine(rsp));
    }
}

void LogExtEvents(const Json::Value& events, const LogFn& log) {
    if (log == nullptr || !events.isArray()) return;
    for (const auto& e : events) {
        const std::string type = e.get("type", "?").asString();
        std::string msg = e.get("msg", "").asString();
        if (msg.empty() && e.isMember("kind")) {
            msg = e["kind"].asString() + " " +
                  e.get("param", "").asString();
        }
        log("  ext event [" + type + "] " + msg);
    }
}
#endif

bool TestExtension(const std::string& xexrj_path,
                   const std::string& probe_fn, const LogFn& log) {
#ifndef _WIN32
    (void)xexrj_path; (void)probe_fn;
    if (log) log("test: worker_pipe is Windows-only");
    return false;
#else
    namespace VE = XER::VideoEditor;
    const std::string exe = FindSiblingExe("VE.ExtensionsRuntime.exe");
    if (exe.empty()) {
        if (log) {
            log("test: VE.ExtensionsRuntime.exe not found beside "
                "IDEXEJSO.exe — build it to test extensions");
        }
        return false;
    }
    VE::WorkerPipe& pipe = VE::ExtRuntimePipe();
    if (!pipe.Available()) {
        if (log) log("test: ext runtime pipe unavailable");
        return false;
    }
    std::string role;
    if (!pipe.Ping(&role)) {
        if (log) log("test: ext runtime ping failed");
        return false;
    }
    if (log) log("test: runtime role = " + role);

    std::string err;
    Json::Value req(Json::objectValue), reply;
    req["kind"] = "ext_load";
    req["path"] = xexrj_path;
    req["project_json"] = "{}";
    if (!ExtRoundTrip(pipe, req, 120000, &reply, &err)) {
        if (log) log("test: ext_load failed — " + err);
        return false;
    }
    const std::string ext_id = reply.get("ext_id", "").asString();
    const bool native = reply.get("native", false).asBool();
    if (log) {
        log("test: loaded '" + ext_id + "' (" +
            reply.get("name", "").asString() + ")" +
            (native ? " native modules=" +
                          std::to_string(reply.get("modules", 0).asUInt64())
                    : " JS/static only — nothing JIT-side to call"));
        const Json::Value& ev = reply["entry_rc"];
        if (native && ev.isInt()) {
            log("test: ve_ext_entry rc=" + std::to_string(ev.asInt()));
        }
    }
    LogExtEvents(reply["events"], log);

    bool ok = true;
    if (native) {
        const std::string fn =
            probe_fn.empty() ? "ve_ext_probe" : probe_fn;
        DrainHostCalls(pipe, log);
        req.clear();
        req["kind"] = "ext_call";
        req["ext_id"] = ext_id;
        req["fn"] = fn;
        req["args"] = "{}";
        reply.clear();
        if (ExtRoundTrip(pipe, req, 30000, &reply, &err)) {
            if (log) {
                log("test: " + fn + " -> " +
                    reply.get("result_json", "").asString());
            }
            LogExtEvents(reply["events"], log);
        } else if (err.find("symbol not found") !=
                   std::string::npos) {
            if (log) {
                log("test: no probe fn '" + fn +
                    "' exported — load+entry verified only");
            }
        } else {
            if (log) log("test: ext_call " + fn + " failed — " + err);
            ok = false;
        }
        DrainHostCalls(pipe, log);
        req.clear();
        req["kind"] = "ext_unload";
        req["ext_id"] = ext_id;
        reply.clear();
        if (!ExtRoundTrip(pipe, req, 10000, &reply, &err) && log) {
            log("test: ext_unload warning — " + err);
        }
    }
    pipe.KillWorker();
    if (log) log(ok ? "test: PASS" : "test: FAIL");
    return ok;
#endif
}

// ---- open in editor -------------------------------------------------------------

bool OpenInEditor(const std::string& xexrj_path, const LogFn& log) {
    const std::string editor = FindSiblingExe("video_editor.exe");
    if (editor.empty()) {
        if (log) {
            log("editor: video_editor.exe not found beside "
                "IDEXEJSO.exe");
        }
        return false;
    }
    std::error_code ec;
    const fs::path exe_dir = U8Path(ExeDirUtf8());
    const fs::path ext_dir = exe_dir / "assets" / "extensions";
    fs::create_directories(ext_dir, ec);
    const fs::path dst =
        ext_dir / U8Path(xexrj_path).filename();
    fs::copy_file(U8Path(xexrj_path), dst,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (log) log("editor: cannot deploy to " + U8(dst));
        return false;
    }
    if (log) log("editor: deployed " + U8(dst));
#ifdef _WIN32
    // CreateProcessW may write into the command line buffer: keep it mutable.
    std::wstring cmd = L"\"" + Widen(editor) + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        if (log) log("editor: failed to launch video_editor.exe");
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#endif
    if (log) log("editor: launched video_editor.exe");
    return true;
}

// ---- selftest ---------------------------------------------------------------------

int RunSelftest(const LogFn& log) {
    const auto say = [&log](const std::string& s) {
        if (log) log(s);
        else std::cout << s << '\n';
    };
    const auto step = [&say](const char* name, bool ok,
                             const std::string& detail = {}) {
        say(std::string("SELFTEST ") + name + (ok ? " OK" : " FAIL") +
            (detail.empty() ? "" : " — " + detail));
        return ok;
    };

    std::error_code ec;
    const fs::path scratch =
        fs::temp_directory_path(ec) /
        ("ide_xejso_selftest_" +
         std::to_string(static_cast<unsigned long long>(
             std::chrono::steady_clock::now()
                 .time_since_epoch()
                 .count())));
    fs::remove_all(scratch, ec);
    const std::string proj_dir = U8(scratch / "selftest_ext");

    std::string error;
    // 1. scaffold (native payload first — the full pipeline; the
    //    JS-only retry below covers toolchains without libclang).
    bool built_native = true;
    if (!step("scaffold",
              ScaffoldExtension(proj_dir, "selftest", "Selftest Ext",
                                true, &error),
              error)) {
        fs::remove_all(scratch, ec);
        return 1;
    }
    // 2. listing must see the core files.
    const auto items = ListProjectFiles(proj_dir);
    const auto has = [&items](const char* rel) {
        return std::any_of(items.begin(), items.end(),
                           [&](const FileItem& f) {
                               return f.rel_path == rel;
                           });
    };
    const bool listing_ok = has("manifest.toml") && has("main.js") &&
                            has("static/index.html") &&
                            has("native/hello.cc");
    if (!step("list", listing_ok)) {
        fs::remove_all(scratch, ec);
        return 1;
    }

    // 3. build through the real sibling ztzmake; a toolchain without
    //    libclang fails only on the .cc payload, so rescaffold JS-only
    //    and pack again — the archive path is still fully exercised.
    std::string xexrj;
    if (!BuildExtension(proj_dir, &xexrj, say)) {
        say("SELFTEST note — native pack failed; retrying JS-only "
            "(no libclang in this toolchain?)");
        fs::remove_all(scratch, ec);
        built_native = false;
        if (!ScaffoldExtension(proj_dir, "selftest", "Selftest Ext",
                               false, &error) ||
            !BuildExtension(proj_dir, &xexrj, say)) {
            step("build", false, error);
            fs::remove_all(scratch, ec);
            return 1;
        }
    }
    if (!step("build", !xexrj.empty(),
              built_native ? "" : "JS-only (native compile skipped)")) {
        fs::remove_all(scratch, ec);
        return 1;
    }

    // 4. verify + member listing.
    const auto report = XER::Tz::TzVerify(xexrj);
    if (!step("verify", report.ok,
              XER::Tz::TzReportText(report))) {
        fs::remove_all(scratch, ec);
        return 1;
    }
    std::string open_err;
    const auto archive = XER::Tz::TzArchive::Open(xexrj, &open_err);
    bool members_ok = archive != nullptr && archive->Has("manifest.toml") &&
                      archive->Has("main.js");
    if (archive != nullptr) {
        for (const auto& e : archive->Entries()) {
            say("  member: " + e.name + " (" +
                std::to_string(e.size) + " B)");
        }
    }
    if (!step("members", members_ok, open_err)) {
        fs::remove_all(scratch, ec);
        return 1;
    }

    // 5. formatter + editor-model checks (all headless).
    {
        // JS beautifier idempotency: format(format(x)) == format(x).
        const std::string ugly_js =
            "const ext=NativeImport(\"xer_ext\");\n"
            "function go(){ext.log( \"hi\" );if(ext.id()){return 1;}\n"
            "return   0;}\n"
            "ext.ve(\"timeline.info\").then(x=>ext.log(x));\n";
        const std::string once = FormatJs(ugly_js);
        const std::string twice = FormatJs(once);
        step("fmt-js", !once.empty() && once == twice,
             once == twice ? "" : "second pass differed");
        if (once != twice) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    }
    if (ClangFormatAvailable()) {
        // C++ clang-format: ugly input must come out normalized.
        const std::string ugly_cc =
            "int ve_ext_probe(ve_ctx* ctx,const char* args_json){\n"
            "(void)ctx;(void)args_json;\n"
            "return \"{\\\"ok\\\":true}\";}\n";
        std::string pretty, f_err;
        const bool f_ok =
            FormatCpp(ugly_cc, "x.cc", &pretty, &f_err);
        if (!step("fmt-cpp",
                  f_ok && pretty != ugly_cc &&
                      pretty.find("ve_ext_probe") != std::string::npos,
                  f_err)) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    } else {
        say("SELFTEST fmt-cpp SKIP — libclang-cpp not linked "
            "(XER_IDE_HAS_CLANG unset)");
    }
#ifdef _WIN32
    {
        // Tokenizer sanity: pathological inputs must tokenize without
        // crashing and end in sane cross-line states.
        bool tok_ok = true;
        // Balanced block comment across lines.
        {
            const std::vector<std::string> doc = {
                "int a; /* open", "still comment */ int b;"};
            const auto spans =
                TokenizeDocument(EditorLanguage::Cpp, doc);
            tok_ok &= spans.size() == 2;
            tok_ok &= !spans[1].empty() &&
                      spans[1].front().kind == TokKind::Comment;
        }
        // Unterminated string stays inside itself to EOL.
        {
            LineLexState st;
            const auto s = TokenizeLine(EditorLanguage::Cpp,
                                        "const char* s = \"unterm", st);
            tok_ok &= !s.empty() &&
                      s.back().kind == TokKind::String &&
                      s.back().end == 23;
        }
        // JS template literal spanning lines -> mode 2 then closed.
        {
            const std::vector<std::string> doc = {
                "let t = `line one", "two ${x} three`;"};
            const auto spans =
                TokenizeDocument(EditorLanguage::JavaScript, doc);
            tok_ok &= spans.size() == 2 && !spans[1].empty() &&
                      spans[1].front().kind == TokKind::String;
        }
        // C++ raw string across lines.
        {
            const std::vector<std::string> doc = {
                "auto s = R\"tag(l1", "l2)tag\"; int y;"};
            const auto spans =
                TokenizeDocument(EditorLanguage::Cpp, doc);
            tok_ok &= spans.size() == 2 && !spans[1].empty() &&
                      spans[1].front().kind == TokKind::String;
        }
        // Garbage bytes: must never crash or produce empty doc spans.
        {
            std::string junk;
            for (int i = 0; i < 256; ++i) {
                junk.push_back(static_cast<char>(i));
            }
            const std::vector<std::string> doc = {junk, "x"};
            const auto spans =
                TokenizeDocument(EditorLanguage::Cpp, doc);
            tok_ok &= spans.size() == 2;
        }
        if (!step("tokenizer", tok_ok)) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    }
    {
        // HTML/CSS language detection + tokenizer checks.
        bool ok = true;
        ok &= LanguageForPath("static/index.html") ==
              EditorLanguage::Html;
        ok &= LanguageForPath("a.htm") == EditorLanguage::Html;
        ok &= LanguageForPath("doc.xml") == EditorLanguage::Html;
        ok &= LanguageForPath("p.xhtml") == EditorLanguage::Html;
        ok &= LanguageForPath("icon.svg") == EditorLanguage::Html;
        ok &= LanguageForPath("site.css") == EditorLanguage::Css;
        ok &= std::string(LanguageName(EditorLanguage::Html)) == "HTML";
        const auto kind_at =
            [](const std::vector<HighlightSpan>& v, std::size_t pos) {
                for (const auto& s : v) {
                    if (pos >= s.begin && pos < s.end) return s.kind;
                }
                return TokKind::Plain;
            };
        {
            const std::vector<std::string> doc = {
                "<!-- head -->",
                "<div class=\"a\">hi</div>",
                "<script>let x = 1;",
                "x += 2;</script><p>t</p>",
                "<style>body { color: #fff }</style>",
            };
            const auto sp =
                TokenizeDocument(EditorLanguage::Html, doc);
            ok &= sp.size() == 5;
            ok &= !sp[0].empty() &&
                  sp[0].front().kind == TokKind::Comment;
            // tag name vs attr name vs quoted value vs text
            ok &= kind_at(sp[1], 2) == TokKind::Keyword;   // div
            ok &= kind_at(sp[1], 7) == TokKind::Type;      // class
            ok &= kind_at(sp[1], 12) == TokKind::String;   // "a"
            ok &= kind_at(sp[1], 16) == TokKind::Plain;    // hi text
            // <script> line delegated to the JS tokenizer
            ok &= kind_at(sp[2], 9) == TokKind::Keyword;   // let
            ok &= kind_at(sp[2], 16) == TokKind::Number;   // 1
            // a line starting mid-script stays JS until </script>
            ok &= kind_at(sp[3], 0) == TokKind::Plain;     // x (JS ident)
            ok &= kind_at(sp[3], 5) == TokKind::Number;    // 2
            ok &= kind_at(sp[3], 8) == TokKind::Punct;     // </
            ok &= kind_at(sp[3], 10) == TokKind::Keyword;  // script
            ok &= kind_at(sp[3], 17) == TokKind::Keyword;  // p tag
            // <style> delegated to the CSS tokenizer
            ok &= kind_at(sp[4], 8) == TokKind::Keyword;   // body sel
            ok &= kind_at(sp[4], 17) == TokKind::Type;     // color:
            ok &= kind_at(sp[4], 24) == TokKind::Number;   // #fff
        }
        // Standalone CSS.
        {
            const std::vector<std::string> doc = {
                "/* c */ body { margin: 0 }",
                ".a { color: red; /* tail */ }",
            };
            const auto sp =
                TokenizeDocument(EditorLanguage::Css, doc);
            ok &= sp.size() == 2;
            ok &= !sp[0].empty() &&
                  sp[0].front().kind == TokKind::Comment;
            ok &= kind_at(sp[0], 10) == TokKind::Keyword;  // body
            ok &= kind_at(sp[0], 19) == TokKind::Type;     // margin:
            ok &= kind_at(sp[0], 23) == TokKind::Number;   // 0
        }
        // Headless auto-indent.
        {
            AutoIndent ai = AutoIndentForNewline(
                "if (x) {", 8, EditorLanguage::Cpp, "    ");
            ok &= ai.indent == "    " && !ai.extra_line;
            ai = AutoIndentForNewline(
                "x = 1;", 6, EditorLanguage::Cpp, "    ");
            ok &= ai.indent.empty();
            ai = AutoIndentForNewline(
                "{}", 1, EditorLanguage::Cpp, "    ");
            ok &= ai.indent == "    " && ai.extra_line &&
                  ai.closing.empty();
            ai = AutoIndentForNewline(
                "\t\tif {", 6, EditorLanguage::Cpp, "\t");
            ok &= ai.indent == "\t\t\t";
            ai = AutoIndentForNewline(
                "<div>", 5, EditorLanguage::Html, "    ");
            ok &= ai.indent == "    ";
            ai = AutoIndentForNewline(
                "  <p>t</p>", 10, EditorLanguage::Html, "    ");
            ok &= ai.indent == "  ";
            ok &= DetectIndentUnit({"\tx;", "y"}) == "\t";
            ok &= DetectIndentUnit({"    y;", "z"}) == "    ";
        }
        // Auto-pairs: pair/overtype/plain and backspace plans.
        {
            ok &= PairedClose('(') == ')' && PairedClose('{') == '}' &&
                  PairedClose('x') == '\0';
            ok &= AutoPairOk("int x", 5);
            ok &= !AutoPairOk("foo", 0);
            PairPlan pp = PlanPairInput("x = ", 4, '(');
            ok &= pp.pair && !pp.overtype;
            pp = PlanPairInput("()", 1, ')');
            ok &= pp.overtype && !pp.pair;
            pp = PlanPairInput("foo", 2, '"');
            ok &= !pp.pair && !pp.overtype;   // next char is a word char
            // Backspace: empty pair deletes both, tab stop, normal.
            BackspacePlan bp = PlanBackspace("()", 1);
            ok &= !bp.join_prev && bp.begin == 0 && bp.end == 2;
            bp = PlanBackspace("        x", 8);
            ok &= bp.begin == 4 && bp.end == 8;
            bp = PlanBackspace("abc", 3);
            ok &= bp.begin == 2 && bp.end == 3;
            bp = PlanBackspace("abc", 0);
            ok &= bp.join_prev;
        }
        if (!step("editor-ux", ok)) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    }
#endif
    {
        // Tab model: open/activate/dirty/close transitions.
        EditorTabs tabs;
        bool ok = tabs.Open("a.js") == 0 && tabs.active == 0;
        ok = ok && tabs.Open("b.cc") == 1 && tabs.active == 1;
        ok = ok && tabs.Open("a.js") == 0 && tabs.active == 0;
        ok = ok && tabs.SetDirty(0, true) && tabs.AnyDirty();
        ok = ok && tabs.Activate(1) && tabs.Close(0) &&
             tabs.active == 0 &&
             tabs.tabs[0].rel_path == "b.cc";
        ok = ok && tabs.Close(0) && tabs.active == -1 &&
             !tabs.Close(0);
        // Diagnostic parsing.
        DiagnosticRef d;
        ok = ok && ParseDiagnostic("native/hello.cc:5:1: error: boom",
                                   &d) &&
             d.path == "native/hello.cc" && d.line == 5 &&
             d.col == 1 && d.is_error;
        ok = ok && ParseDiagnostic(
                       "D:/proj/x.cc(12,3) note ignored", &d) ==
                       false &&
             ParseDiagnostic("x.cc:9: warning: w", &d) &&
             d.line == 9 && d.col == 0;
        if (!step("tabs+diag", ok)) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    }

    // 6. tolerated: drive VE.ExtensionsRuntime when it sits beside the
    //    exe (LLVM/JIT absence is reported, never fatal — the contract
    //    above is scaffold+build+list).
    if (!FindSiblingExe("VE.ExtensionsRuntime.exe").empty()) {
        std::string probe;
        std::string manifest_text;
        ManifestInfo mi;
        if (archive->Read("manifest.toml", &manifest_text) &&
            ParseManifestLite(manifest_text, &mi)) {
            probe = mi.probe;
        }
        say("SELFTEST runtime — VE.ExtensionsRuntime found, driving "
            "ext_load (non-fatal)");
        TestExtension(xexrj, probe, say);
    } else {
        say("SELFTEST runtime SKIP — VE.ExtensionsRuntime.exe absent");
    }

    // 7. live-preview selftest: file watcher round trip (hard fail)
    //    + headless CEF availability report (non-fatal when absent).
    {
        std::string report;
        const bool preview_ok = IdePreviewSelftest(&report);
        say("SELFTEST preview — " + report);
        if (!step("preview", preview_ok)) {
            fs::remove_all(scratch, ec);
            return 1;
        }
    }

    fs::remove_all(scratch, ec);
    say("SELFTEST DONE");
    return 0;
}

}  // namespace XER::IdeXejso
