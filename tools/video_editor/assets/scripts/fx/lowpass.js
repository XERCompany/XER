// Lowpass — single-pole IIR; filter state lives in the closure and
// persists for the lifetime of the worker's runtime.
xerFxAudio(function() {
    let yl = 0.0, yr = 0.0;
    return {
        id: "lowpass",
        title: "Lowpass",
        process(pcm, p, ctx) {
            const rc = 1.0 / (2.0 * Math.PI * p.cutoff);
            const a = 1.0 / (1.0 + rc * ctx.sample_rate);
            const frames = pcm.length >> 1;
            for (let i = 0; i < frames; ++i) {
                yl += a * (pcm[i * 2] - yl);
                yr += a * (pcm[i * 2 + 1] - yr);
                pcm[i * 2] = yl;
                pcm[i * 2 + 1] = yr;
            }
        }
    };
}());
