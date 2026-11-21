// .xexrj entry — runs inside the editor's V8 via the xer_ext module.
const ext = NativeImport("xer_ext");
ext.log("demo extension starting, id=" + ext.id());
ext.log("project: " + ext.projectInfo());
ext.setStatus("Demo extension loaded from TZ");
const files = JSON.parse(ext.listFiles());
ext.log("members: " + files.join(", "));
ext.log("helper.js: " + ext.readFile("scripts/helper.js").trim());
// ve.core v3: the SAME service table native extensions reach through
// ve_call — routed in-process to the editor's dispatcher. Result is
// the synchronous {"ok":true,...} / {"ok":false,...} JSON string.
const info = JSON.parse(ext.ve("timeline.info", {}));
ext.log("timeline: playhead=" + info.playhead + " duration=" +
        info.duration + " tracks=" + info.tracks);
const services = JSON.parse(ext.ve("ve.services"));
ext.log("ve.services: " + services.services.length + " calls served");
// Uncomment to render the extension UI into the Web tab via CEF:
// ext.openUi();
