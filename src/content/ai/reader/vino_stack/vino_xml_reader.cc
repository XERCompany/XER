#include "vino_xml_reader.h"

#include <fstream>
#include <sstream>

namespace XER::ModelsBuilder::Reader::Vino {

bool VinoXmlReader::ParseFile(const std::string& xml_path) {
  std::ifstream file(xml_path);
  if (!file.is_open()) return false;

  std::ostringstream oss;
  oss << file.rdbuf();
  return ParseString(oss.str());
}

bool VinoXmlReader::ParseString(const std::string& xml_content) {
  layers_.clear();

  // Minimal hand-rolled XML parser — real implementation should use
  // pugixml or libxml2 for production robustness.
  // Extract model name from <net name="...">
  auto name_pos = xml_content.find("name=\"");
  if (name_pos != std::string::npos) {
    name_pos += 6;
    auto end = xml_content.find('"', name_pos);
    if (end != std::string::npos)
      model_name_ = xml_content.substr(name_pos, end - name_pos);
  }

  // Extract <layer> blocks
  size_t pos = 0;
  while ((pos = xml_content.find("<layer ", pos)) != std::string::npos) {
    size_t end = xml_content.find("</layer>", pos);
    if (end == std::string::npos) break;
    end += 8;
    ParseLayerElement(xml_content.substr(pos, end - pos));
    pos = end;
  }

  return !layers_.empty();
}

void VinoXmlReader::ParseLayerElement(const std::string& block) {
  VinoLayerInfo info;

  // Extract id="..."
  auto extract_attr = [&](const std::string& attr) -> std::string {
    std::string key = attr + "=\"";
    auto pos = block.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    auto end = block.find('"', pos);
    return end != std::string::npos ? block.substr(pos, end - pos) : "";
  };

  info.id   = extract_attr("id");
  info.name = extract_attr("name");
  info.type = extract_attr("type");

  // Parse <data> attributes
  auto data_pos = block.find("<data ");
  if (data_pos != std::string::npos) {
    size_t data_end = block.find('>', data_pos);
    std::string data_block = block.substr(data_pos, data_end - data_pos);
    // Extract all attr="value" pairs from <data>
    size_t p = 0;
    while ((p = data_block.find('=', p)) != std::string::npos) {
      size_t kstart = data_block.rfind(' ', p);
      if (kstart == std::string::npos) { ++p; continue; }
      std::string key = data_block.substr(kstart + 1, p - kstart - 1);
      if (data_block[p + 1] != '"') { ++p; continue; }
      size_t vstart = p + 2;
      size_t vend   = data_block.find('"', vstart);
      if (vend == std::string::npos) break;
      info.attrs[key] = data_block.substr(vstart, vend - vstart);
      p = vend + 1;
    }
  }

  if (!info.type.empty()) layers_.push_back(std::move(info));
}

std::shared_ptr<Core::Model> VinoXmlReader::BuildModel(
    const VinoBinReader& bin_reader) {
  VinoIrConverter converter;
  return converter.Convert(layers_, bin_reader);
}

}  // namespace XER::ModelsBuilder::Reader::Vino
