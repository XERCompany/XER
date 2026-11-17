/**
 * @file v8_runtime_checker.cc
 * @brief Реализация проверки совместимости с V8.
 * @author Yoshi Arasaka (main)
 * @date 2023-10-27
 * @copyright MIT License
 */

#include "v8_integration/v8_runtime_checker.h"
#include "core/logger.h"

namespace XER {

V8RuntimeChecker::V8RuntimeChecker(Context& context) : context_(context) {
    load_known_v8_conflicts();
}

void V8RuntimeChecker::load_known_v8_conflicts() {
    Logger::debug("V8RuntimeChecker: Loading known V8 conflicts.");
    // Загрузка списка известных конфликтующих библиотек
    // Это может быть загрузка из файла конфигурации или жестко задано
    known_v8_conflicts_ = {
        {"libssl", "1.0.x"}, // Пример: libssl версии 1.0.x может конфликтовать
        {"libcrypto", "1.0.x"}, // Пример: libcrypto версии 1.0.x может конфликтовать
        {"zlib", "< 1.2.8"} // Пример: zlib ниже версии 1.2.8 может вызывать проблемы
        // ... другие известные конфликты
    };
    Logger::debug("V8RuntimeChecker: Loaded %zu known V8 conflicts.", known_v8_conflicts_.size());
}

void V8RuntimeChecker::check_v8_compatibility(
    const std::vector<LibraryDependencyInfo>& installed_libs,
    std::vector<std::string>& v8_conflicts
) {
    Logger::info("V8RuntimeChecker: Checking V8 compatibility with installed libraries.");

    check_for_specific_conflicts(installed_libs, v8_conflicts);

    // Дополнительные проверки, например, версии самого V8, если он установлен как зависимость
    // ...

    if (!v8_conflicts.empty()) {
        Logger::warning("V8RuntimeChecker: Detected %zu potential conflicts with V8.", v8_conflicts.size());
    } else {
        Logger::info("V8RuntimeChecker: No known V8 conflicts detected.");
    }
}

void V8RuntimeChecker::check_for_specific_conflicts(
    const std::vector<LibraryDependencyInfo>& installed_libs,
    std::vector<std::string>& v8_conflicts
) {
    Logger::debug("V8RuntimeChecker: Checking for specific conflicts...");
    for (const auto& known_conflict : known_v8_conflicts_) {
        const std::string& conflict_name = known_conflict.first;
        const std::string& conflict_version_spec = known_conflict.second;

        for (const auto& installed_lib : installed_libs) {
            if (installed_lib.name == conflict_name) {
                // Здесь нужна сложная логика сравнения версий, которая зависит от format conflict_version_spec
                // Для простоты, предполагаем, что любая версия конфликтующей библиотеки является проблемой
                // В реальном коде нужно парсить conflict_version_spec (например, ">=1.0,<2.0")
                Logger::debug("V8RuntimeChecker: Found installed library %s, checking version spec %s.", installed_lib.name.c_str(), conflict_version_spec.c_str());
                
                // Пример упрощенной проверки:
                bool version_matches = false;
                if (conflict_version_spec == "1.0.x" && installed_lib.version.rfind("1.0.", 0) == 0) {
                    version_matches = true;
                }
                // ... более сложная логика сравнения версий ...

                if (version_matches) {
                    std::string conflict_desc = installed_lib.name + " (version " + installed_lib.version + ") - known conflict with V8";
                    v8_conflicts.push_back(conflict_desc);
                    Logger::warning("%s", conflict_desc.c_str());
                }
            }
        }
    }
}

} // namespace XER
