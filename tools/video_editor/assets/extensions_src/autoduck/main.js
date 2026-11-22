// Auto Duck — speech-envelope sidechain: gain_db keyframes on the
// music clip, driven by silence analysis of the dialogue clip.
const ext = NativeImport("xer_ext");
const sel = JSON.parse(ext.ve("selection.get", {}));
if (sel.clip && sel.clip.source) {
    ext.setStatus("Auto Duck — '" + (sel.clip.name || sel.clip.source) +
                  "' is the dialogue sidechain");
} else {
    ext.setStatus("Auto Duck — select the DIALOGUE clip first");
}
ext.openUi();
