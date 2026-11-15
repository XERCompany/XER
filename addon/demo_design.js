// demo_design.js — showcase of the XER UI design system (xer.UI.*).
// Run:  node demo_design.js
const { UI } = require('./index.js');

const app = new UI.Application();
const win = new UI.Window();

// ---------------------------------------------------------------------------
// helpers — `new UI.Widget()` is a Card (padded surface); `UI.Form(cols, gap)`
// stacks children into a grid, so each card body is a Form.
// ---------------------------------------------------------------------------
const card = (children, columns = 1, gap = 10) => {
    const c = new UI.Widget();
    const f = new UI.Form(columns, gap);
    for (const child of [].concat(children)) f.AddChild(child);
    c.AddChild(f);
    return c;
};
const label = (text, muted = false) => new UI.Label(text, muted);
// card header block: title + muted subtitle + separator
const head = (title, sub) => [label(title), label(sub, true), new UI.Separator('horizontal')];

// ---------------------------------------------------------------------------
// 1 · Header
// ---------------------------------------------------------------------------
const badges = new UI.Form(4, 8);
badges.AddChild(new UI.Badge('Stable', 'accent'));
badges.AddChild(new UI.Badge('v0.3.0', 'secondary'));
badges.AddChild(new UI.Badge('Direct2D', 'outline'));
badges.AddChild(new UI.Avatar('XER', 44));

const headerCard = card([
    label('XER Design System'),
    label('Native widgets rendered through the Direct2D/DXGI pipeline — no DOM, no browser.', true),
    badges,
], 1, 12);

// ---------------------------------------------------------------------------
// 2 · Buttons & toggles
// ---------------------------------------------------------------------------
const btnRow = new UI.Form(3, 8);
btnRow.AddChild(new UI.Button('Save', 'primary'));
btnRow.AddChild(new UI.Button('Cancel', 'secondary'));
btnRow.AddChild(new UI.Button('Export', 'outline'));
btnRow.AddChild(new UI.Button('Docs', 'ghost'));
btnRow.AddChild(new UI.Button('Delete', 'destructive'));
btnRow.AddChild(new UI.Button('Spinner…', 'secondary'));

const toggles = new UI.Form(3, 8);
const chk = new UI.Checkbox(true);
chk.SetOnChanged((v) => console.log('[checkbox] notifications =', v));
toggles.AddChild(chk);
const sw = new UI.Switch(false);
sw.SetOnChanged((v) => console.log('[switch] animations =', v));
toggles.AddChild(sw);
const tg = new UI.Toggle('Bold', false);
tg.SetOnPressedChanged((v) => console.log('[toggle] bold =', v));
toggles.AddChild(tg);

const buttonsCard = card([
    ...head('Buttons', 'Five styles plus checkbox, switch and toggle.'),
    btnRow,
    toggles,
]);

// ---------------------------------------------------------------------------
// 3 · Inputs
// ---------------------------------------------------------------------------
const nameInput = new UI.TextInput('');
nameInput.SetPlaceholder('Display name');
nameInput.SetOnSubmit(() =>
    console.log('[input] submit:', JSON.stringify(nameInput.GetValue())));

const passInput = new UI.TextInput('');
passInput.SetPlaceholder('Password');
passInput.SetPasswordMode(true);

const themeSelect = new UI.Select(['System', 'Light', 'Dark'], 0);
themeSelect.SetOnChanged(() =>
    console.log('[select] theme =', themeSelect.GetSelectedText()));

const fontSelect = new UI.NativeSelect(['Mono', 'Sans', 'Serif'], 1);

const inputs = new UI.Form(2, 10);
inputs.AddChild(nameInput);
inputs.AddChild(passInput);
inputs.AddChild(themeSelect);
inputs.AddChild(fontSelect);

const volumeLabel = label('Volume');
const volume = new UI.Slider(0, 100, 35);
const volumeBar = new UI.Progress(0.35);
volume.SetOnValueChanged((v) => {
    volumeBar.SetValue(v / 100);
    console.log('[slider] volume =', v.toFixed(0));
});

const notes = new UI.Textarea('');
notes.SetPlaceholder('Release notes…');
notes.SetOnChanged((t) => console.log('[textarea] chars =', t.length));

const inputsCard = card([
    ...head('Inputs', 'Text fields, selects, slider + progress, textarea.'),
    inputs,
    volumeLabel,
    volume,
    volumeBar,
    notes,
]);

// ---------------------------------------------------------------------------
// 4 · Data — table, chart, pagination, breadcrumb
// ---------------------------------------------------------------------------
const table = new UI.Table();
table.SetColumns([
    { header: 'Name', width: 160 },
    { header: 'Role', width: 140 },
    { header: 'Status' },
]);
table.SetRows([
    ['Devin', 'Engineer', 'Active'],
    ['Ana', 'Designer', 'Active'],
    ['Max', 'Reviewer', 'Away'],
    ['Sara', 'PM', 'Active'],
]);
table.SetOnCellClick((cell) =>
    console.log('[table] cell:', cell.row, '/', cell.col));

const chart = new UI.Chart('Line');
chart.SetTitle('Weekly commits');
chart.SetSeries([
    { label: 'main', values: [12, 19, 7, 24, 18, 31, 22] },
    { label: 'dev', values: [8, 14, 11, 17, 22, 15, 27] },
]);
chart.SetShowGrid(true);
chart.SetShowLegend(true);
chart.SetOnPointClick((p) =>
    console.log('[chart] point:', p.series, p.index));

