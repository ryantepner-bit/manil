#include "MandelMath.h"
#include <cmath>

namespace MandelMath
{
    bool inMainBulbs (double x, double y)
    {
        const double q = (x - 0.25) * (x - 0.25) + y * y;
        if (q * (q + (x - 0.25)) <= 0.25 * y * y) return true;   // main cardioid
        if ((x + 1.0) * (x + 1.0) + y * y <= 0.0625) return true; // period-2 bulb
        return false;
    }

    IterResult iterate (double cx, double cy, int maxIter)
    {
        IterResult r;
        if (inMainBulbs (cx, cy)) { r.iterations = maxIter; return r; }
        double zx = 0, zy = 0, dzx = 0, dzy = 0;
        int i = 0;
        double mag2 = 0;
        for (; i < maxIter; ++i)
        {
            // dz = 2 z dz + 1
            const double ndzx = 2.0 * (zx * dzx - zy * dzy) + 1.0;
            const double ndzy = 2.0 * (zx * dzy + zy * dzx);
            dzx = ndzx; dzy = ndzy;
            const double nzx = zx * zx - zy * zy + cx;
            zy = 2.0 * zx * zy + cy;
            zx = nzx;
            mag2 = zx * zx + zy * zy;
            if (mag2 > 1e8) break;
        }
        r.iterations = i;
        if (i < maxIter)
        {
            r.escaped = true;
            const double logZn = 0.5 * std::log (mag2);
            const double nu = std::log (logZn / std::log (2.0)) / std::log (2.0);
            r.smooth = (double) i + 1.0 - nu;
            const double mz = std::sqrt (mag2), mdz = std::sqrt (dzx * dzx + dzy * dzy);
            r.distance = mdz > 0 ? 0.5 * mz * std::log (mz) / mdz : 0.0;
        }
        else
            r.smooth = (double) maxIter;
        return r;
    }

    std::vector<std::complex<double>> orbit (double cx, double cy, int count, int skip)
    {
        std::vector<std::complex<double>> out;
        out.reserve ((size_t) count);
        std::complex<double> z (0, 0), c (cx, cy);
        for (int i = 0; i < skip + count; ++i)
        {
            z = z * z + c;
            if (std::norm (z) > 16.0) break;
            if (i >= skip) out.push_back (z);
        }
        return out;
    }

    int detectPeriod (double cx, double cy, int maxIter)
    {
        std::complex<double> z (0, 0), c (cx, cy);
        const int transient = juce::jmin (maxIter, 1500);
        for (int i = 0; i < transient; ++i)
        {
            z = z * z + c;
            if (std::norm (z) > 4.0) return 0;
        }
        const auto ref = z;
        for (int p = 1; p <= 64; ++p)
        {
            z = z * z + c;
            if (std::abs (z - ref) < 1e-7) return p;
        }
        return 0;
    }

    static float norm01 (double v) { return (float) juce::jlimit (0.0, 1.0, v); }

