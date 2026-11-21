// media_worker_server.cc — see media_worker_server.h for the contract.
#include "media_worker_server.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <utility>

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

// Shared fallback: a kind the role does not serve gets an explicit
// error reply so a misrouted request fails loudly instead of
// silently doing nothing in the wrong process.
std::string NotImplemented(const MediaWorker::Request& req,
                           const char* what) {
    return MediaWorker::EncodeErrorReply(
        req.id, std::string(what) +
                    " not implemented in this worker role");
}

}  // namespace

int RunJsonWorkerLoop(const char* role, const char* serves,
                      const MediaRequestHandler& handler) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    const std::string role_str =
        (role != nullptr && role[0] != '\0') ? role : "worker";
    const std::string serves_str =
        serves != nullptr ? serves : "ping,info";
    std::string line;
    while (std::getline(std::cin, line)) {
        MediaWorker::Request req;
        std::string reply;
        if (!MediaWorker::DecodeRequest(line, &req)) {
            reply = MediaWorker::EncodeErrorReply(0,
                                                  "malformed request");
        } else {
            switch (req.kind) {
                case MediaWorker::Kind::kPing:
                    reply = MediaWorker::EncodePongReply(req.id,
                                                         role_str);
                    break;
                case MediaWorker::Kind::kInfo:
                    reply = MediaWorker::EncodeInfoReply(
                        req.id, role_str, serves_str);
                    break;
                default:
                    reply = handler(req, line);
                    break;
            }
        }
        std::cout << reply << '\n' << std::flush;
    }
    // EOF = parent closed stdin (shutdown or crash) — exit quietly.
    return 0;
}

int RunMediaWorker(const char* role) {
    return RunJsonWorkerLoop(
        role, "poster,peaks,ping,info",
        [](const MediaWorker::Request& req,
           const std::string&) -> std::string {
            switch (req.kind) {
                case MediaWorker::Kind::kPeaks:
                    return HandlePeaks(req);
                case MediaWorker::Kind::kPosterAt:
                default:
                    return HandlePosterAt(req);
            }
        });
}

int RunStubWorker(const char* role) {
    return RunJsonWorkerLoop(
        role, "ping,info",
        [](const MediaWorker::Request& req,
           const std::string&) -> std::string {
            return NotImplemented(req, "request");
        });
}

}  // namespace XER::VideoEditor
