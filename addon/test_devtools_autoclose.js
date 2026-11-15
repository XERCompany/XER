// DevTools auto-close test.
// Scenario: open DevTools, then close the MAIN window — the DevTools
// window must close itself (close hook) and the app must exit.
const xer = require('./index.js');
const UI = xer.UI || xer.XER;
const app = new UI.Application();
const win = new UI.Window();
win.Create('Main App', 800, 500);

const card = new UI.Widget();
card.AddChild(new UI.Label('target'));
win.SetContent(card);
win.Show();

let frames = 0;
let opened = false;
win.SetFrameCallback(() => {
  frames++;
  if (frames === 5 && !opened) {
    opened = true;
    const dt = win.OpenDevTools();
    console.log('OpenDevTools ->', dt ? dt.GetTitle() : dt);
  }
  if (frames === 30) {
    win.Close();   // main closes; devtools should follow via close hook
    console.log('main closed — devtools should auto-close now');
  }
}, 50);
app.Initialize();
app.Run();
console.log('app exited cleanly (devtools did not orphan), frames =', frames);