    FractalFeatures analyse (double cx, double cy, double scale, int maxIter)
    {
        FractalFeatures f;
        f.cx = cx; f.cy = cy; f.scale = scale; f.maxIter = maxIter;
        const auto r = iterate (cx, cy, maxIter);
        f.inside = ! r.escaped;
        f.escape = r.escaped ? juce::jlimit (0.0, 1.0, std::log1p (r.smooth) / std::log1p ((double) maxIter)) : 1.0;
        f.period = f.inside ? detectPeriod (cx, cy, maxIter) : 0;
        f.angle = (std::atan2 (cy, cx) + juce::MathConstants<double>::pi) / juce::MathConstants<double>::twoPi;
        f.radius = juce::jlimit (0.0, 1.0, std::sqrt (cx * cx + cy * cy) / 2.0);

        // boundary proximity: distance estimate relative to view scale
        if (r.escaped)
            f.boundary = juce::jlimit (0.0, 1.0, 1.0 - std::log10 (1.0 + 50.0 * r.distance / juce::jmax (1e-12, scale)) / 2.0);
        else
            f.boundary = 0.0;

        // orbit chaos: mean log|2z| along orbit (Lyapunov exponent estimate)
        {
            auto orb = orbit (cx, cy, 200, 0);
            double acc = 0.0;
            for (auto& z : orb) acc += std::log (juce::jmax (1e-9, 2.0 * std::abs (z)));
            const double lyap = orb.empty() ? 0.0 : acc / (double) orb.size();
            f.chaos = juce::jlimit (0.0, 1.0, 0.5 + lyap * 0.5);
        }

        // neighbourhood grid
        {
            constexpr int G = 12;
            const int iters = juce::jmin (maxIter, 600);
            std::vector<double> esc ((size_t) (G * G));
            int inside = 0;
            for (int j = 0; j < G; ++j)
                for (int i = 0; i < G; ++i)
                {
                    const double px = cx + ((i + 0.5) / G - 0.5) * scale;
                    const double py = cy + ((j + 0.5) / G - 0.5) * scale;
                    const auto rr = iterate (px, py, iters);
                    double e = rr.escaped ? std::log1p (rr.smooth) / std::log1p ((double) iters) : 1.0;
                    if (! rr.escaped) ++inside;
                    esc[(size_t) (j * G + i)] = e;
                }
            f.insideFraction = inside / double (G * G);
            double mean = 0; for (auto e : esc) mean += e; mean /= esc.size();
            f.meanEscape = mean;
            int hist[8] = {};
            for (auto e : esc) hist[juce::jlimit (0, 7, (int) (e * 8.0))]++;
            double H = 0;
            for (int h : hist) if (h > 0) { const double p = h / (double) esc.size(); H -= p * std::log2 (p); }
            f.entropy = H / 3.0;
            double rough = 0; int cnt = 0;
            for (int j = 0; j < G; ++j)
                for (int i = 0; i + 1 < G; ++i) { rough += std::abs (esc[(size_t) (j * G + i)] - esc[(size_t) (j * G + i + 1)]); ++cnt; }
            f.roughness = juce::jlimit (0.0, 1.0, (rough / cnt) * 6.0);
            // a neighbourhood that mixes inside and outside points straddles the boundary
            const double mixMeasure = 1.0 - std::abs (2.0 * f.insideFraction - 1.0);
            f.boundary = juce::jmax (f.boundary, juce::jlimit (0.0, 1.0, mixMeasure * 0.8 + f.roughness * 0.4));
        }

        // contour + energy (64 values)
        f.contour.resize (64);
        f.energy.resize (64);
        auto orb = orbit (cx, cy, 64, f.inside ? 300 : 0);
        if (orb.size() >= 16)
        {
            for (size_t i = 0; i < 64; ++i)
            {
                const auto& z = orb[i % orb.size()];
                f.contour[i] = norm01 ((std::arg (z) + juce::MathConstants<double>::pi) / juce::MathConstants<double>::twoPi);
                f.energy[i] = norm01 (std::abs (z) / 2.0);
            }
        }
        else
        {
            // escaping quickly: read the escape-time ring around the point
            const int iters = juce::jmin (maxIter, 800);
            for (int i = 0; i < 64; ++i)
            {
                const double a = juce::MathConstants<double>::twoPi * i / 64.0;
                const double rad = scale * (0.18 + 0.12 * std::sin (a * 3.0));
                const auto rr = iterate (cx + rad * std::cos (a), cy + rad * std::sin (a), iters);
                const double e = rr.escaped ? std::log1p (rr.smooth) / std::log1p ((double) iters) : 1.0;
                f.contour[(size_t) i] = norm01 (e);
                f.energy[(size_t) i] = norm01 (std::fmod (rr.smooth * 0.137, 1.0));
            }
        }
        return f;
    }

