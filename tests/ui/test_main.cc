#include <exception>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI CrashTrace(EXCEPTION_POINTERS* e) {
    void* addr = e->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    std::fprintf(stderr, "\n[CRASH] code=0x%08lx addr=%p\n",
                 e->ExceptionRecord->ExceptionCode, addr);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &mod)) {
        char name[MAX_PATH]{};
        GetModuleFileNameA(mod, name, MAX_PATH);
        std::fprintf(stderr, "[CRASH] module=%s rva=%p\n", name,
                     reinterpret_cast<char*>(addr) -
                         reinterpret_cast<char*>(mod));
    }
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    void* stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);
    for (USHORT i = 0; i < frames; ++i) {
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = MAX_SYM_NAME;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, reinterpret_cast<DWORD64>(stack[i]), &disp,
                        sym)) {
            HMODULE smod = nullptr;
            char mname[MAX_PATH]{};
            if (GetModuleHandleExA(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(stack[i]), &smod)) {
                GetModuleFileNameA(smod, mname, MAX_PATH);
            }
            std::fprintf(stderr, "  #%u %s!%s+0x%llx\n", i, mname, sym->Name,
                         disp);
        }
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#include "colorspace/color_space.h"
#include "test_harness.h"

XER_UI_TEST(color_space_name_is_srgb) {
    XER_UI_EXPECT_EQ(
        std::string_view{"sRGB"},
        XER::UI::ColorSpace::ColorSpaceToString(XER::UI::ColorSpace::ColorSpace::sRGB)
    );
}

XER_UI_TEST(sc_rgb_is_hdr) {
    XER_UI_EXPECT_TRUE(XER::UI::ColorSpace::IsHDR(XER::UI::ColorSpace::ColorSpace::ScRGB));
    XER_UI_EXPECT_FALSE(XER::UI::ColorSpace::IsHDR(XER::UI::ColorSpace::ColorSpace::sRGB));
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashTrace);
#endif
    std::string_view filter;
    if (argc == 3 && std::string_view{argv[1]} == "--filter") {
        filter = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: XER_UI_Tests [--filter <name-substring>]\n";
        return 2;
    }

    std::size_t selected = 0;
    for (const auto& test : XER::UI::Test::Registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string_view::npos) {
            continue;
        }

        ++selected;
        const int failures_before = XER::UI::Test::FailureCount();
        try {
            test.function();
        } catch (const std::exception& exception) {
            std::cerr << test.name << ": uncaught exception: " << exception.what() << '\n';
            ++XER::UI::Test::FailureCount();
        } catch (...) {
            std::cerr << test.name << ": uncaught non-standard exception\n";
            ++XER::UI::Test::FailureCount();
        }

        if (XER::UI::Test::FailureCount() == failures_before) {
            std::cout << "[PASS] " << test.name << '\n';
        } else {
            std::cerr << "[FAIL] " << test.name << '\n';
        }
    }

    if (selected == 0) {
        std::cerr << "No UI tests matched filter: " << filter << '\n';
        return 2;
    }

    std::cout << selected << " UI test(s), " << XER::UI::Test::FailureCount()
              << " failure(s)\n";
    return XER::UI::Test::FailureCount() == 0 ? 0 : 1;
}
