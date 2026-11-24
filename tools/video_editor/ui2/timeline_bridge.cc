// TimelineBridge — TimelineHooks -> TimelineHistory translation.
// Every method mirrors the legacy VideoEditorAppWidget handler of
// the same name (ui/video_editor_app.cc): same op order, same macro
// boundaries, same status strings, so undo labels and failure
// behavior match the old shell.

#include "timeline_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "media_catalog.h"

namespace XER::VideoEditor::Ui2 {

TimelineBridge::TimelineBridge(Project& project,
                               TimelineHistory& history, Host host)
    : project_(project),
      history_(history),
      host_(std::move(host)) {
    BuildHooks();
}

void TimelineBridge::SetHost(Host host) {
    host_ = std::move(host);
    BuildHooks();
}

void TimelineBridge::SetTransitionDefaults(Transition::Type type,
                                           double duration,
                                           std::string script) {
    transition_type_ = type;
    transition_duration_ = duration;
    transition_script_ = std::move(script);
}

void TimelineBridge::BuildHooks() {
    hooks_ = TimelineHooks{};
    TimelineBridge* self = this;

    hooks_.on_select_clip = [self](const std::string& id) {
        self->SelectClip(id);
    };
    hooks_.on_seek = [self](double t) { self->Seek(t); };
    hooks_.on_move_clip = [self](const std::string& id, double t) {
        self->MoveClip(id, t);
    };
    hooks_.on_trim_clip =
        [self](const std::string& id, TrimEdge edge, double point) {
            self->TrimClip(id, edge, point);
        };
    hooks_.on_ripple_trim =
        [self](const std::string& id, TrimEdge edge, double point) {
            self->RippleTrimClip(id, edge, point);
        };
    hooks_.on_delete_clip = [self](const std::string& id) {
        self->DeleteClip(id);
    };
    hooks_.on_ripple_delete = [self](const std::string& id) {
        self->RippleDeleteClipById(id);
    };
    hooks_.on_split_clip = [self](const std::string& id, double t) {
        self->SplitClip(id, t);
    };
    hooks_.on_rolling_edit =
        [self](const std::string& from, const std::string& to,
               double delta) {
            self->RollingEditClips(from, to, delta);
        };
    hooks_.on_slip_clip = [self](const std::string& id, double d) {
        self->SlipClipBy(id, d);
    };
    hooks_.on_slide_clip = [self](const std::string& id, double d) {
        self->SlideClipBy(id, d);
    };
    hooks_.on_rate_stretch = [self](const std::string& id, double r) {
        self->SetClipRateBy(id, r);
    };
    hooks_.on_add_transition =
        [self](const std::string& id, Transition::Type type,
               double duration, const std::string& script) {
            self->AddTransitionAfter(id, type, duration, script);
        };
    hooks_.on_set_transition_duration =
        [self](const std::string& from, const std::string& to,
               double duration) {
            self->SetTransitionDurationBy(from, to, duration);
        };
    hooks_.on_add_marker = [self](double t, const std::string& name) {
        self->AddMarkerAt(t, name);
    };
    hooks_.on_remove_marker = [self](double t) {
        self->RemoveMarkerAt(t);
    };
    hooks_.on_set_work_area = [self](double in, double out) {
        self->SetWorkAreaRange(in, out);
    };
    hooks_.on_track_mute = [self](int track, bool on) {
        self->SetTrackFlag(track, 'm', on);
    };
    hooks_.on_track_hidden = [self](int track, bool on) {
        self->SetTrackFlag(track, 'h', on);
    };
    hooks_.on_track_locked = [self](int track, bool on) {
        self->SetTrackFlag(track, 'l', on);
    };
    hooks_.on_status = [self](const std::string& text, bool err) {
        self->SetStatus(text, err);
    };

    // Media caches — wire to the host's async pool (MediaPrep) or
    // answer the non-blocking kFailed default so paint never
    // stalls on decode.
    hooks_.get_audio_peaks =
        [self](const std::string& path, AudioPeaks* out)
        -> MediaLookup {
        if (self->host_.audio_peaks != nullptr) {
            return self->host_.audio_peaks(path, out);
        }
        return MediaLookup::kFailed;
    };
    hooks_.get_poster =
        [self](const std::string& path, double t, video::Frame* out)
        -> MediaLookup {
        if (self->host_.posters != nullptr) {
            return self->host_.posters(path, t, out);
        }
        return MediaLookup::kFailed;
    };
    hooks_.get_media_bounds =
        [self](const std::string& path, MediaBounds* out) -> bool {
        if (self->host_.media_bounds != nullptr) {
            return self->host_.media_bounds(path, out);
        }
        *out = self->BoundsForPath(path);
        return true;
    };

    // Snap toggle requests — the shell owns the flag.
    hooks_.on_snap_toggle = [self](bool enabled) {
        if (self->host_.on_snap_toggle != nullptr) {
            self->host_.on_snap_toggle(enabled);
        }
    };

    // Gesture bracketing -> one history macro per gesture (the
    // hooks contract's documented shell-side requirement).
    hooks_.on_gesture_begin = [self](const char* label) {
        self->history_.BeginMacro(label != nullptr ? label : "edit");
    };
    hooks_.on_gesture_end = [self] { self->history_.EndMacro(); };

    // Optional seams — bind ONLY when the host provides them so the
    // surface's context menu stays collapsed on unwired entries.
    if (host_.add_generated) hooks_.on_add_generated = host_.add_generated;
    if (host_.add_media) hooks_.on_add_media = host_.add_media;
    if (host_.ai_find_objects) {
        hooks_.on_ai_find_objects = host_.ai_find_objects;
    }
    if (host_.ai_clear_tags) hooks_.on_ai_clear_tags = host_.ai_clear_tags;
    if (host_.smart_reframe) hooks_.on_smart_reframe = host_.smart_reframe;
    if (host_.track_object) hooks_.on_track_object = host_.track_object;
    if (host_.pin_title) hooks_.on_pin_title = host_.pin_title;
    if (host_.pin_mosaic) hooks_.on_pin_mosaic = host_.pin_mosaic;
    if (host_.clear_track) hooks_.on_clear_track = host_.clear_track;
    if (host_.face_blur) hooks_.on_face_blur = host_.face_blur;
    if (host_.track_mask) hooks_.on_track_mask = host_.track_mask;
    if (host_.clear_mask) hooks_.on_clear_mask = host_.clear_mask;
    if (host_.ai_slowmo) hooks_.on_ai_slowmo = host_.ai_slowmo;
    if (host_.edit_transcript) {
        hooks_.on_edit_transcript = host_.edit_transcript;
    }
}

MediaBounds TimelineBridge::BoundsForPath(
    const std::string& path) const {
    if (host_.bounds_for_path != nullptr) {
        return host_.bounds_for_path(path);
    }
    // No catalog seam — unbounded, the legacy catalog-miss answer.
    return {};
}

void TimelineBridge::SetStatus(const std::string& text,
                               bool is_error) {
    if (host_.on_status) host_.on_status(text, is_error);
}

void TimelineBridge::NotifyProjectChanged() {
    if (host_.on_project_changed) host_.on_project_changed();
}

void TimelineBridge::SelectClip(const std::string& clip_id) {
    selected_clip_id_ = clip_id;
    if (host_.on_select) host_.on_select(clip_id);
    SetStatus(clip_id.empty() ? "Selection cleared"
                              : "Selected " + clip_id);
}

void TimelineBridge::Seek(double seconds) {
    playhead_ = std::clamp(seconds, 0.0, project_.duration());
    if (host_.on_seek) host_.on_seek(playhead_);
}

void TimelineBridge::MoveClip(const std::string& clip_id,
                              double start) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.MoveClip(clip_id, start,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Move failed: " + error, true);
    } else {
        SetStatus("Moved clip");
    }
    NotifyProjectChanged();
}

