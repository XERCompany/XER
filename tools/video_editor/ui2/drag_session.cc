#include "drag_session.h"

#include "timeline_surface.h"

namespace XER::VideoEditor::Ui2 {

void BinDragSession::Update(XER::UI::Layout::Point point,
                            TimelineSurface* surface) {
    if (!active_ || surface == nullptr) return;
    const bool over = surface->GetBounds().Contains(point);
    if (over != hovering_) {
        hovering_ = over;
        surface->SetExternalDropHover(over);
    }
}

std::optional<BinDragSession::Drop> BinDragSession::End(
    XER::UI::Layout::Point point, TimelineSurface* surface) {
    if (!active_) return std::nullopt;
    active_ = false;
    std::optional<Drop> out;
    if (surface != nullptr) {
        if (hovering_) {
            surface->SetExternalDropHover(false);
            hovering_ = false;
        }
        if (surface->GetBounds().Contains(point)) {
            Drop drop;
            drop.time = surface->TimeAtDropPoint(point);
            drop.track = surface->TrackAtDropPoint(point);
            out = drop;
        }
    }
    paths_.clear();
    return out;
}

void BinDragSession::Cancel(TimelineSurface* surface) {
    if (surface != nullptr && hovering_) {
        surface->SetExternalDropHover(false);
    }
    hovering_ = false;
    active_ = false;
    paths_.clear();
}

}  // namespace XER::VideoEditor::Ui2
