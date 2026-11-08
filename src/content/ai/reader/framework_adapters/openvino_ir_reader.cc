#include "openvino_ir_reader.h"

#include "../vino_stack/vino_bin_reader.h"
#include "../vino_stack/vino_xml_reader.h"

namespace XER::ModelsBuilder::Reader::Framework {

namespace {

std::string XmlToBinPath(const std::string& xml_path) {
  if (xml_path.size() < 4 || xml_path.substr(xml_path.size() - 4) != ".xml") {
    return xml_path + ".bin";
  }
  return xml_path.substr(0, xml_path.size() - 4) + ".bin";
}

}  // namespace

LoadResult OpenVinoIrReader::Load(const std::string& filepath) {
  Vino::VinoXmlReader xml_reader;
  if (!xml_reader.ParseFile(filepath)) {
    return {nullptr, false, "Failed to parse OpenVINO XML: " + filepath};
  }

  Vino::VinoBinReader bin_reader;
  const std::string bin_path = XmlToBinPath(filepath);
  if (!bin_reader.LoadFile(bin_path)) {
    return {nullptr, false, "Failed to read OpenVINO BIN: " + bin_path};
  }

  auto model = xml_reader.BuildModel(bin_reader);
  if (!model) {
    return {nullptr, false, "OpenVINO conversion returned null model"};
  }

  if (!model->Build({1U, 128U})) {
    return {nullptr, false, "OpenVINO converted model build failed"};
  }
  if (!model->Compile()) {
    return {nullptr, false, "OpenVINO converted model compile failed"};
  }

  return {model, true, ""};
}

bool OpenVinoIrReader::CanLoad(const std::string& filepath) const {
  return filepath.size() >= 4 && filepath.substr(filepath.size() - 4) == ".xml";
}

}  // namespace XER::ModelsBuilder::Reader::Framework
