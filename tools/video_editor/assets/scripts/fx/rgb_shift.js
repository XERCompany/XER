// RGB Shift — shifts the red channel `offset` pixels horizontally
// (chromatic fringing). B/G/A stay aligned; the gap fills from the
// row edge so no wrap-around ghosts appear.
xerFxVideo({
    id: "rgb_shift",
    title: "RGB Shift",
    apply(frame, p, ctx) {
        const off = Math.round(p.offset);
        if (off <= 0) return;
        const d = frame.data;
        const stride = frame.stride;
        const w = frame.width;
        for (let y = 0; y < frame.height; ++y) {
            const row = y * stride;
            for (let x = w - 1; x >= off; --x) {
                d[row + x * 4 + 2] = d[row + (x - off) * 4 + 2];
            }
            for (let x = Math.min(off, w) - 1; x >= 0; --x) {
                d[row + x * 4 + 2] = d[row + 2];  // edge pixel
            }
        }
    }
});
