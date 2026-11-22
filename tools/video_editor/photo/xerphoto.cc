// xerphoto.cc — see xerphoto.h for the on-disk layout.
#include "photo/xerphoto.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <json/json.h>

#include "helper/compression_codec.h"
#include "helper/path_utils.h"

namespace XER::VideoEditor::Photo {

namespace {

constexpr char kMagic[] = "XERPHOTO";  // 8 bytes, no NUL written
constexpr uint32_t kFormatVersion = 2;    // v1 read compat below
constexpr uint32_t kMinReadVersion = 1;

std::string KindName(LayerKind kind) {
    switch (kind) {
        case LayerKind::kGroup: return "group";
        case LayerKind::kRaster: return "raster";
        case LayerKind::kAdjustment: return "adjustment";
    }
    return "raster";
}

// Serializes a coverage device (A8 mask) into `blob` as w*h*1-byte
// rows over its extent; writes the locator record into `px`.
void SerializeDeviceCoverage(const TiledDevice& dev, Json::Value* px,
                             std::string* blob) {
    const IntRect ext = dev.Extent();
    (*px)["x"] = ext.x;
    (*px)["y"] = ext.y;
    (*px)["w"] = ext.w;
    (*px)["h"] = ext.h;
    (*px)["offset"] = static_cast<Json::UInt64>(blob->size());
    if (!ext.IsEmpty()) {
        const std::vector<uint8_t> region =
            dev.ReadRegionCoverage(ext);
        blob->append(reinterpret_cast<const char*>(region.data()),
                     region.size());
    }
    (*px)["size"] = static_cast<Json::UInt64>(
        blob->size() - static_cast<size_t>((*px)["offset"].asUInt64()));
}

// Appends one node's JSON record; raster extents are packed into
// `blob` (RGBA8, rect.w*4-byte rows) and mask extents as A8
// (rect.w*1-byte rows), both located by offset/size.
void SerializeNode(const LayerNode& node, Json::Value* out,
                   std::string* blob) {
    Json::Value j(Json::objectValue);
    j["kind"] = KindName(node.Kind());
    j["name"] = node.Name();
    j["visible"] = node.Visible();
    j["locked"] = node.Locked();
    j["opacity"] = static_cast<double>(node.Opacity());
    j["blend"] = node.BlendMode();
    if (node.Kind() == LayerKind::kRaster) {
        Json::Value px(Json::objectValue);
        const TiledDevice* dev = node.Device();
        const IntRect ext =
            dev != nullptr ? dev->Extent() : IntRect{};
        px["x"] = ext.x;
        px["y"] = ext.y;
        px["w"] = ext.w;
        px["h"] = ext.h;
        px["offset"] = static_cast<Json::UInt64>(blob->size());
        if (!ext.IsEmpty()) {
            const std::vector<uint8_t> region = dev->ReadRegion(ext);
            blob->append(reinterpret_cast<const char*>(region.data()),
                         region.size());
        }
        px["size"] = static_cast<Json::UInt64>(
            blob->size() - static_cast<size_t>(px["offset"].asUInt64()));
        j["pixels"] = px;
    }
    if (node.Kind() == LayerKind::kAdjustment) {
        Json::Value adj(Json::objectValue);
        adj["op"] = node.Adjustment().op;
        Json::Value params(Json::arrayValue);
        for (const AdjustmentParam& p : node.Adjustment().params) {
            Json::Value pj(Json::objectValue);
            pj["name"] = p.name;
            pj["value"] = p.value;
            params.append(pj);
        }
        adj["params"] = params;
        j["adjustment"] = adj;
    }
    if (node.Mask() != nullptr) {
        Json::Value mk(Json::objectValue);
        SerializeDeviceCoverage(*node.Mask(), &mk, blob);
        j["mask"] = mk;
    }
    if (!node.Children().empty()) {
        Json::Value kids(Json::arrayValue);
        for (const auto& child : node.Children()) {
            Json::Value cj(Json::objectValue);
            SerializeNode(*child, &cj, blob);
            kids.append(cj);
        }
        j["children"] = kids;
    }
    *out = j;
}

uint32_t ReadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void WriteU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

bool GetStr(const Json::Value& v, const char* key, std::string* out) {
    if (!v.isObject() || !v.isMember(key) || !v[key].isString()) {
        return false;
    }
    *out = v[key].asString();
    return true;
}

// Rebuilds one node (and descendants) under `parent`; raster pixels
// are read back from the uncompressed blob.
bool BuildNode(Document& doc, LayerNode* parent, const Json::Value& j,
               const std::string& blob, std::string* err) {
    std::string kind;
    if (!GetStr(j, "kind", &kind)) {
        if (err) *err = "layer record missing kind";
        return false;
    }
    LayerKind lk = LayerKind::kRaster;
    if (kind == "group") lk = LayerKind::kGroup;
    if (kind == "adjustment") lk = LayerKind::kAdjustment;
    LayerNode& node = doc.AddLayer(lk, "", parent);
    node.SetName(j.get("name", "").asString());
    node.SetVisible(j.get("visible", true).asBool());
    node.SetLocked(j.get("locked", false).asBool());
    node.SetOpacity(
        static_cast<float>(j.get("opacity", 1.0).asDouble()));
    node.SetBlendMode(j.get("blend", "srcOver").asString());

    // v2: adjustment op + params.
    if (lk == LayerKind::kAdjustment && j["adjustment"].isObject()) {
        const Json::Value& adj = j["adjustment"];
        AdjustmentSpec spec;
        spec.op = adj.get("op", "").asString();
        const Json::Value& params = adj["params"];
        if (params.isArray()) {
            for (Json::ArrayIndex i = 0; i < params.size(); ++i) {
                spec.params.push_back(
                    {params[i].get("name", "").asString(),
                     params[i].get("value", 0.0).asDouble()});
            }
        }
        node.SetAdjustment(spec);
    }

    // v2: A8 mask extent (coverage bytes in the shared blob).
    if (j["mask"].isObject()) {
        const Json::Value& mk = j["mask"];
        const int mx = mk.get("x", 0).asInt();
        const int my = mk.get("y", 0).asInt();
        const int mw = mk.get("w", 0).asInt();
        const int mh = mk.get("h", 0).asInt();
        const uint64_t moff = mk.get("offset", 0).asUInt64();
        const uint64_t msize = mk.get("size", 0).asUInt64();
        if (mw < 0 || mh < 0 ||
            msize !=
                static_cast<uint64_t>(mw) * static_cast<uint64_t>(mh) ||
            moff + msize > blob.size()) {
            if (err) *err = "mask record out of range";
            return false;
        }
        auto mask = std::make_unique<TiledDevice>(
            doc.CanvasWidth(), doc.CanvasHeight(),
            image::PixelFormat::Gray8, Pixel{0, 0, 0, 255});
        if (mw > 0 && mh > 0) {
            mask->WriteRegionCoverage(
                IntRect{mx, my, mw, mh},
                reinterpret_cast<const uint8_t*>(blob.data() + moff));
        }
        node.SetMask(std::move(mask));
    }

    if (kind == "group") {
        const Json::Value& kids = j["children"];
        if (!kids.isNull() && !kids.isArray()) {
            if (err) *err = "group children not an array";
            return false;
        }
        for (Json::ArrayIndex i = 0; i < kids.size(); ++i) {
            if (!BuildNode(doc, &node, kids[i], blob, err)) {
                return false;
            }
        }
        return true;
    }
    if (kind == "adjustment") return true;
    if (kind != "raster") {
        if (err) *err = "unknown layer kind: " + kind;
        return false;
    }
    const Json::Value& px = j["pixels"];
    if (!px.isObject()) return true;  // empty raster layer
    const int x = px.get("x", 0).asInt();
    const int y = px.get("y", 0).asInt();
    const int w = px.get("w", 0).asInt();
    const int h = px.get("h", 0).asInt();
    const uint64_t offset = px.get("offset", 0).asUInt64();
    const uint64_t size = px.get("size", 0).asUInt64();
    if (w < 0 || h < 0 ||
        size != static_cast<uint64_t>(w) * static_cast<uint64_t>(h) *
                    4 ||
        offset + size > blob.size()) {
        if (err) *err = "raster pixel record out of range";
        return false;
    }
    if (w > 0 && h > 0) {
        node.Device()->WriteRegion(
            IntRect{x, y, w, h},
            reinterpret_cast<const uint8_t*>(blob.data() + offset));
    }
    return true;
}

}  // namespace

bool SaveDocument(const Document& doc, const std::string& utf8_path,
                  std::string* err) {
    std::string blob;
    Json::Value layers(Json::arrayValue);
    for (const auto& child : doc.Root().Children()) {
        Json::Value cj(Json::objectValue);
        SerializeNode(*child, &cj, &blob);
        layers.append(cj);
    }

    const auto compressed = XER::Common::CompressionCodec::Compress(blob);
    if (!compressed.has_value()) {
        if (err) *err = "zlib compression failed";
        return false;
    }

    Json::Value root(Json::objectValue);
    root["format"] = "xerphoto";
    root["version"] = kFormatVersion;
    Json::Value canvas(Json::objectValue);
    canvas["w"] = doc.CanvasWidth();
    canvas["h"] = doc.CanvasHeight();
    canvas["color_space"] =
        image::ColorSpaceName(doc.WorkingSpace());
    root["canvas"] = canvas;
    Json::Value blobj(Json::objectValue);
    blobj["uncompressed_size"] =
        static_cast<Json::UInt64>(blob.size());
    root["blob"] = blobj;
    root["layers"] = layers;

    Json::StreamWriterBuilder wb;
    const std::string header = Json::writeString(wb, root);

    std::ofstream f(XER::Helper::Path::FromUtf8(utf8_path),
                    std::ios::binary | std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot open file for writing";
        return false;
    }
    uint8_t len_le[4];
    WriteU32LE(len_le, static_cast<uint32_t>(header.size()));
    f.write(kMagic, sizeof(kMagic) - 1);
    f.write(reinterpret_cast<const char*>(len_le), 4);
    f.write(header.data(), static_cast<std::streamsize>(header.size()));
    if (!compressed->empty()) {
        f.write(reinterpret_cast<const char*>(compressed->data()),
                static_cast<std::streamsize>(compressed->size()));
    }
    if (!f) {
        if (err) *err = "write failed";
        return false;
    }
    return true;
}

std::unique_ptr<Document> LoadDocument(const std::string& utf8_path,
                                       std::string* err) {
    const auto fail = [err](const char* msg) {
        if (err) *err = msg;
        return std::unique_ptr<Document>();
    };
    std::ifstream f(XER::Helper::Path::FromUtf8(utf8_path),
                    std::ios::binary);
    if (!f) return fail("cannot open file");
    const std::vector<uint8_t> bytes{
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()};
    if (bytes.size() < 12 ||
        std::memcmp(bytes.data(), kMagic, sizeof(kMagic) - 1) != 0) {
        return fail("not a .xerphoto file");
    }
    const uint32_t header_len = ReadU32LE(bytes.data() + 8);
    if (12ull + header_len > bytes.size()) {
        return fail("truncated header");
    }
    const char* hb =
        reinterpret_cast<const char*>(bytes.data() + 12);
    Json::Value root;
    {
        Json::CharReaderBuilder rb;
        std::string perr;
        std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
        if (!reader->parse(hb, hb + header_len, &root, &perr)) {
            return fail("bad header JSON");
        }
    }
    if (!root.isObject() ||
        root.get("format", "").asString() != "xerphoto") {
        return fail("unsupported .xerphoto version");
    }
    const uint32_t version = root.get("version", 0).asUInt();
    if (version < kMinReadVersion || version > kFormatVersion) {
        return fail("unsupported .xerphoto version");
    }
    const Json::Value& canvas = root["canvas"];
    const int w = canvas.get("w", 0).asInt();
    const int h = canvas.get("h", 0).asInt();
    if (w <= 0 || h <= 0) return fail("bad canvas size");

    const uint64_t blob_size =
        root["blob"].get("uncompressed_size", 0).asUInt64();
    const std::vector<uint8_t> comp(
        bytes.begin() + 12 + header_len, bytes.end());
    const auto blob =
        XER::Common::CompressionCodec::DecompressToString(comp);
    if (!blob.has_value() ||
        static_cast<uint64_t>(blob->size()) != blob_size) {
        return fail("blob decompress failed / size mismatch");
    }

    auto doc = std::make_unique<Document>(
        w, h, image::ColorSpaceFromName(
                  canvas.get("color_space", "sRGB").asString()));
    const Json::Value& layers = root["layers"];
    if (!layers.isNull() && layers.isArray()) {
        for (Json::ArrayIndex i = 0; i < layers.size(); ++i) {
            if (!BuildNode(*doc, &doc->Root(), layers[i], *blob, err)) {
                return nullptr;
            }
        }
    }
    return doc;
}

}  // namespace XER::VideoEditor::Photo
