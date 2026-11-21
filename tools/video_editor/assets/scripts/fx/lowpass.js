// Lowpass — single-pole IIR; filter state lives in the closure and
// persists for the lifetime of the worker's runtime. Keyed by
// ctx.clip_id so two clips sharing "script:lowpass" keep
// independent filter memory.
xerFxAudio(function() {
    const slots = {};  // ctx.clip_id -> {yl, yr}
    return {
        id: "lowpass",
        title: "Lowpass",
        process(pcm, p, ctx) {
            const key = ctx.clip_id || "";
            const s = slots[key] ||
                (slots[key] = { yl: 0.0, yr: 0.0 });
            const rc = 1.0 / (2.0 * Math.PI * p.cutoff);
            const a = 1.0 / (1.0 + rc * ctx.sample_rate);
            const frames = pcm.length >> 1;
            for (let i = 0; i < frames; ++i) {
                s.yl += a * (pcm[i * 2] - s.yl);
                s.yr += a * (pcm[i * 2 + 1] - s.yr);
                pcm[i * 2] = s.yl;
                pcm[i * 2 + 1] = s.yr;
            }
        }
    };
}());