const pager = new UI.Pagination(12, 0);
pager.SetOnPageChanged((p) => console.log('[pagination] page =', p));

const crumbs = new UI.Breadcrumb();
crumbs.SetItems(['Home', 'Projects', 'XER3', 'Design demo']);
crumbs.SetOnNavigate((i) => console.log('[breadcrumb] →', i));

const dataCard = card([
    ...head('Data', 'Table, line chart, pagination and breadcrumb.'),
    table,
    chart,
    pager,
    crumbs,
]);

// ---------------------------------------------------------------------------
// 5 · Feedback — alerts, spinner, progress, skeleton, accordion, carousel
// ---------------------------------------------------------------------------
const alertOk = new UI.Alert('Update ready',
    'Version 0.3.1 is available with performance improvements.', 'default');
const alertBad = new UI.Alert('Build failed',
    'Linker error in XER_UI_Core — check the build log.', 'destructive');

const spinner = new UI.Spinner(0);
const loading = new UI.Progress(0.0);
const skel = new UI.Skeleton(0, 0, 'text');
skel.SetSize(280, 18);
const loadingRow = new UI.Form(3, 8);
loadingRow.AddChild(spinner);
loadingRow.AddChild(loading);
loadingRow.AddChild(skel);

const accordion = new UI.Accordion();
accordion.SetItems([
    { title: 'What is this?',
      content: label('A native C++ widget toolkit with GPU-accelerated rendering.'),
      open: true },
    { title: 'Keyboard support',
      content: label('Tabs, dialogs and menus are fully keyboard navigable.'),
      open: false },
    { title: 'Theming',
      content: label('Light and dark variants via window.SetTheme().'),
      open: false },
]);

const mkSlide = (n, txt, style) => {
    const s = new UI.Widget();
    const f = new UI.Form(1, 8);
    f.AddChild(label(`Slide ${n}`));
    f.AddChild(label(txt, true));
    f.AddChild(new UI.Button(`Action ${n}`, style));
    s.AddChild(f);
    return s;
};
const carousel = new UI.Carousel();
carousel.SetLoop(true);
carousel.SetShowDots(true);
carousel.AddSlide(mkSlide(1, 'Native Direct2D rendering', 'primary'));
carousel.AddSlide(mkSlide(2, 'Damage-aware partial repaints', 'secondary'));
carousel.AddSlide(mkSlide(3, 'Zero-copy audio + UI in one runtime', 'outline'));

const feedbackCard = card([
    ...head('Feedback', 'Alerts, spinner, progress, skeleton, accordion, carousel.'),
    alertOk,
    alertBad,
    loadingRow,
    accordion,
    carousel,
]);

// ---------------------------------------------------------------------------
// 6 · Overlay demo — dialog launched from a button
// ---------------------------------------------------------------------------
const dlgBody = new UI.Form(1, 10);
dlgBody.AddChild(label('This dialog is a native overlay widget.'));
dlgBody.AddChild(label('It was created in JS and shown via ShowOverlay().', true));
const dlgCard = new UI.Widget();
dlgCard.AddChild(dlgBody);

const openDlg = new UI.Button('Open dialog', 'primary');
openDlg.SetOnClick(() => {
    const dlg = new UI.Dialog('Confirm action', dlgCard, 'OK', 'Cancel');
    dlg.SetOnClosed(() =>
        console.log('[dialog] closed, confirmed =', dlg.WasConfirmed()));
    openDlg.ShowOverlay(dlg);   // any widget in the tree can host the overlay
});

const overlayCard = card([
    ...head('Overlays', 'Dialogs render in a modal layer above the widget tree.'),
    openDlg,
    new UI.Kbd('Ctrl+K'),
    new UI.Marker('New'),
]);

// ---------------------------------------------------------------------------
// assemble — root stack inside a scroll area
// ---------------------------------------------------------------------------
const root = new UI.Form(1, 14);
[
    headerCard,
    buttonsCard,
    inputsCard,
    dataCard,
    feedbackCard,
    overlayCard,
].forEach((w) => root.AddChild(w));

const scroll = new UI.ScrollArea(root);
if (process.env.XER_DEMO_DEBUG) {
    console.log('[debug] root preferred =', JSON.stringify(root.GetPreferredSize()));
    console.log('[debug] scroll contentHeight =', scroll.GetContentHeight());
}
win.Create('XER Design System — demo_design.js', 1120, 760);
win.SetTheme('dark');
win.SetContent(scroll);
win.SetSizeLimits(640, 480, 1920, 1200);
win.Show();

// live animation: spinner phase + fake progress.
// XER_DEMO_FRAMES=N auto-closes after N frames — smoke-test hook.
const autoFrames = parseInt(process.env.XER_DEMO_FRAMES || '0', 10);
let t = 0, frames = 0;
win.SetFrameCallback(() => {
    t += 0.08;
    spinner.SetPhase(t % (Math.PI * 2));
    loading.SetValue((Math.sin(t * 0.7) + 1) / 2);
    if (autoFrames > 0 && ++frames >= autoFrames) {
        const st = win.GetRenderStats();
        console.log('[demo] frames =', frames,
                    'renderError =', JSON.stringify(win.GetLastRenderError()),
                    'stats =', JSON.stringify(st));
        win.Close();
    }
}, 50);

app.Initialize();
app.Run();
