// Expo Out — exponential ease-out, very fast initial blend.
xerFxTransition({
    id: "expo_out",
    title: "Expo Out",
    weight(progress, ctx) {
        const p = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
        return p >= 1.0 ? 1.0 : 1.0 - Math.pow(2.0, -10.0 * p);
    }
});
