// Sort Clips by Name — Community Scripts demo for the timeline API.
// Everything is guarded: the script must stay safe when the project
// is empty, when the app exposes no clip list yet, or when the host
// rejects an action (unknown actions return {"ok":false,...}).
const editor = NativeImport("xer_editor");

editor.log("sort_clips: selecting timeline clips");
editor.call("select_all_clips", "{}");

let info = null;
try {
    info = JSON.parse(editor.projectInfo());
} catch (e) {
    editor.log("sort_clips: project info unavailable (" + e + ")");
}
const clips = info && info.ok !== false ? info.clips : null;

if (!clips || !clips.length) {
    editor.log("sort_clips: no clips on the timeline — nothing to do");
} else {
    const sorted = clips.slice().sort(function (a, b) {
        return String(a && a.name ? a.name : "")
            .localeCompare(String(b && b.name ? b.name : ""));
    });
    const order = sorted.map(function (c) { return c.id; });

    let reply = null;
    try {
        reply = JSON.parse(
            editor.call("set_clip_order", JSON.stringify({ order: order })));
    } catch (e) {
        reply = null;
    }
    if (reply && reply.ok) {
        editor.log("sort_clips: applied order for " + order.length +
                   " clip(s)");
    } else {
        editor.log("sort_clips: host rejected set_clip_order — " +
                   "timeline left unchanged");
    }
}
