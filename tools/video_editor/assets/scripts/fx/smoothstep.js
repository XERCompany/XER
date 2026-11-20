// Smoothstep — hermite 3p^2-2p^3, the classic smooth S curve.
xerFxTransition({
    id: "smoothstep",
    title: "Smoothstep",
    weight(progress, ctx) {
        const p = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
        return p * p * (3.0 - 2.0 * p);
    }
});
