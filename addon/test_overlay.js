// Overlay/damage stress: open dialog, close it, scroll — verify no stale pixels.
const { UI } = require('./index.js');
const app = new UI.Application();
const win = new UI.Window();

const root = new UI.Form(1, 14);
const mkCard = (t) => {
    const c = new UI.Widget();
    const f = new UI.Form(1, 10);
    f.AddChild(new UI.Label(t));
    f.AddChild(new UI.Label('body text ' + t, true));
    f.AddChild(new UI.Button('btn ' + t, 'secondary'));
    c.AddChild(f);
    return c;
};
for (let i = 1; i <= 8; ++i) root.AddChild(mkCard('Card ' + i));

const scroll = new UI.ScrollArea(root);
win.Create('overlay test', 900, 640);
win.SetTheme('dark');
win.SetContent(scroll);
win.Show();

const body = new UI.Form(1, 8);
body.AddChild(new UI.Label('Dialog body — should disappear cleanly.'));
const dlgCard = new UI.Widget();
dlgCard.AddChild(body);

let phase = 0, frames = 0;
win.SetFrameCallback(() => {
    ++frames;
    if (frames === 30) {
        const dlg = new UI.Dialog('Test', dlgCard, 'OK', 'Cancel');
        root.ShowOverlay(dlg);
        console.log('[t] dialog open');
    } else if (frames === 90) {
        root.HideOverlay();
        console.log('[t] dialog closed');
    } else if (frames === 150) {
        console.log('[t] done, renderError =', JSON.stringify(win.GetLastRenderError()));
        if (!process.env.KEEP_OPEN) win.Close();
    }
}, 33);

app.Initialize();
app.Run();
