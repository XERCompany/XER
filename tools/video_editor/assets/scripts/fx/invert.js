// Invert — RGB inversion blended by `amount` (alpha untouched).
xerFxVideo({
    id: "invert",
    title: "Invert Colors",
    apply(frame, p, ctx) {
        const amt = p.amount;
        const inv = 1.0 - amt;
        const d = frame.data;
        const stride = frame.stride;
        for (let y = 0; y < frame.height; ++y) {
            let i = y * stride;
            for (let x = 0; x < frame.width; ++x, i += 4) {
                d[i] = d[i] * inv + (255 - d[i]) * amt;
                d[i + 1] = d[i + 1] * inv + (255 - d[i + 1]) * amt;
                d[i + 2] = d[i + 2] * inv + (255 - d[i + 2]) * amt;
            }
        }
    }
});
