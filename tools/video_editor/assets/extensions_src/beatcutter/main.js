// Beat Cutter — spectral beat detection -> timeline slice/markers.
// Runs inside the editor's V8 (xer_ext module); the interactive work
// happens in the xer-ext:// panel page through the editor.* bridge.
const ext = NativeImport("xer_ext");
const info = JSON.parse(ext.ve("timeline.info", {}));
const sel = JSON.parse(ext.ve("selection.get", {}));
ext.log("beatcutter: " + info.clips + " clip(s) on " + info.tracks +
        " track(s), duration " + info.duration.toFixed(2) + "s");
if (sel.clip && sel.clip.source) {
    ext.log("beatcutter: selected clip '" +
            (sel.clip.name || sel.clip.source) + "' ready for beats");
} else {
    ext.log("beatcutter: select a media clip, then use the panel");
}
ext.setStatus("Beat Cutter — panel opened (select a media clip)");
ext.openUi();
