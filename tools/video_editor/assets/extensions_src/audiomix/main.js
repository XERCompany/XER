// Audio Mix Assistant — per-clip LUFS measurement -> loudness
// normalization via timeline.set_clip gain_db.
const ext = NativeImport("xer_ext");
const clips = JSON.parse(ext.ve("timeline.clips", {}));
let audio = 0;
for (const c of (clips.clips || [])) { if (c.source) ++audio; }
ext.log("audiomix: " + audio + " media clip(s) measurable");
ext.setStatus("Audio Mix — LUFS normalize from the panel");
ext.openUi();
