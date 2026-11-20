// Bounce Out — classic bounce easing (the blend "lands" with hops).
xerFxTransition({
    id: "bounce_out",
    title: "Bounce Out",
    weight(progress, ctx) {
        let p = progress < 0 ? 0 : (progress > 1 ? 1 : progress);
        const n1 = 7.5625, d1 = 2.75;
        if (p < 1.0 / d1) {
            return n1 * p * p;
        } else if (p < 2.0 / d1) {
            p -= 1.5 / d1;
            return n1 * p * p + 0.75;
        } else if (p < 2.5 / d1) {
            p -= 2.25 / d1;
            return n1 * p * p + 0.9375;
        }
        p -= 2.625 / d1;
        return n1 * p * p + 0.984375;
    }
});
