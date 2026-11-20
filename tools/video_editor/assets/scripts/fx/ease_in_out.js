// Ease In-Out — cubic ease on the blend weight.
xerFxTransition({
    id: "ease_in_out",
    title: "Ease In-Out",
    weight(progress, ctx) {
        const p = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
        return p < 0.5 ? 4.0 * p * p * p
                       : 1.0 - Math.pow(-2.0 * p + 2.0, 3) / 2.0;
    }
});
