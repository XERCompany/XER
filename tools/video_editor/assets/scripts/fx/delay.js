// Delay — feedback delay with a ring buffer kept in the closure.
// The buffer persists across chunks (one engine per worker thread),
// so echoes carry over seamlessly. State is keyed by ctx.clip_id:
// one process() fn serves every clip using "script:delay", so
// unkeyed state would bleed one clip's tail into another's.
xerFxAudio(function() {
    const slots = {};  // ctx.clip_id -> {buf, pos}
    return {
        id: "delay",
        title: "Delay",
        process(pcm, p, ctx) {
            const key = ctx.clip_id || "";
            const s = slots[key] ||
                (slots[key] = { buf: null, pos: 0 });
            const sr = ctx.sample_rate;
            const delayFrames =
                Math.max(1, Math.round(p.time_ms * 0.001 * sr));
            if (s.buf === null || s.buf.length !== delayFrames * 2) {
                s.buf = new Float32Array(delayFrames * 2);
                s.pos = 0;
            }
            const buf = s.buf;
            const n = buf.length;
            const fb = p.feedback;
            const mix = p.mix;
            const dry = 1.0 - mix;
            const frames = pcm.length >> 1;
            for (let i = 0; i < frames; ++i) {
                const l = pcm[i * 2], r = pcm[i * 2 + 1];
                const dl = buf[s.pos], dr = buf[s.pos + 1];
                buf[s.pos] = l + dl * fb;
                buf[s.pos + 1] = r + dr * fb;
                pcm[i * 2] = l * dry + dl * mix;
                pcm[i * 2 + 1] = r * dry + dr * mix;
                s.pos = (s.pos + 2) % n;
            }
        }
    };
}());
