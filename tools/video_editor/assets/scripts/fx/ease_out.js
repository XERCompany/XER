// Ease Out — quadratic ease-out (fast start, gentle settle).
xerFxTransition({
    id: "ease_out",
    title: "Ease Out",
    weight(progress, ctx) {
        const p = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
        return 1.0 - (1.0 - p) * (1.0 - p);
    }
});
