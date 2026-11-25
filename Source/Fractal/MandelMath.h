#pragma once
#include <vector>
#include <complex>
#include <juce_core/juce_core.h>

/** Mathematical characteristics of a point (and its neighbourhood) of the Mandelbrot set. */
struct FractalFeatures
{
    double cx = -0.75, cy = 0.1, scale = 0.05;
    int maxIter = 400;

    bool inside = false;
    double escape = 0.0;        // smooth escape iteration / maxIter, log-normalised 0..1
    int period = 0;             // attracting cycle period if inside (0 = unknown)
    double boundary = 0.0;      // 1 = very close to the set boundary (distance estimator)
    double chaos = 0.0;         // orbit Lyapunov-like divergence 0..1
    double angle = 0.0;         // arg(c) normalised 0..1
    double radius = 0.0;        // |c| / 2, clamped

    // neighbourhood statistics (grid sampled around the point at 'scale')
    double insideFraction = 0.0;
    double meanEscape = 0.0;
    double entropy = 0.0;       // escape histogram entropy 0..1
    double roughness = 0.0;     // mean neighbour difference 0..1

    std::vector<float> contour;  // 64 values 0..1 : melodic contour (orbit angles / escape ring)
    std::vector<float> energy;   // 64 values 0..1 : orbit radii / escape magnitudes (velocity/duration)

    /** Deterministic 64-bit hash of the location (used to seed generators). */
    juce::uint64 hash() const;
    juce::String describe() const;
};

namespace MandelMath
{
    struct IterResult
    {
        int iterations = 0;
        bool escaped = false;
        double smooth = 0.0;     // smooth iteration count
        double distance = 0.0;   // exterior distance estimate
    };

    IterResult iterate (double cx, double cy, int maxIter);

    /** Fast inside test for main cardioid and period-2 bulb. */
    bool inMainBulbs (double cx, double cy);

    std::vector<std::complex<double>> orbit (double cx, double cy, int count, int skip);

    int detectPeriod (double cx, double cy, int maxIter);

    FractalFeatures analyse (double cx, double cy, double scale, int maxIter);

    /** Blend two feature sets (t=0 -> a, t=1 -> b). Used for smooth musical evolution. */
    FractalFeatures blend (const FractalFeatures& a, const FractalFeatures& b, double t);

    /** Builds a single-cycle wavetable (frames x size) from the orbit around c. */
    std::vector<float> buildWavetable (double cx, double cy, int frames, int size);
}
