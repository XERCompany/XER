// Silence Cutter — RMS silence analysis -> ripple-delete dead air.
const ext = NativeImport("xer_ext");
const sel = JSON.parse(ext.ve("selection.get", {}));
if (sel.clip && sel.clip.source) {
    ext.log("silencecutter: target '" +
            (sel.clip.name || sel.clip.source) + "'");
    ext.setStatus("Silence Cutter — analyze from the panel");
} else {
    ext.setStatus("Silence Cutter — select a media clip");
}
ext.openUi();
