// Sepia — classic sepia matrix blended by `amount` (0..1).
xerFxVideo({
    id: "sepia",
    title: "Sepia",
    apply(frame, p, ctx) {
        const amt = p.amount;
        const inv = 1.0 - amt;
        const d = frame.data;
        const stride = frame.stride;
        for (let y = 0; y < frame.height; ++y) {
            let i = y * stride;
            for (let x = 0; x < frame.width; ++x, i += 4) {
                const b = d[i], g = d[i + 1], r = d[i + 2];
                const sr = 0.393 * r + 0.769 * g + 0.189 * b;
                const sg = 0.349 * r + 0.686 * g + 0.168 * b;
                const sb = 0.272 * r + 0.534 * g + 0.131 * b;
                d[i + 2] = r * inv + sr * amt;
                d[i + 1] = g * inv + sg * amt;
                d[i] = b * inv + sb * amt;
            }
        }
    }
});
