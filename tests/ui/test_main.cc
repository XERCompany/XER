#include <exception>
#include <iostream>
#include <string_view>

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
