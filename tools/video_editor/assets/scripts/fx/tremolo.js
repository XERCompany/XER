// Tremolo — sinusoidal amplitude modulation; phase anchored to
// ctx.time so the LFO stays continuous across chunk boundaries.
xerFxAudio({
    id: "tremolo",
    title: "Tremolo",
    process(pcm, p, ctx) {
        const w = 2.0 * Math.PI * p.rate;
        const depth = p.depth;
        const sr = ctx.sample_rate;
        const frames = pcm.length >> 1;
        const t0 = ctx.time;
        for (let i = 0; i < frames; ++i) {
            const g = 1.0 - depth * 0.5 *
                (1.0 + Math.sin(w * (t0 + i / sr)));
            pcm[i * 2] *= g;
            pcm[i * 2 + 1] *= g;
        }
    }
});
