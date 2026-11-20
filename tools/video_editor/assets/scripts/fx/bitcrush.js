// Bitcrusher — quantizes samples to `bits` resolution.
xerFxAudio({
    id: "bitcrush",
    title: "Bitcrusher",
    process(pcm, p, ctx) {
        const levels = Math.pow(2.0, Math.round(p.bits));
        const inv = 1.0 / levels;
        for (let i = 0; i < pcm.length; ++i) {
            pcm[i] = Math.round(pcm[i] * levels) * inv;
        }
    }
});
