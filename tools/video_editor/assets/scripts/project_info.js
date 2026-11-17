// Project Info — prints the JSON the app reports for the open project.
// xer_editor.projectInfo() is shorthand for call("project_info", "{}")
// and returns a JSON string; malformed/missing data degrades to an
// error object instead of throwing.
const editor = NativeImport("xer_editor");

let info = null;
try {
    info = JSON.parse(editor.projectInfo());
} catch (e) {
    info = { ok: false, error: "project info unavailable", detail: String(e) };
}

editor.log(JSON.stringify(info, null, 2));
