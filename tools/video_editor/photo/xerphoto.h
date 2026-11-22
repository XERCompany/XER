// xerphoto.h — `.xerphoto`, the suite's native photo-document format
// (research/2026-11-21-graphics-editor-stack.md §6, M5 row moved up
// to M2): an 8-byte magic, a little-endian u32 JSON header length,
// the JSON header, then one zlib stream holding every raster layer's
// painted extent as packed RGBA8 rows.
//
//   [8B]  "XERPHOTO" magic
//   [4B]  header JSON byte length (LE)
//   [..]  header JSON:
//         { "format":"xerphoto", "version":1,
//           "canvas":{"w":W,"h":H,"color_space":"sRGB"},
//           "blob":{"uncompressed_size":N},
//           "layers":[ {kind,name,visible,locked,opacity,blend,
//                       children[] | pixels{x,y,w,h,offset,size}} ]}
//   [..]  zlib stream (XER::Common::CompressionCodec) — layer pixel
//         blobs concatenated in document order; each raster layer's
//         pixels field locates its w*h*4-byte extent region.
//
// Layer ids are NOT persisted — load rebuilds the tree with fresh
// ids, matching how OpenPng seeds a Background layer.
#pragma once

#include <memory>
#include <string>

#include "photo/document.h"

namespace XER::VideoEditor::Photo {

// Serializes the document (canvas, working space, full layer tree
// with attributes, raster pixels) to `utf8_path`.
bool SaveDocument(const Document& doc, const std::string& utf8_path,
                  std::string* err = nullptr);

// Rebuilds a document from a `.xerphoto` file. Returns nullptr and
// fills `err` on any structural/corruption failure.
std::unique_ptr<Document> LoadDocument(const std::string& utf8_path,
                                       std::string* err = nullptr);

}  // namespace XER::VideoEditor::Photo
