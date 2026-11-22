// Captionist — whisper transcription -> styled caption title clips.
const ext = NativeImport("xer_ext");
const sel = JSON.parse(ext.ve("selection.get", {}));
const services = JSON.parse(ext.ve("ve.services", {}));
const hasAi = (services.services || []).indexOf("ai.transcribe") >= 0;
ext.log("captionist: ai.transcribe " +
        (hasAi ? "available" : "NOT in service table"));
if (sel.clip && sel.clip.source) {
    ext.setStatus("Captionist — transcribe '" +
                  (sel.clip.name || sel.clip.source) + "' from the panel");
} else {
    ext.setStatus("Captionist — select a media clip with speech");
}
ext.openUi();