void TimelineBridge::TrimClip(const std::string& clip_id,
                              TrimEdge edge, double point) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.TrimClip(clip_id, edge, point,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Trim failed: " + error, true);
    } else {
        SetStatus("Trimmed clip");
    }
    NotifyProjectChanged();
}

void TimelineBridge::SplitClip(const std::string& clip_id,
                               double at_seconds) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to split", true);
        return;
    }
    std::string new_id;
    std::string error;
    if (!history_.SplitClip(clip_id, at_seconds, &new_id, &error)) {
        SetStatus("Split failed: " + error, true);
        return;
    }
    SelectClip(new_id);
    NotifyProjectChanged();
    SetStatus("Split clip");
}

void TimelineBridge::DeleteClip(const std::string& clip_id) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to delete", true);
        return;
    }
    // Grab the source path before the clip leaves the model so its
    // queued media-prep requests can be dropped (still-needed media
    // simply re-enqueues on the next render).
    const Clip* doomed = FindClip(project_, clip_id);
    const std::string source =
        doomed != nullptr ? doomed->source_path : std::string{};
    if (!history_.RemoveClip(clip_id)) {
        SetStatus("Delete failed", true);
        return;
    }
    if (!source.empty() && host_.cancel_queued_media != nullptr) {
        host_.cancel_queued_media(source);
    }
    selected_clip_id_.clear();
    NotifyProjectChanged();
    SetStatus("Deleted clip");
}

