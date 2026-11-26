/*
 * PyoGen.h - the generative (random / rhythm) algorithms of pyo, the engine behind cecilia5,
 *            ported to plain C++ for MANDELBROT's note generators and modulators.
 *
 *   Xnoise  <- pyo randommodule.c Xnoise: 12 distributions (uniform, linear min/max, triangle,
 *              exponential min/max, bi-exponential, Cauchy, Weibull, Gaussian, Poisson,
 *              walker, loopseg)
 *   Beat    <- pyo metromodule.c Beater: weighted accent-probability rhythms (w1 downbeats,
 *              w2 secondary beats, w3 off-beats) with pyo's tap grouping by 7/6/5/4/3/2 and
 *              its accent velocity ranges
 *   Urn     <- pyo randommodule.c Urn: random choice without repetition until all are used
 *   euclid  <- Bjorklund / Euclidean rhythm (pyo Euclide)
 *
 * pyo: Copyright 2009-2015 Olivier Belanger, LGPL-3.0-or-later.
 * This port is part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_PYO_GEN_H
#define LMMS_FRACTAL_PYO_GEN_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lmms::fractal::pyo
{

//! pyo's own random generator range is replaced by a xorshift; same distribution shapes
struct Random
{
	uint32_t s;
	explicit Random(uint32_t seed = 0x9e3779b9u) : s(seed ? seed : 1u) {}
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	double uniform() { return (next() >> 8) * (1.0 / 16777216.0); } // RANDOM_UNIFORM, [0, 1)
};

enum class Dist
{
	Uniform = 0, LinearMin, LinearMax, Triangle, ExponMin, ExponMax, BiExpon, Cauchy, Weibull, Gaussian,
	Poisson, Walker, Loopseg, Count
};

inline const char* distName(int d)
{
	static const char* n[] = {"Uniform", "Linear min", "Linear max", "Triangle", "Exponential min",
		"Exponential max", "Bi-exponential", "Cauchy", "Weibull", "Gaussian", "Poisson", "Walker", "Loopseg"};
	return n[std::clamp(d, 0, static_cast<int>(Dist::Count) - 1)];
}

//! pyo Xnoise: next() returns 0..1 drawn from the chosen distribution (x1, x2 as in pyo)
class Xnoise
{
public:
	explicit Xnoise(uint32_t seed = 1) : m_rng(seed) {}
	Dist dist = Dist::Uniform;
	double x1 = 0.5, x2 = 0.5;

	double next()
	{
		auto clip = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
		switch (dist)
		{
		case Dist::Uniform: return m_rng.uniform();
		case Dist::LinearMin: { const double a = m_rng.uniform(), b = m_rng.uniform(); return a < b ? a : b; }
		case Dist::LinearMax: { const double a = m_rng.uniform(), b = m_rng.uniform(); return a > b ? a : b; }
		case Dist::Triangle: return (m_rng.uniform() + m_rng.uniform()) * 0.5;
		case Dist::ExponMin:
		{
			const double xx1 = x1 <= 0.0 ? 0.00001 : x1;
			return clip(-std::log(std::max(1e-12, m_rng.uniform())) / xx1);
		}
		case Dist::ExponMax:
		{
			const double xx1 = x1 <= 0.0 ? 0.00001 : x1;
			return clip(1.0 - (-std::log(std::max(1e-12, m_rng.uniform())) / xx1));
		}
		case Dist::BiExpon:
		{
			const double xx1 = x1 <= 0.0 ? 0.00001 : x1;
			double sum = m_rng.uniform() * 2.0, polar = 1.0;
			if (sum > 1.0) { polar = -1.0; sum = 2.0 - sum; }
			return clip(0.5 * (polar * std::log(std::max(1e-12, sum)) / xx1) + 0.5);
		}
		case Dist::Cauchy:
		{
			double rnd;
			do { rnd = m_rng.uniform(); } while (rnd == 0.5);
			const double dir = (m_rng.next() & 0x80000000u) ? 1.0 : -1.0;
			return clip(0.5 * (std::tan(rnd) * x1 * dir) + 0.5);
		}
		case Dist::Weibull:
		{
			const double xx2 = x2 <= 0.0 ? 0.00001 : x2;
			const double rnd = 1.0 / (1.0 - std::min(0.999999, m_rng.uniform()));
			return clip(x1 * std::pow(std::log(rnd), 1.0 / xx2));
		}
		case Dist::Gaussian:
		{
			double rnd = 0.0;
			for (int i = 0; i < 6; ++i) { rnd += m_rng.uniform(); }
			return clip(x2 * (rnd - 3.0) * 0.33 + x1);
		}
		case Dist::Poisson:
		{
			const double xx1 = std::max(0.1, x1 * 12.0), xx2 = std::max(0.1, x2 * 2.0);
			if (xx1 != m_lastPoissonX1)
			{
				m_lastPoissonX1 = xx1;
				m_poisson.clear();
				double factorial = 1.0;
				for (int i = 1; i < 12; ++i)
				{
					factorial *= i;
					const long tot = static_cast<long>(1000.0 * (std::pow(2.7182818, -xx1) * std::pow(xx1, i) / factorial));
					for (long j = 0; j < tot; ++j) { m_poisson.push_back(i); }
				}
				if (m_poisson.empty()) { m_poisson.push_back(1); }
			}
			return clip(m_poisson[m_rng.next() % m_poisson.size()] / 12.0 * xx2);
		}
		case Dist::Walker: return walkStep();
		case Dist::Loopseg: return loopseg();
		default: return m_rng.uniform();
		}
	}

private:
	double walkStep()
	{
		const double xx2 = std::max(0.002, x2);
		const unsigned modulo = std::max(1u, static_cast<unsigned>(xx2 * 1000.0));
		if (m_rng.next() % 100 < 50) { m_walker += (m_rng.next() % modulo) * 0.001; }
		else { m_walker -= (m_rng.next() % modulo) * 0.001; }
		m_walker = std::clamp(m_walker, 0.0, std::max(0.0, x1));
		return m_walker;
	}
	double loopseg()
	{
		if (m_loopChoice == 0)
		{
			m_loopCountPlay = m_loopTime = 0;
			const double v = walkStep();
			m_loop[static_cast<size_t>(m_loopCountRec++)] = v;
			if (m_loopCountRec >= m_loopLen) { m_loopChoice = 1; m_loopStop = static_cast<int>(m_rng.next() % 4) + 1; }
			return v;
		}
		m_loopCountRec = 0;
		m_walker = m_loop[static_cast<size_t>(m_loopCountPlay++)];
		if (m_loopCountPlay >= m_loopLen) { m_loopCountPlay = 0; ++m_loopTime; }
		if (m_loopTime == m_loopStop) { m_loopChoice = 0; m_loopLen = static_cast<int>(m_rng.next() % 10) + 3; }
		return m_walker;
	}

	Random m_rng;
	double m_walker = 0.5, m_lastPoissonX1 = -1.0;
	std::vector<int> m_poisson;
	std::array<double, 16> m_loop {};
	int m_loopChoice = 0, m_loopCountRec = 0, m_loopCountPlay = 0, m_loopTime = 0, m_loopStop = 1, m_loopLen = 6;
};

//! pyo Beater: one pattern of `taps` steps; hit[i] and accent (velocity 0..1) per step
struct BeatPattern
{
	std::vector<bool> hit;
	std::vector<float> accent;
};

inline BeatPattern beat(int taps, int w1, int w2, int w3, Random& rng)
{
	taps = std::clamp(taps, 1, 64);
	BeatPattern p;
	p.hit.assign(static_cast<size_t>(taps), false);
	p.accent.assign(static_cast<size_t>(taps), 0.5f);
	auto accent = [&](int n) -> float {
		if (n == 1) { return static_cast<float>((rng.next() % 15) + 112) / 127.0f; }
		if (n == 2) { return static_cast<float>((rng.next() % 20) + 70) / 127.0f; }
		return static_cast<float>((rng.next() % 20) + 40) / 127.0f;
	};
	for (int i = 0; i < taps; ++i)
	{
		int cls = 3;
		if (taps % 7 == 0) { const int m = i % 7; cls = m == 0 ? 1 : ((m == 4 || m == 2) ? 2 : 3); }
		else if (taps % 6 == 0) { const int m = i % 6; cls = m == 0 ? 1 : (m == 3 ? 2 : 3); }
		else if (taps % 5 == 0) { const int m = i % 5; cls = m == 0 ? 1 : (m == 3 ? 2 : 3); }
		else if (taps % 4 == 0) { const int m = i % 4; cls = m == 0 ? 1 : (m == 2 ? 2 : 3); }
		else if (taps % 3 == 0) { cls = i % 3 == 0 ? 1 : 3; }
		else if (taps % 2 == 0) { cls = i % 2 == 0 ? 1 : 3; }
		else { cls = i == 0 ? 1 : 3; }
		const int prob = cls == 1 ? w1 : (cls == 2 ? w2 : w3);
		p.accent[static_cast<size_t>(i)] = accent(cls);
		p.hit[static_cast<size_t>(i)] = static_cast<int>(rng.next() % 100) < prob;
	}
	return p;
}

//! Euclidean rhythm (Bjorklund): k hits spread over n steps, rotated
inline std::vector<bool> euclid(int k, int n, int rotate)
{
	n = std::clamp(n, 1, 64);
	k = std::clamp(k, 0, n);
	std::vector<bool> out(static_cast<size_t>(n), false);
	for (int i = 0; i < n; ++i)
	{
		const int j = ((i + rotate) % n + n) % n;
		out[static_cast<size_t>(i)] = ((j * k) % n) < k;
	}
	return out;
}

//! pyo Urn: draws 0..n-1 without repetition until every value was used, then refills
class Urn
{
public:
	explicit Urn(uint32_t seed = 7) : m_rng(seed) {}
	int next(int n)
	{
		n = std::max(1, n);
		if (static_cast<int>(m_size) != n || m_pool.empty())
		{
			m_size = static_cast<size_t>(n);
			m_pool.clear();
			for (int i = 0; i < n; ++i) { m_pool.push_back(i); }
		}
		const size_t idx = m_rng.next() % m_pool.size();
		const int v = m_pool[idx];
		m_pool.erase(m_pool.begin() + static_cast<long>(idx));
		return v;
	}

private:
	Random m_rng;
	size_t m_size = 0;
	std::vector<int> m_pool;
};

} // namespace lmms::fractal::pyo

#endif // LMMS_FRACTAL_PYO_GEN_H
