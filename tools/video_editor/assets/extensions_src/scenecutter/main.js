// Scene Cutter — luma-signature scene-change detection -> cuts/markers.
const ext = NativeImport("xer_ext");
const sel = JSON.parse(ext.ve("selection.get", {}));
if (sel.clip && sel.clip.source) {
    ext.setStatus("Scene Cutter — scan '" +
                  (sel.clip.name || sel.clip.source) + "' from the panel");
} else {
    ext.setStatus("Scene Cutter — select a video clip");
}
ext.openUi();
