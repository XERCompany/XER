#include "mb_config_manager.h"

namespace XER::ModelsBuilder::Utility {

MbConfigManager& MbConfigManager::Instance() {
  static MbConfigManager inst;
  return inst;
}

void MbConfigManager::Set(const std::string& key, ConfigValue value) {
  store_[key] = std::move(value);
}

bool MbConfigManager::GetBool(const std::string& key, bool def) const {
  auto it = store_.find(key);
  if (it == store_.end()) return def;
  if (auto* v = std::get_if<bool>(&it->second)) return *v;
  return def;
}

int MbConfigManager::GetInt(const std::string& key, int def) const {
  auto it = store_.find(key);
  if (it == store_.end()) return def;
  if (auto* v = std::get_if<int>(&it->second)) return *v;
  return def;
}

float MbConfigManager::GetFloat(const std::string& key, float def) const {
  auto it = store_.find(key);
  if (it == store_.end()) return def;
  if (auto* v = std::get_if<float>(&it->second)) return *v;
  return def;
}

std::string MbConfigManager::GetString(const std::string& key,
                                         const std::string& def) const {
  auto it = store_.find(key);
  if (it == store_.end()) return def;
  if (auto* v = std::get_if<std::string>(&it->second)) return *v;
  return def;
}

bool MbConfigManager::Has(const std::string& key) const {
  return store_.count(key) > 0;
}

}  // namespace XER::ModelsBuilder::Utility