    FractalFeatures blend (const FractalFeatures& a, const FractalFeatures& b, double t)
    {
        t = juce::jlimit (0.0, 1.0, t);
        auto L = [t] (double x, double y) { return x + (y - x) * t; };
        FractalFeatures f = t < 0.5 ? a : b;
        f.cx = L (a.cx, b.cx); f.cy = L (a.cy, b.cy); f.scale = L (a.scale, b.scale);
        f.escape = L (a.escape, b.escape);
        f.boundary = L (a.boundary, b.boundary);
        f.chaos = L (a.chaos, b.chaos);
        f.angle = L (a.angle, b.angle);
        f.radius = L (a.radius, b.radius);
        f.insideFraction = L (a.insideFraction, b.insideFraction);
        f.meanEscape = L (a.meanEscape, b.meanEscape);
        f.entropy = L (a.entropy, b.entropy);
        f.roughness = L (a.roughness, b.roughness);
        if (a.contour.size() == 64 && b.contour.size() == 64)
            for (size_t i = 0; i < 64; ++i)
            {
                f.contour[i] = (float) L (a.contour[i], b.contour[i]);
                f.energy[i] = (float) L (a.energy[i], b.energy[i]);
            }
        return f;
    }

    std::vector<float> buildWavetable (double cx, double cy, int frames, int size)
    {
        std::vector<float> table ((size_t) (frames * size), 0.0f);
        for (int fr = 0; fr < frames; ++fr)
        {
            // each frame walks further along a spiral around c => evolving harmonic spectrum
            const double t = fr / (double) juce::jmax (1, frames - 1);
            const double ox = cx + 0.02 * t * std::cos (t * 9.0);
            const double oy = cy + 0.02 * t * std::sin (t * 9.0);
            auto orb = orbit (ox, oy, 32, 50);
            if (orb.size() < 4) orb = orbit (ox * 0.5, oy * 0.5, 32, 0);
            // treat orbit magnitudes/phases as harmonic amplitudes/phases (additive synthesis)
            const int numH = juce::jmin (24, (int) orb.size());
            float* dst = table.data() + (size_t) (fr * size);
            for (int h = 0; h < numH; ++h)
            {
                const double amp = (0.3 + std::abs (orb[(size_t) h])) / (1.0 + h * (1.2 - 0.9 * t));
                const double ph = std::arg (orb[(size_t) h]);
                for (int s = 0; s < size; ++s)
                    dst[s] += (float) (amp * std::sin (juce::MathConstants<double>::twoPi * (h + 1) * s / size + ph));
            }
            float peak = 0.0f;
            for (int s = 0; s < size; ++s) peak = juce::jmax (peak, std::abs (dst[s]));
            if (peak > 0) for (int s = 0; s < size; ++s) dst[s] /= peak;
        }
        return table;
    }
}

juce::uint64 FractalFeatures::hash() const
{
    // quantise to the current view scale so tiny jitter doesn't change the seed
    const double q = juce::jmax (1e-15, scale / 64.0);
    const auto qx = (juce::int64) std::floor (cx / q), qy = (juce::int64) std::floor (cy / q);
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xff; h *= 1099511628211ull; } };
    mix ((juce::uint64) qx); mix ((juce::uint64) qy); mix ((juce::uint64) (juce::int64) std::round (std::log2 (q) * 4.0));
    return h;
}

juce::String FractalFeatures::describe() const
{
    juce::String s;
    s << (inside ? "INSIDE" : "OUTSIDE")
      << "  esc " << juce::String (escape, 2)
      << "  period " << (period > 0 ? juce::String (period) : juce::String ("-"))
      << "  boundary " << juce::String (boundary, 2)
      << "  chaos " << juce::String (chaos, 2)
      << "  entropy " << juce::String (entropy, 2)
      << "  rough " << juce::String (roughness, 2)
      << "  inside " << juce::String (insideFraction * 100.0, 1) << "%";
    return s;
}
