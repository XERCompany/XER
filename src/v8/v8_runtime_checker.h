/**
 * @file v8_runtime_checker.h
 * @brief Проверяет совместимость окружения с V8 и анализирует конфликты.
 * @author Yoshi Arasaka (main)
 * @date 2023-10-27
 * @copyright MIT License
 */

#pragma once

#include <string>
#include <vector>
#include <map>

namespace XER {

class V8RuntimeChecker {
public:
    V8RuntimeChecker() = default;
    ~V8RuntimeChecker() = default;

    /// Checks installed libraries for V8 conflicts.
    /// Returns names of conflicting libraries.
    std::vector<std::string> CheckV8Compatibility(
        const std::vector<std::string>& installed_libs);

private:
    std::vector<std::pair<std::string, std::string>> known_v8_conflicts_;

    void LoadKnownConflicts();
};

} // namespace XER

