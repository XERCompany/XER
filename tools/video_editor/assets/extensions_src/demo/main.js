// .xexrj entry — runs inside the editor's V8 via the xer_ext module.
const ext = NativeImport("xer_ext");
ext.log("demo extension starting, id=" + ext.id());
ext.log("project: " + ext.projectInfo());
ext.setStatus("Demo extension loaded from TZ");
const files = JSON.parse(ext.listFiles());
ext.log("members: " + files.join(", "));
ext.log("helper.js: " + ext.readFile("scripts/helper.js").trim());
// Uncomment to render the extension UI into the Web tab via CEF:
// ext.openUi();
