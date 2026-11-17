// Hello Script — minimal Community Scripts demo.
// NativeImport("xer_editor") is the host-provided module:
//   .log(msg)            captured into the run's output pane
//   .setStatus(text)     routes {"text": ...} to the app's status line
//   .call(action, json)  generic action dispatch, returns a JSON string
const editor = NativeImport("xer_editor");

editor.log("hello.js: running");
editor.setStatus("Hello from a community script");
editor.log("hello.js: done");
