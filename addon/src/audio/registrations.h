#pragma once

// Registration entry points for every xer.audio.* class binding.
// Implemented across src/audio/{dsp,fx,analysis,codec,midi,synth,core,plugin}/.

#include "util.h"

namespace xer::addon::audio::dsp {
void InitDspBiquad(v8::Local<v8::Object> ns);
void InitDspFFT(v8::Local<v8::Object> ns);
void InitDspSTFT(v8::Local<v8::Object> ns);
void InitDspWindowing(v8::Local<v8::Object> ns);
void InitDspDelayLine(v8::Local<v8::Object> ns);
void InitDspConvolution(v8::Local<v8::Object> ns);
void InitDspPhaseVocoder(v8::Local<v8::Object> ns);
void InitDspPitchShifter(v8::Local<v8::Object> ns);
void InitDspSpectralShaper(v8::Local<v8::Object> ns);
void InitDspFIRBank(v8::Local<v8::Object> ns);
void InitDspIIRBank(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::fx {
void InitFxCompressor(v8::Local<v8::Object> ns);
void InitFxExpander(v8::Local<v8::Object> ns);
void InitFxGate(v8::Local<v8::Object> ns);
void InitFxLimiter(v8::Local<v8::Object> ns);
void InitFxBitcrush(v8::Local<v8::Object> ns);
void InitFxTube(v8::Local<v8::Object> ns);
void InitFxGraphicEQ(v8::Local<v8::Object> ns);
void InitFxParametricEQ(v8::Local<v8::Object> ns);
void InitFxChorus(v8::Local<v8::Object> ns);
void InitFxTremolo(v8::Local<v8::Object> ns);
void InitFxVibrato(v8::Local<v8::Object> ns);
void InitFxRingMod(v8::Local<v8::Object> ns);
void InitFxFlanger(v8::Local<v8::Object> ns);
void InitFxPhaser(v8::Local<v8::Object> ns);
void InitFxAutoTune(v8::Local<v8::Object> ns);
void InitFxReverbAlgo(v8::Local<v8::Object> ns);
void InitFxReverbConv(v8::Local<v8::Object> ns);
void InitFxCustom(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::analysis {
void InitAnalysisBeatTracker(v8::Local<v8::Object> ns);
void InitAnalysisHarmonic(v8::Local<v8::Object> ns);
void InitAnalysisLoudness(v8::Local<v8::Object> ns);
void InitAnalysisOnset(v8::Local<v8::Object> ns);
void InitAnalysisPitchEst(v8::Local<v8::Object> ns);
void InitAnalysisRmsPeak(v8::Local<v8::Object> ns);
void InitAnalysisSpectrogram(v8::Local<v8::Object> ns);
void InitAnalysisFft(v8::Local<v8::Object> ns);
void InitAnalysisPitchDet(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::codec {
void InitCodecWavPcm(v8::Local<v8::Object> ns);
void InitCodecWavFloat(v8::Local<v8::Object> ns);
void InitCodecFlac(v8::Local<v8::Object> ns);
void InitCodecMp3(v8::Local<v8::Object> ns);
void InitCodecOgg(v8::Local<v8::Object> ns);
void InitCodecAac(v8::Local<v8::Object> ns);
void InitCodecId3(v8::Local<v8::Object> ns);
void InitCodecStreamer(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::midi {
void InitMidiParser(v8::Local<v8::Object> ns);
void InitMidiSequencer(v8::Local<v8::Object> ns);
void InitMidiClock(v8::Local<v8::Object> ns);
void InitMidiMapping(v8::Local<v8::Object> ns);
void InitMidiDispatcher(v8::Local<v8::Object> ns);
void InitMidiSysex(v8::Local<v8::Object> ns);
void InitMidiAutomation(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::synth {
void InitSynthAdsr(v8::Local<v8::Object> ns);
void InitSynthMultistage(v8::Local<v8::Object> ns);
void InitSynthFilter(v8::Local<v8::Object> ns);
void InitSynthLfo(v8::Local<v8::Object> ns);
void InitSynthModMatrix(v8::Local<v8::Object> ns);
void InitSynthOscAnalog(v8::Local<v8::Object> ns);
void InitSynthOscFm(v8::Local<v8::Object> ns);
void InitSynthOscNoise(v8::Local<v8::Object> ns);
void InitSynthOscWavetable(v8::Local<v8::Object> ns);
void InitSynthPolyphony(v8::Local<v8::Object> ns);
void InitSynthVoiceManager(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::core {
void InitCoreEngine(v8::Local<v8::Object> ns);
void InitCoreEngineHost(v8::Local<v8::Object> ns);
void InitCoreClockSync(v8::Local<v8::Object> ns);
void InitCoreFifo(v8::Local<v8::Object> ns);
void InitCoreInterleave(v8::Local<v8::Object> ns);
void InitCoreBitDepth(v8::Local<v8::Object> ns);
void InitCoreSampleRate(v8::Local<v8::Object> ns);
void InitCoreBufferManager(v8::Local<v8::Object> ns);
void InitCoreLatency(v8::Local<v8::Object> ns);
void InitCoreSourceLoader(v8::Local<v8::Object> ns);
void InitCoreLogger(v8::Local<v8::Object> ns);
void InitCoreThreadPriority(v8::Local<v8::Object> ns);
void InitCoreBufferPool(v8::Local<v8::Object> ns);
void InitCoreCacheManager(v8::Local<v8::Object> ns);
void InitCoreAsyncManager(v8::Local<v8::Object> ns);
}

namespace xer::addon::audio::plugin {
void InitPluginParamBridge(v8::Local<v8::Object> ns);
void InitPluginBuiltinHost(v8::Local<v8::Object> ns);
void InitPluginVst3Host(v8::Local<v8::Object> ns);
void InitPluginClapHost(v8::Local<v8::Object> ns);
void InitPluginAuHost(v8::Local<v8::Object> ns);
void InitPluginVst3External(v8::Local<v8::Object> ns);
void InitPluginClapExternal(v8::Local<v8::Object> ns);
void InitPluginVst3Scanner(v8::Local<v8::Object> ns);
}
