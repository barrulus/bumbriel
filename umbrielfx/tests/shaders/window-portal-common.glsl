// Analytic progress/seed only: held audio and reversed progress retrace exactly.
float portal_envelope() {
    float p = umbriel_clamped_progress;
    return 4.0 * p * (1.0 - p);
}
float portal_presence() {
    return umbriel_direction < 0.0 ? 1.0 - umbriel_clamped_progress : umbriel_clamped_progress;
}
