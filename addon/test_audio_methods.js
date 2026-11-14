const xer = require('./index.js');
const a = xer.audio;
const step = process.argv[2];

const buf = new a.Buffer({ sampleRate: 44100, channels: 1, data: new Float32Array(512).fill(0.1) });

const steps = {
  fx: () => {
    const fx = new a.fx.Tremolo(); fx.Initialize(44100); fx.SetRateHz(5); fx.SetDepth(0.5);
    const out = fx.ProcessBlock(new Float32Array(256).fill(0.5));
    console.log('tremolo out', out && out.length);
    const comp = new a.fx.Compressor(); comp.SetThresholdDb(-20); comp.SetRatio(4);
    console.log('comp', comp.ProcessBlock(new Float32Array(128).fill(0.9))?.length);
    const ge = new a.fx.GraphicEQ(); console.log('geq init', ge.Initialize(44100, [100, 1000, 10000]));
    const pe = new a.fx.ParametricEQ(); console.log('peq init', pe.Initialize(44100, 2));
    const at = new a.fx.AutoTune(); console.log('at init', at.Initialize(44100, 512));
    const rv = new a.fx.ReverbConv(); console.log('ir', rv.LoadImpulseResponse(new Float32Array(64).fill(0.1)));
    console.log('rv', rv.ProcessBlock(new Float32Array(64).fill(0.2))?.length);
    const ra = new a.fx.ReverbAlgo(); console.log('ra init', ra.Initialize(44100, 4410));
    console.log('ra', ra.ProcessBlock(new Float32Array(256).fill(0.1))?.length);
    const ch = new a.fx.Chorus(); console.log('ch init', ch.Initialize(44100, 4410));
    console.log('ch', ch.ProcessBlock(new Float32Array(256).fill(0.1))?.length);
  },
  dsp: () => {
    const fft = new a.dsp.FFT(1024); fft.Forward(new Float32Array(1024));
    console.log('fft mag', fft.GetMagnitude().length);
    const bq = new a.dsp.Biquad(); console.log('bq cfg', bq.Configure(0, 44100, 1000, 0.707));
    console.log('bq sample', bq.ProcessSample(0.5));
    const dl = new a.dsp.DelayLine(); dl.Initialize(128); dl.SetDelaySamples(32);
    console.log('delay', dl.Process(0.5));
    const st = new a.dsp.STFT(512, 128); st.Analyze(new Float32Array(512));
    console.log('stft', st.GetFftSize(), st.GetSpectrum().length);
    const pv = new a.dsp.PhaseVocoder(); console.log('pv ok');
    const ps = new a.dsp.PitchShifter(); console.log('ps ok');
    const ss = new a.dsp.SpectralShaper(); console.log('ss ok');
    const fir = new a.dsp.FIRBank(); console.log('fir ok');
    const iir = new a.dsp.IIRBank(); console.log('iir ok');
    const cv = new a.dsp.Convolution(); console.log('cv ok');
  },
  analysis: () => {
    const rms = new a.analysis.RmsPeak(); rms.Analyze(new Float32Array(1024).fill(0.5), 1);
    console.log('rms', rms.GetRMS());
    const loud = new a.analysis.LoudnessMeter();
    loud.ProcessBlock(new Float32Array(512).fill(0.5), 1);
    console.log('lufs', loud.GetIntegratedLUFS());
    const bt = new a.analysis.BeatTracker(); bt.Analyze(new Float32Array(8192).fill(0.3), 44100);
    console.log('bpm', bt.GetEstimatedBPM());
    const hc = new a.analysis.HarmonicContent(2048); hc.Analyze(new Float32Array(2048).fill(0.2), 44100);
    console.log('harm', hc.GetFundamentalFrequency());
    const od = new a.analysis.OnsetDetector(); od.DetectOnsets(new Float32Array(4096).fill(0.1), 44100);
    console.log('onsets', od.GetOnsetFrames().length);
    const pe = new a.analysis.PitchEstimator(); pe.Estimate(new Float32Array(2048).fill(0.1));
    console.log('pitch est', JSON.stringify(pe.GetLastPitch()));
    const sg = new a.analysis.Spectrogram(1024, 256); sg.Generate(new Float32Array(4096).fill(0.1));
    console.log('spec', sg.GetFrameCount(), sg.GetBinCount());
    const fa = new a.analysis.FFTAnalyzer(2048); fa.ComputeFFT(new Float32Array(2048).fill(0.1));
    console.log('ffta', fa.GetMagnitudeSpectrum().length);
    const pd = new a.analysis.PitchDetector();
    const pi = pd.DetectPitch(new Float32Array(2048).fill(0.1));
    console.log('pdet', JSON.stringify(pi));
  },
  codec: () => {
    const wp = new a.codec.WavPcm();
    const d = wp.Encode16(new Float32Array(256).fill(0.5), 44100, 1);
    console.log('wav enc', d.length);
    const dec = wp.Decode16(d);
    console.log('wav dec', dec.data.length, dec.sampleRate, dec.channels);
    const d2 = wp.Encode16(new Float32Array(512).fill(0.25), 48000, 2);
    const dec2 = wp.Decode16(d2);
    console.log('wav stereo', dec2.data.length, dec2.sampleRate, dec2.channels);
    const wf = new a.codec.WavFloat();
    const df = wf.Encode32(new Float32Array(128).fill(0.25));
    console.log('wavf', df.length, wf.Decode32(df).length);
    const fl = new a.codec.Flac();
    try { const fd = fl.Decode(new Uint8Array(64)); console.log('flac', fd ? fd.length : 'reject'); } catch(e) { console.log('flac err', e.message); }
    const mp3 = new a.codec.Mp3();
    try { const m = mp3.Encode(new Float32Array(1152).fill(0.1)); console.log('mp3 enc', m.length); } catch(e) { console.log('mp3 err', e.message); }
    const ogg = new a.codec.Ogg();
    try { const o = ogg.Encode(new Float32Array(512).fill(0.1)); console.log('ogg', o.length); } catch(e) { console.log('ogg err', e.message); }
    const aac = new a.codec.Aac();
    try { const ac = aac.Encode(new Float32Array(1024).fill(0.1)); console.log('aac', ac.length); } catch(e) { console.log('aac err', e.message); }
    const id3 = new a.codec.Id3();
    const tag = id3.Build({title:'T', artist:'A', album:'B'});
    console.log('id3', tag.length, JSON.stringify(id3.Parse(tag)));
  },
  midi: () => {
    const mp = new a.midi.Parser();
    const evs = mp.Parse(new Uint8Array([0x90,60,100,0x80,60,0]));
    console.log('events', evs.length, JSON.stringify(evs[0]));
    console.log('desc', mp.DescribeType(0x90));
    const sq = new a.midi.Sequencer();
    sq.SetPattern(evs); sq.SetLoopLength(480);
    console.log('coll', sq.CollectRange(0, 480).length, 'pat', sq.GetPattern().length);
    const ck = new a.midi.Clock(); ck.SetTempoBpm(120);
    console.log('pulses', ck.AdvanceSamples(4410, 44100));
    const mm = new a.midi.Mapping();
    mm.Bind(7, 42, 0, 1, false, -1, 'vol');
    console.log('resolve', mm.Resolve(7));
    const sy = new a.midi.Sysex();
    console.log('sysex valid', sy.IsValidSysex(new Uint8Array([0xF0,1,2,0xF7])));
    const req = sy.BuildIdentityRequest(0x7F); console.log('sysex req', req.length);
    const au = new a.midi.Automation();
    au.AddPoint(0, 0); au.AddPoint(480, 1);
    console.log('auto', au.Evaluate(240));
    const dp = new a.midi.Dispatcher();
    let got = 0; dp.AddListener((ev) => { got++; });
    dp.Dispatch(evs[0]); console.log('dispatch got', got);
  },
  synth: () => {
    const e = new a.synth.Adsr(); e.Set(0.01,0.1,0.7,0.3); e.NoteOn();
    console.log('adsr', e.Process(44100));
    const me = new a.synth.MultistageEnv();
    me.SetStages([{target:1, time_s:0.1},{target:0, time_s:0.2}]); me.Trigger();
    console.log('msenv', me.Process(44100));
    const f = new a.synth.Filter(); f.SetCutoff(0.5); f.SetResonance(0.2);
    console.log('filter', f.Process(0.5));
    const l = new a.synth.Lfo(); l.SetRateHz(5); console.log('lfo', l.Process(44100));
    const mm = new a.synth.ModMatrix();
    mm.SetAmount(0, 1, 0.5); mm.SetSourceValue(0, 0.8); mm.ConfigureDestination(1, 0, -1, 1);
    console.log('mod', mm.Apply(1), mm.GetDestinationValue(1));
    const oa = new a.synth.OscAnalog(); oa.SetFrequency(440); console.log('oscA', oa.Process(44100));
    const of = new a.synth.OscFm(); of.SetCarrier(440); of.SetModulator(110); of.SetIndex(2);
    console.log('oscF', of.Process(44100));
    const on = new a.synth.OscNoise(); on.SetSeed(42); console.log('oscN', on.Process());
    const ow = new a.synth.OscWavetable(); ow.SetTable(new Float32Array(256).fill(0.5)); ow.SetFrequency(220);
    console.log('oscW', ow.Process(44100));
    const po = new a.synth.Polyphony(); po.SetMaxVoices(8);
    console.log('poly', JSON.stringify(po.AllocateVoice(60)));
    const vm = new a.synth.VoiceManager(); vm.SetMaxVoices(4); vm.NoteOn(60, 440);
    console.log('vm', vm.RenderSample(44100), vm.GetActiveVoiceCount());
  },
  core: () => {
    const en = new a.core.Engine();
    console.log('eng init', en.Initialize(44100, 512, 2), en.Start(), en.IsRunning());
    en.Stop();
    const eh = new a.core.EngineHost();
    console.log('host', eh.CreateEngine('e1', 44100, 512, 2), eh.GetEngineCount(), eh.GetEngineList());
    eh.DestroyEngine('e1');
    const cs = new a.core.ClockSync(); cs.Initialize(44100, 0);
    cs.UpdateClock(1000); cs.SyncToExternal(2000, 123456);
    console.log('clock', JSON.stringify(cs.GetClockInfo()), cs.GetCurrentTime());
    console.log('ratio', cs.CalculateSampleRateRatio(44100, 48000));
    const fq = new a.core.Fifo(256); fq.Write(new Float32Array(100).fill(0.1));
    console.log('fifo', fq.GetAvailableFrames(), fq.Read(50).length, fq.GetAvailableFrames());
    const il = new a.core.Interleave();
    console.log('il', il.Interleave([new Float32Array([1,2]), new Float32Array([3,4])]));
    console.log('deil', il.Deinterleave(new Float32Array([1,3,2,4]), 2));
    const bd = new a.core.BitDepth();
    console.log('bd', bd.GetBytesPerSample(1), bd.IsFloatingPoint(4));
    const rs = new a.core.Resample(2); rs.Initialize(44100, 48000);
    const conv = rs.Convert(new Float32Array(100)); console.log('rs', conv.length, rs.GetRatio());
    const nb = new a.core.NativeBuffer(44100, 2, 32, 0); nb.Resize(128); nb.Fill(0.5);
    console.log('nb', nb.GetFrameCount(), nb.GetFloatData().length);
    const lm = new a.core.Latency();
    lm.StartMonitoring(); lm.RecordInputTimestamp(); lm.RecordOutputTimestamp(); lm.StopMonitoring();
    console.log('lat', JSON.stringify(lm.CalculateLatency()));
  },
  plugin: () => {
    const pb = new a.plugin.ParamBridge();
    pb.RegisterParameter(1, 'gain', 0, 0, 1); pb.SetValue(1, 0.5);
    console.log('pb', pb.GetValue(1), pb.Snapshot().length);
    const bh = new a.plugin.BuiltinHost();
    console.log('bh init', bh.Initialize(44100, 512), 'load', bh.Load('builtin://gain'));
    console.log('bh proc', bh.Process(new Float32Array(64).fill(0.5)).length);
    console.log('bh params', bh.GetParameters().length);
    const vh = new a.plugin.Vst3Host(); vh.Initialize(44100, 512);
    console.log('vst3 backend', vh.GetBackendMode());
    const ch = new a.plugin.ClapHost(); ch.Initialize(44100, 512);
    console.log('clap ok');
    const ah = new a.plugin.AuHost(); ah.Initialize(44100, 512);
    console.log('au', ah.GetBackendMode());
    const ve = new a.plugin.Vst3External();
    console.log('vst3ext loaded?', ve.IsLoaded());
    const ce = new a.plugin.ClapExternal(); ce.Initialize(44100, 512);
    console.log('clapext ok');
  },
  player: () => {
    const p = new a.Player();
    p.Load(buf);
    console.log('player state', p.GetState ? p.GetState() : 'n/a');
    let threw = false;
    try { p.Load({sampleRate:44100}); } catch(e) { threw = true; }
    console.log('rejects non-buffer:', threw);
  },
};

if (step === 'all') { for (const k of Object.keys(steps)) { console.log('=== ' + k); steps[k](); } }
else steps[step]();
console.log('OK');
