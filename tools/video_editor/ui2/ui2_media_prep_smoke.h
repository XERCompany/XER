// `ui2_media_prep` headless smoke check — registered exe-side from
// main.cc next to `ui2_panels`. Exercises the real media-prep seam
// end to end against a synthesized AV fixture (no widgets):
//   * MediaCatalog::Import probe -> CatalogEntry row (duration/fps/
//     resolution/has_video/has_audio state transitions);
//   * MediaPrep::LookupPoster -> kPending -> worker decode -> kReady
//     BGRA video::Frame (ClipPoster::GetPosterAt, 320px thumb cap);
//   * MediaPrep::LookupPeaks -> kPending -> worker decode -> kReady
//     AudioPeaks (channel-major min/max pairs, non-silent sine);
//   * RequestImportPoster -> catalog-dirty drain -> poster_path row;
//   * RequestProbe -> catalog-dirty + row materializes for a path
//     that was never imported (the project-load warmer path);
//   * render-dirty/catalog-dirty one-shot drains (the VeApp::Tick
//     marshal contract);
//   * negative-cache: a missing file answers kFailed and stays failed,
//     and Options::failed_ttl_seconds expiry re-enqueues exactly once.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2MediaPrepSmoke();

}  // namespace XER::VideoEditor::Ui2