void TimelineBridge::RippleDeleteClipById(
    const std::string& clip_id) {
    if (clip_id.empty()) {
        SetStatus("Select a clip to ripple-delete", true);
        return;
    }
    const Clip* doomed = FindClip(project_, clip_id);
    const std::string source =
        doomed != nullptr ? doomed->source_path : std::string{};
    std::string error;
    if (!history_.RippleDeleteClip(clip_id, &error)) {
        SetStatus("Ripple delete failed: " + error, true);
        return;
    }
    if (!source.empty() && host_.cancel_queued_media != nullptr) {
        host_.cancel_queued_media(source);
    }
    if (selected_clip_id_ == clip_id) selected_clip_id_.clear();
    NotifyProjectChanged();
    SetStatus("Ripple deleted clip");
}

void TimelineBridge::RippleTrimClip(const std::string& clip_id,
                                    TrimEdge edge, double point) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    const int track = clip->track;
    const double old_start = clip->timeline_start;
    const double old_end = ClipEndSeconds(*clip);
    std::string error;
    history_.BeginMacro("Ripple trim");
    bool ok = history_.TrimClip(clip_id, edge, point,
                                BoundsForPath(clip->source_path),
                                &error);
    if (ok) {
        // Freed span on this track, if any: head trims move the
        // clip's start right, tail trims pull its end left. Growth
        // in either direction needs no gap close (TrimClip already
        // rejected growth into occupied space).
        const Clip* after = FindClip(project_, clip_id);
        double gap = 0.0;
        double pivot = 0.0;   // followers start at/after this time
        if (after != nullptr && edge == TrimEdge::In) {
            gap = after->timeline_start - old_start;
            pivot = after->timeline_start;
        } else if (after != nullptr) {
            gap = old_end - ClipEndSeconds(*after);
            pivot = old_end;
        }
        if (gap > 1e-9) {
            std::vector<const Clip*> followers;
            for (const Clip& c : project_.clips) {
                if (c.track != track) continue;
                const bool self_moved =
                    edge == TrimEdge::In && c.id == clip_id;
                if (self_moved ||
                    (c.id != clip_id &&
                     c.timeline_start >= pivot - 1e-9)) {
                    followers.push_back(&c);
                }
            }
            std::sort(followers.begin(), followers.end(),
                      [](const Clip* a, const Clip* b) {
                          return a->timeline_start <
                                 b->timeline_start;
                      });
            // Move leftmost-first so each clip lands in freed space.
            for (const Clip* f : followers) {
                if (!history_.MoveClip(
                        f->id, f->timeline_start - gap,
                        BoundsForPath(f->source_path), &error)) {
                    ok = false;
                    break;
                }
            }
        }
    }
    history_.EndMacro();
    if (!ok) {
        SetStatus("Ripple trim failed: " + error, true);
    } else {
        SetStatus("Ripple trim");
    }
    NotifyProjectChanged();
}

void TimelineBridge::RollingEditClips(const std::string& from_id,
                                      const std::string& to_id,
                                      double delta) {
    const Clip* from = FindClip(project_, from_id);
    const Clip* to = FindClip(project_, to_id);
    if (from == nullptr || to == nullptr) return;
    std::string error;
    if (!history_.RollingEdit(from_id, to_id, delta,
                              BoundsForPath(from->source_path),
                              BoundsForPath(to->source_path),
                              &error)) {
        SetStatus("Rolling edit failed: " + error, true);
    } else {
        SetStatus("Rolling edit");
    }
    NotifyProjectChanged();
}

void TimelineBridge::SlipClipBy(const std::string& clip_id,
                                double delta_src) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SlipClip(clip_id, delta_src,
                           BoundsForPath(clip->source_path), &error)) {
        SetStatus("Slip failed: " + error, true);
    } else {
        SetStatus("Slipped clip");
    }
    NotifyProjectChanged();
}

