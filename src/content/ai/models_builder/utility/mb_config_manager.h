#pragma once

#include <string>
#include <unordered_map>
#include <variant>

namespace XER::ModelsBuilder::Utility {

using ConfigValue = std::variant<bool, int, float, std::string>;

/// @brief Singleton key-value configuration store for models_builder subsystems
class MbConfigManager {
 public:
  static MbConfigManager& Instance();

  void   Set(const std::string& key, ConfigValue value);
  bool   GetBool(const std::string& key, bool def = false)    const;
  int    GetInt(const std::string& key, int def = 0)          const;
  float  GetFloat(const std::string& key, float def = 0.0f)   const;
  std::string GetString(const std::string& key,
                         const std::string& def = {}) const;
  bool   Has(const std::string& key) const;

 private:
  MbConfigManager() = default;
  std::unordered_map<std::string, ConfigValue> store_;
};

}  // namespace XER::ModelsBuilder::Utility
