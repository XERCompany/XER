// Compile-time smoke test for index.d.ts — verifies the declared surface
// matches how the API is actually used (example.js / demo_design.js style).
// Check with: npx -p typescript tsc --noEmit --strict type_smoke.ts
import xer = require('./index');

const { UI, audio, image, notify } = xer;

// XER is the same object as UI at runtime.
const X: typeof UI = xer.XER;

const info = xer.info();
const nodeVersion: string = info.node;

// --- UI ---
const app = new UI.Application();
const win = new UI.Window();
const widget = new UI.Widget();

win.Create('title', 800, 600);
win.SetContent(widget);
win.Show();

const visible: boolean = widget.IsVisible();
const enabled: boolean = widget.IsEnabled();
const contentH: number = widget.GetContentHeight();
const err: string = win.GetLastRenderError();
const stats = win.GetRenderStats();
const avgMs: number = stats.avgFrameMs;
const fullFrame: boolean = stats.lastFrameFull;
const hwnd: number = win.GetHWND();
const devtools: boolean = win.OpenDevTools();

const xmlDump: string = widget.DumpXml();
const parsed = UI.parseXml('<Widget><Label text="hi"/></Widget>');
if (parsed.ok && parsed.widget) {
    win.SetContent(parsed.widget);
    const warns: string[] = parsed.warnings;
    const perr: string = parsed.error;
    void warns; void perr;
}
const parsedFile = UI.parseXmlFile('ui/main.xml');
void parsedFile;
const tags: string = UI.xmlSupportedTags();

class MyWidget extends UI.Widget {
    constructor() {
        super();
        this.AddChild(new UI.Label('hi'));
    }
}

const label = new UI.Label('x');
const btn = new UI.Button('ok');
const dlg = new UI.Dialog();
const scroll = new UI.ScrollArea(new UI.Widget());
const table = new UI.Table();

// --- audio ---
const buf = audio.Buffer.FromFile('a.wav');
const rate: any = buf.GetSampleRate();
const biquad = new audio.dsp.Biquad();
const fft = new audio.dsp.FFT(1024);
const engine = new audio.core.Engine();
const running: boolean = engine.IsRunning();
const player = new audio.Player();
const fx = new audio.fx.Compressor();
const conv = new audio.codec.WavPcm();

// namespace functions
const hann: any = audio.dsp.Hann(256);
const fxList: any = audio.fx.custom.List();

// --- image / notify ---
const img: any = image.decode('x.png');
image.save(img, 'y.png');
notify.info('hello');

app.Run();
