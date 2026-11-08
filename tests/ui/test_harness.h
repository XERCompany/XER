#ifndef XER_UI_TESTS_UI_TEST_HARNESS_H
#define XER_UI_TESTS_UI_TEST_HARNESS_H

#include <iostream>
#include <string_view>
#include <vector>

namespace XER::UI::Test {

using TestFunction = void (*)();

struct TestCase {
    std::string_view name;
    TestFunction function;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& FailureCount() {
    static int failures = 0;
    return failures;
}

class Registrar {
public:
    Registrar(std::string_view name, TestFunction function) {
        Registry().push_back({name, function});
    }
};

inline void Expect(bool condition, std::string_view expression, const char* file, int line) {
    if (condition) {
        return;
    }

    ++FailureCount();
    std::cerr << file << ':' << line << ": assertion failed: " << expression << '\n';
}

template <typename Expected, typename Actual>
void ExpectEqual(
    const Expected& expected,
    const Actual& actual,
    std::string_view expression,
    const char* file,
    int line
) {
    Expect(expected == actual, expression, file, line);
}

} // namespace XER::UI::Test

#define XER_UI_TEST(name) \
    static void name(); \
    static const ::XER::UI::Test::Registrar name##_registrar{#name, &name}; \
    static void name()

#define XER_UI_EXPECT_TRUE(condition) \
    ::XER::UI::Test::Expect(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

#define XER_UI_EXPECT_FALSE(condition) \
    ::XER::UI::Test::Expect(!static_cast<bool>(condition), "!(" #condition ")", __FILE__, __LINE__)

#define XER_UI_EXPECT_EQ(expected, actual) \
    ::XER::UI::Test::ExpectEqual((expected), (actual), #expected " == " #actual, __FILE__, __LINE__)

#endif // XER_UI_TESTS_UI_TEST_HARNESS_H
