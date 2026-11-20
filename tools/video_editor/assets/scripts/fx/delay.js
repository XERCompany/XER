// Delay — feedback delay with a ring buffer kept in the closure.
// The buffer persists across chunks (one engine per worker thread),
// so echoes carry over seamlessly.
xerFxAudio(function() {
    let buf = null;
    let pos = 0;
    return {
        id: "delay",
        title: "Delay",
        process(pcm, p, ctx) {
            const sr = ctx.sample_rate;
            const delayFrames =
                Math.max(1, Math.round(p.time_ms * 0.001 * sr));
            if (buf === null || buf.length !== delayFrames * 2) {
                buf = new Float32Array(delayFrames * 2);
                pos = 0;
            }
            const n = buf.length;
            const fb = p.feedback;
            const mix = p.mix;
            const dry = 1.0 - mix;
            const frames = pcm.length >> 1;
            for (let i = 0; i < frames; ++i) {
                const l = pcm[i * 2], r = pcm[i * 2 + 1];
                const dl = buf[pos], dr = buf[pos + 1];
                buf[pos] = l + dl * fb;
                buf[pos + 1] = r + dr * fb;
                pcm[i * 2] = l * dry + dl * mix;
                pcm[i * 2 + 1] = r * dry + dr * mix;
                pos = (pos + 2) % n;
            }
        }
    };
}());