void TimelineBridge::SlideClipBy(const std::string& clip_id,
                                 double delta_tl) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SlideClip(clip_id, delta_tl,
                            BoundsForPath(clip->source_path), &error)) {
        SetStatus("Slide failed: " + error, true);
    } else {
        SetStatus("Slid clip");
    }
    NotifyProjectChanged();
}

void TimelineBridge::SetClipRateBy(const std::string& clip_id,
                                   double rate) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    std::string error;
    if (!history_.SetClipRate(clip_id, rate,
                              BoundsForPath(clip->source_path),
                              &error)) {
        SetStatus("Rate change failed: " + error, true);
    } else {
        SetStatus("Rate " + std::to_string(rate));
    }
    NotifyProjectChanged();
}

void TimelineBridge::AddTransitionAfter(
    const std::string& clip_id, Transition::Type type,
    double duration, const std::string& script) {
    const Clip* clip = FindClip(project_, clip_id);
    if (clip == nullptr) return;
    const Clip* next = nullptr;
    for (const Clip& candidate : project_.clips) {
        if (candidate.track != clip->track ||
            candidate.timeline_start < ClipEndSeconds(*clip)) {
            continue;
        }
        if (next == nullptr ||
            candidate.timeline_start < next->timeline_start) {
            next = &candidate;
        }
    }
    if (next == nullptr) {
        SetStatus("No adjacent clip for transition", true);
        return;
    }
    AddTransitionPair(clip->id, next->id, type, duration, script);
}

void TimelineBridge::AddTransitionPair(
    const std::string& from_id, const std::string& to_id,
    Transition::Type type, double duration,
    const std::string& script) {
    const double applied =
        duration > 0.0 ? duration : transition_duration_;
    std::string error;
    if (!history_.AddTransition(from_id, to_id, type, applied,
                                &error, script)) {
        SetStatus("Transition failed: " + error, true);
        return;
    }
    // The applied pick re-arms the default — <= 0 duration requests
    // follow the last-used pair.
    transition_type_ = type;
    transition_duration_ = applied;
    transition_script_ = script;
    NotifyProjectChanged();
    SetStatus(std::string("Added ") + TransitionTypeLabel(type));
}

void TimelineBridge::SetTransitionDurationBy(
    const std::string& from_id, const std::string& to_id,
    double duration) {
    std::string error;
    if (!history_.SetTransitionDuration(from_id, to_id, duration,
                                        &error)) {
        SetStatus("Transition duration failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Transition %.2f s", duration);
    SetStatus(buf);
}

void TimelineBridge::AddMarkerAt(double t, const std::string& name) {
    Marker marker;
    marker.time = t;
    marker.name = name.empty() ? "Marker" : name;
    std::string error;
    if (!history_.AddMarker(marker, &error)) {
        SetStatus("Marker failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Added marker");
}

void TimelineBridge::RemoveMarkerAt(double t) {
    if (!history_.RemoveMarker(t)) {
        SetStatus("No marker at that time", true);
        return;
    }
    NotifyProjectChanged();
    SetStatus("Removed marker");
}

void TimelineBridge::SetWorkAreaRange(double in_s, double out_s) {
    std::string error;
    if (!history_.SetWorkArea(in_s, out_s, &error)) {
        SetStatus("Work area failed: " + error, true);
        return;
    }
    NotifyProjectChanged();
    SetStatus(out_s <= in_s ? "Work area cleared" : "Work area set");
}

void TimelineBridge::SetTrackFlag(int track, char which, bool on) {
    if (track < 0 ||
        track >= static_cast<int>(project_.tracks.size())) {
        return;
    }
    std::string error;
    if (which == 'l') {
        if (!history_.SetTrackLocked(track, on, &error)) {
            SetStatus("Lock failed: " + error, true);
            return;
        }
    } else if (which == 'm') {
        if (!history_.SetTrackMuted(track, on, &error)) {
            SetStatus("Mute failed: " + error, true);
            return;
        }
    } else {
        if (!history_.SetTrackHidden(track, on, &error)) {
            SetStatus("Hide failed: " + error, true);
            return;
        }
    }
    NotifyProjectChanged();
}

}  // namespace XER::VideoEditor::Ui2
