// Film Grain — animated per-pixel luminance noise (hash-based, so it
// flickers naturally frame to frame via ctx.time).
xerFxVideo({
    id: "film_grain",
    title: "Film Grain",
    apply(frame, p, ctx) {
        const amt = p.amount * 255.0;
        const d = frame.data;
        const stride = frame.stride;
        let seed = (ctx.time * 997.0) | 0;
        for (let y = 0; y < frame.height; ++y) {
            let i = y * stride;
            for (let x = 0; x < frame.width; ++x, i += 4) {
                seed = (seed * 1103515245 + 12345 + x * 2654435761 + y) |
                    0;
                const n = ((seed >>> 8) & 0xffff) / 65535.0 - 0.5;
                const g = n * amt;
                d[i] += g;
                d[i + 1] += g;
                d[i + 2] += g;
            }
        }
    }
});
