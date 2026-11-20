// media_worker_server.cc — see media_worker_server.h for the contract.
#include "media_worker_server.h"

#include <cstdio>
#include <iostream>
#include <string>

#include "frame.h"
#include "media_catalog.h"
#include "media_worker_protocol.h"

namespace XER::VideoEditor {
namespace {

// One decoded frame → reply line; the frame carries packed BGRA8
// (ClipPoster caps the long edge at 320 px upstream).
std::string HandlePosterAt(const MediaWorker::Request& req) {
    video::Frame frame;
    std::string err;
    if (!MediaCatalog::GetPosterAt(req.path, req.seconds, &frame,
                                   &err)) {
        return MediaWorker::EncodeErrorReply(
            req.id, err.empty() ? "poster decode failed" : err);
    }
    if (frame.Format() != video::PixelFormat::BGRA8) {
        return MediaWorker::EncodeErrorReply(req.id,
                                             "unexpected pixel format");
    }
    return MediaWorker::EncodePosterReply(req.id, frame);
}

std::string HandlePeaks(const MediaWorker::Request& req) {
    AudioPeaks peaks;
    std::string err;
    if (!DecodeAudioPeaks(req.path, &peaks, &err)) {
        return MediaWorker::EncodeErrorReply(
            req.id, err.empty() ? "peaks decode failed" : err);
    }
    return MediaWorker::EncodePeaksReply(req.id, peaks);
}

}  // namespace

int RunMediaWorker() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::string line;
    while (std::getline(std::cin, line)) {
        MediaWorker::Request req;
        std::string reply;
        if (MediaWorker::DecodeRequest(line, &req)) {
            reply = req.kind == MediaWorker::Kind::kPeaks
                        ? HandlePeaks(req)
                        : HandlePosterAt(req);
        } else {
            reply = MediaWorker::EncodeErrorReply(0,
                                                  "malformed request");
        }
        std::cout << reply << '\n' << std::flush;
    }
    // EOF = parent closed stdin (shutdown or crash) — exit quietly.
    return 0;
}

}  // namespace XER::VideoEditor
