#include "graph_serializer.h"

#include <cstring>
#include <stdexcept>

// Simple hand-rolled FlatBuffers-like serialization for GraphNode records.
// Format: [4-byte node count] followed by records:
//   [4-byte id][4-byte op_type len][op_type bytes][4-byte name len][name bytes]

namespace XER::ModelsBuilder::GraphEngine {

namespace {
void WriteU32(std::vector<uint8_t>& buf, uint32_t v) {
  uint8_t b[4];
  std::memcpy(b, &v, 4);
  buf.insert(buf.end(), b, b + 4);
}

void WriteString(std::vector<uint8_t>& buf, const std::string& s) {
  WriteU32(buf, static_cast<uint32_t>(s.size()));
  buf.insert(buf.end(), s.begin(), s.end());
}

bool ReadU32(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
  if (pos + 4 > size) return false;
  std::memcpy(&out, data + pos, 4); pos += 4; return true;
}

bool ReadString(const uint8_t* data, size_t size, size_t& pos,
                std::string& out) {
  uint32_t len;
  if (!ReadU32(data, size, pos, len)) return false;
  if (pos + len > size) return false;
  out.assign(reinterpret_cast<const char*>(data + pos), len);
  pos += len; return true;
}
}  // namespace

std::vector<uint8_t> GraphSerializer::Serialize(const GraphBase& graph) {
  std::vector<uint8_t> buf;
  auto& nodes = graph.Nodes();
  WriteU32(buf, static_cast<uint32_t>(nodes.size()));
  for (auto& [id, node] : nodes) {
    WriteU32(buf, static_cast<uint32_t>(id));
    WriteString(buf, node.op_type);
    WriteString(buf, node.name);
  }
  return buf;
}

bool GraphSerializer::Deserialize(const uint8_t* data, size_t size,
                                   GraphBase& out_graph) {
  size_t pos = 0;
  uint32_t count;
  if (!ReadU32(data, size, pos, count)) return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id;
    std::string op_type, name;
    if (!ReadU32(data, size, pos, id)) return false;
    if (!ReadString(data, size, pos, op_type)) return false;
    if (!ReadString(data, size, pos, name)) return false;
    out_graph.AddNode(GraphNode{static_cast<int64_t>(id), op_type, name});
  }
  return true;
}

}  // namespace XER::ModelsBuilder::GraphEngine
