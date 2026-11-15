// DevTools window lifecycle test.
// Scenario: open DevTools, close the DevTools window first — the app must
// keep running; then close the main window — app must exit cleanly.
const xer = require('./index.js');
const UI = xer.UI || xer.XER;
const app = new UI.Application();
const win = new UI.Window();
win.Create('Main App', 800, 500);

const card = new UI.Widget();
card.SetTooltip('main card');
const btn = new UI.Button('DevTools target');
btn.SetPosition(20, 20); btn.SetSize(200, 36);
const lbl = new UI.Label('inspected content');
lbl.SetPosition(20, 70);
card.AddChild(btn); card.AddChild(lbl);
win.SetContent(card);
win.Show();

let frames = 0;
let devtools = null;
win.SetFrameCallback(() => {
  frames++;
  if (frames === 5 && !devtools) {
    devtools = win.OpenDevTools();
    console.log('OpenDevTools ->', devtools ? devtools.GetTitle() : devtools,
                'isClosed=', devtools ? devtools.IsClosed() : '-');
  }
  if (frames === 30 && devtools && !devtools.IsClosed()) {
    devtools.Close();
    console.log('devtools.Close() posted');
  }
  if (frames === 45) {
    console.log('devtools.IsClosed() =', devtools.IsClosed(),
                '(app still running — good)');
  }
  if (frames === 60) {
    win.Close();
    console.log('main Close() posted');
  }
}, 50);
app.Initialize();
app.Run();
console.log('app exited cleanly, frames =', frames);
