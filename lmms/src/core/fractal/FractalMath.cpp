/*
 * FractalMath.cpp - Mandelbrot analysis and scales (migrated from MANDELBROT v0.3).
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "fractal/FractalMath.h"

#include <cmath>
#include <complex>
#include <cstdio>

namespace lmms::fractal
{

namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double TwoPi = 2.0 * Pi;
}

std::string Features::describe() const
{
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s  escape %.2f  period %d  boundary %.2f  chaos %.2f  entropy %.2f  rough %.2f",
		inside ? "inside" : "outside", escape, period, boundary, chaos, entropy, roughness);
	return buf;
}

namespace math
{

bool inMainBulbs(double x, double y)
{
	const double q = (x - 0.25) * (x - 0.25) + y * y;
	if (q * (q + (x - 0.25)) <= 0.25 * y * y) { return true; }  // main cardioid
	if ((x + 1.0) * (x + 1.0) + y * y <= 0.0625) { return true; } // period-2 bulb
	return false;
}

IterResult iterate(double cx, double cy, int maxIter)
{
	IterResult r;
	if (inMainBulbs(cx, cy)) { r.iterations = maxIter; r.smooth = maxIter; return r; }
	double zx = 0, zy = 0, dzx = 0, dzy = 0, mag2 = 0;
	int i = 0;
	for (; i < maxIter; ++i)
	{
		const double ndzx = 2.0 * (zx * dzx - zy * dzy) + 1.0;
		const double ndzy = 2.0 * (zx * dzy + zy * dzx);
		dzx = ndzx; dzy = ndzy;
		const double nzx = zx * zx - zy * zy + cx;
		zy = 2.0 * zx * zy + cy;
		zx = nzx;
		mag2 = zx * zx + zy * zy;
		if (mag2 > 1e8) { break; }
	}
	r.iterations = i;
	if (i < maxIter)
	{
		r.escaped = true;
		const double logZn = 0.5 * std::log(mag2);
		const double nu = std::log(logZn / std::log(2.0)) / std::log(2.0);
		r.smooth = static_cast<double>(i) + 1.0 - nu;
		const double mz = std::sqrt(mag2), mdz = std::sqrt(dzx * dzx + dzy * dzy);
		r.distance = mdz > 0 ? 0.5 * mz * std::log(mz) / mdz : 0.0;
	}
	else { r.smooth = maxIter; }
	return r;
}

int detectPeriod(double cx, double cy, int maxIter)
{
	std::complex<double> z(0, 0);
	const std::complex<double> c(cx, cy);
	const int transient = std::min(maxIter * 3, 1500);
	for (int i = 0; i < transient; ++i)
	{
		z = z * z + c;
		if (std::norm(z) > 4.0) { return 0; }
	}
	const auto ref = z;
	for (int p = 1; p <= 64; ++p)
	{
		z = z * z + c;
		if (std::abs(z - ref) < 1e-7) { return p; }
	}
	return 0;
}

float pixel(double cx, double cy, int maxIter)
{
	const auto r = iterate(cx, cy, maxIter);
	if (!r.escaped) { return 1.0f; }
	return static_cast<float>(clampv(std::log1p(r.smooth) / std::log1p(static_cast<double>(maxIter)), 0.0, 0.999));
}

Features analyse(double cx, double cy, double scale, int maxIter, int grid)
{
	Features f;
	f.cx = cx; f.cy = cy; f.scale = scale;
	const auto r = iterate(cx, cy, maxIter);
	f.inside = !r.escaped;
	f.escape = r.escaped ? clampv(std::log1p(r.smooth) / std::log1p(static_cast<double>(maxIter)), 0.0, 1.0) : 1.0;
	f.period = f.inside ? detectPeriod(cx, cy, maxIter) : 0;
	f.angle = (std::atan2(cy, cx) + Pi) / TwoPi;
	f.radius = clampv(std::sqrt(cx * cx + cy * cy) / 2.0, 0.0, 1.0);
	if (r.escaped)
	{
		f.boundary = clampv(1.0 - std::log10(1.0 + 50.0 * r.distance / std::max(1e-12, scale)) / 2.0, 0.0, 1.0);
	}

	// orbit chaos: mean log|2z| along the orbit (Lyapunov exponent estimate)
	{
		std::complex<double> z(0, 0);
		const std::complex<double> c(cx, cy);
		double acc = 0.0; int n = 0;
		for (int i = 0; i < 200; ++i)
		{
			z = z * z + c;
			if (std::norm(z) > 16.0) { break; }
			acc += std::log(std::max(1e-9, 2.0 * std::abs(z)));
			++n;
		}
		f.chaos = clampv(0.5 + (n > 0 ? acc / n : 0.0) * 0.5, 0.0, 1.0);
	}

	// neighbourhood grid
	{
		const int G = std::max(3, grid);
		const int iters = std::min(maxIter, 400);
		std::vector<double> esc(static_cast<size_t>(G * G));
		int inside = 0;
		for (int j = 0; j < G; ++j)
		{
			for (int i = 0; i < G; ++i)
			{
				const double px = cx + ((i + 0.5) / G - 0.5) * scale;
				const double py = cy + ((j + 0.5) / G - 0.5) * scale;
				const auto rr = iterate(px, py, iters);
				const double e = rr.escaped ? std::log1p(rr.smooth) / std::log1p(static_cast<double>(iters)) : 1.0;
				if (!rr.escaped) { ++inside; }
				esc[static_cast<size_t>(j * G + i)] = e;
			}
		}
		f.insideFraction = inside / static_cast<double>(G * G);
		int hist[8] = {};
		for (auto e : esc) { hist[clampv(static_cast<int>(e * 8.0), 0, 7)]++; }
		double H = 0;
		for (int h : hist)
		{
			if (h > 0) { const double p = h / static_cast<double>(esc.size()); H -= p * std::log2(p); }
		}
		f.entropy = H / 3.0;
		double rough = 0; int cnt = 0;
		for (int j = 0; j < G; ++j)
		{
			for (int i = 0; i + 1 < G; ++i)
			{
				rough += std::abs(esc[static_cast<size_t>(j * G + i)] - esc[static_cast<size_t>(j * G + i + 1)]);
				++cnt;
			}
		}
		f.roughness = clampv((rough / std::max(1, cnt)) * 6.0, 0.0, 1.0);
		const double mix = 1.0 - std::abs(2.0 * f.insideFraction - 1.0);
		f.boundary = std::max(f.boundary, clampv(mix * 0.8 + f.roughness * 0.4, 0.0, 1.0));
	}

	// orbit of z -> z^2 + c: inside a period-3+ bulb, settle onto the attracting cycle
	// (a p-note riff); otherwise use the spiral / escape path
	{
		double zx = 0, zy = 0;
		int count = 0;
		const int skip = (f.inside && f.period >= 3) ? 160 : 0;
		for (int it = 0; it < skip + Features::OrbitLen && count < Features::OrbitLen; ++it)
		{
			const double nx = zx * zx - zy * zy + cx, ny = 2.0 * zx * zy + cy;
			zx = nx; zy = ny;
			const double r2 = zx * zx + zy * zy;
			if (r2 > 4.0) { break; }
			if (it < skip) { continue; }
			f.orbitAngle[static_cast<size_t>(count)] = static_cast<float>((std::atan2(zy, zx) + Pi) / TwoPi);
			f.orbitRadius[static_cast<size_t>(count)] = static_cast<float>(std::min(1.0, std::sqrt(r2) * 0.5));
			++count;
		}
		if (count < 8)
		{
			// escapes at once: read the escape-time ring around the point instead (still location specific)
			const int iters = std::min(maxIter, 400);
			count = Features::OrbitLen;
			for (int i = 0; i < count; ++i)
			{
				const double a = TwoPi * i / count;
				const double rad = scale * (0.18 + 0.12 * std::sin(a * 3.0));
				const auto rr = iterate(cx + rad * std::cos(a), cy + rad * std::sin(a), iters);
				const double e = rr.escaped ? std::log1p(rr.smooth) / std::log1p(static_cast<double>(iters)) : 1.0;
				f.orbitAngle[static_cast<size_t>(i)] = static_cast<float>(clampv(e, 0.0, 1.0));
				f.orbitRadius[static_cast<size_t>(i)] = static_cast<float>(std::fmod(rr.smooth * 0.137, 1.0));
			}
		}
		f.orbitCount = std::max(1, count);
	}
	return f;
}

} // namespace math

namespace scales
{

static int maskOf(std::initializer_list<int> l)
{
	int m = 0;
	for (auto i : l) { m |= (1 << i); }
	return m;
}

const char* name(int kind)
{
	static const char* names[] = {"Major", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
		"Harmonic minor", "Melodic minor", "Whole tone", "Pentatonic minor", "Pentatonic major", "Hirajoshi",
		"In sen", "Chromatic"};
	return names[clampv(kind, 0, NumKinds - 1)];
}

int mask(int kind)
{
	switch (kind)
	{
	case Major: return maskOf({0, 2, 4, 5, 7, 9, 11});
	case Dorian: return maskOf({0, 2, 3, 5, 7, 9, 10});
	case Phrygian: return maskOf({0, 1, 3, 5, 7, 8, 10});
	case Lydian: return maskOf({0, 2, 4, 6, 7, 9, 11});
	case Mixolydian: return maskOf({0, 2, 4, 5, 7, 9, 10});
	case Aeolian: return maskOf({0, 2, 3, 5, 7, 8, 10});
	case Locrian: return maskOf({0, 1, 3, 5, 6, 8, 10});
	case HarmonicMinor: return maskOf({0, 2, 3, 5, 7, 8, 11});
	case MelodicMinor: return maskOf({0, 2, 3, 5, 7, 9, 11});
	case WholeTone: return maskOf({0, 2, 4, 6, 8, 10});
	case PentatonicMinor: return maskOf({0, 3, 5, 7, 10});
	case PentatonicMajor: return maskOf({0, 2, 4, 7, 9});
	case Hirajoshi: return maskOf({0, 2, 3, 7, 8});
	case InSen: return maskOf({0, 1, 5, 7, 10});
	default: return 0xfff;
	}
}

std::vector<int> degrees(int kind)
{
	std::vector<int> d;
	const int m = mask(kind);
	for (int i = 0; i < 12; ++i) { if (m & (1 << i)) { d.push_back(i); } }
	if (d.empty()) { d.push_back(0); }
	return d;
}

bool contains(int pitch, int root, int kind)
{
	const int pc = ((pitch - root) % 12 + 12) % 12;
	return (mask(kind) >> pc) & 1;
}

const char* rootName(int pc)
{
	static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return n[((pc % 12) + 12) % 12];
}

} // namespace scales

void Key::set(int rootPc, int scaleKind)
{
	root = ((rootPc % 12) + 12) % 12;
	kind = clampv(scaleKind, 0, scales::NumKinds - 1);
	const auto d = scales::degrees(kind);
	numDegs = static_cast<int>(d.size());
	for (int i = 0; i < 12; ++i) { degs[static_cast<size_t>(i)] = i < numDegs ? d[static_cast<size_t>(i)] : 0; }
}

int Key::pitch(int degree, int octave) const
{
	const int n = std::max(1, numDegs);
	const int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
	const int idx = degree - oct * n;
	return clampv(12 * (octave + 1) + root + oct * 12 + degs[static_cast<size_t>(idx)], 0, 127);
}

int Key::degreeOf(int midiPitch, int octave) const
{
	int best = 0, bestDist = 1000;
	for (int d = -3 * numDegs; d <= 6 * numDegs; ++d)
	{
		const int dist = std::abs(pitch(d, octave) - midiPitch);
		if (dist < bestDist) { bestDist = dist; best = d; }
	}
	return best;
}

} // namespace lmms::fractal
