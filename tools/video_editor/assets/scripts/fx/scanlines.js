// Scanlines — CRT-style darkened rows every `period` pixels.
xerFxVideo({
    id: "scanlines",
    title: "Scanlines",
    apply(frame, p, ctx) {
        const keep = 1.0 - p.strength;
        const period = Math.max(2, Math.round(p.period));
        const d = frame.data;
        const stride = frame.stride;
        for (let y = 0; y < frame.height; ++y) {
            if (y % period !== 0) continue;
            let i = y * stride;
            for (let x = 0; x < frame.width; ++x, i += 4) {
                d[i] *= keep;
                d[i + 1] *= keep;
                d[i + 2] *= keep;
            }
        }
    }
});
