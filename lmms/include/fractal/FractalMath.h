/*
 * FractalMath.h - Mandelbrot set analysis, scales and random helpers for the MANDELBROT
 *                 fractal engine. Pure C++ (no Qt), migrated from MANDELBROT v0.3
 *                 (Source/Fractal/MandelMath, Source/Core/Scales, Source/Audio/DSP.h).
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Affero General Public License version 3 as published by the Free Software
 * Foundation (the combined work includes AGPL-3.0-only code, see docs/AUDIT_AND_MIGRATION.md).
 */

#ifndef LMMS_FRACTAL_MATH_H
#define LMMS_FRACTAL_MATH_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lmms::fractal
{

// ------------------------------------------------------------------ random
inline uint32_t hash32(uint32_t x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}

inline uint64_t hash64(uint64_t x)
{
	x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
	return x;
}

struct Rng
{
	uint32_t s = 0x9e3779b9u;
	explicit Rng(uint32_t seed = 0x9e3779b9u) : s(seed ? seed : 1u) {}
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float uni() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); } // 0..1
	float bi() { return uni() * 2.0f - 1.0f; }                                       // -1..1
	int below(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

template<typename T> inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ------------------------------------------------------------------ Mandelbrot
struct Features
{
	double cx = -0.75, cy = 0.1, scale = 0.05;
	bool inside = false;
	double escape = 0.0;          //!< smooth escape time, log-normalised 0..1 (1 = inside)
	int period = 0;               //!< attracting cycle period if inside (0 = unknown / outside)
	double boundary = 0.0;        //!< 1 = right on the boundary of the set
	double chaos = 0.0;           //!< Lyapunov-like orbit divergence 0..1
	double angle = 0.0;           //!< arg(c) 0..1
	double radius = 0.0;          //!< |c| / 2
	double insideFraction = 0.0;  //!< share of the neighbourhood inside the set
	double entropy = 0.0;         //!< escape-time histogram entropy of the neighbourhood 0..1
	double roughness = 0.0;       //!< mean neighbour difference 0..1
	static constexpr int OrbitLen = 64;
	std::array<float, OrbitLen> orbitAngle {};  //!< arg z_n 0..1 (the melodic material)
	std::array<float, OrbitLen> orbitRadius {}; //!< |z_n| / 2 (velocities / lengths)
	int orbitCount = 1;

	std::string describe() const;
};

namespace math
{
	struct IterResult { int iterations = 0; bool escaped = false; double smooth = 0.0; double distance = 0.0; };
	bool inMainBulbs(double cx, double cy);
	IterResult iterate(double cx, double cy, int maxIter);
	int detectPeriod(double cx, double cy, int maxIter);
	//! Full analysis of a point and its neighbourhood (grid x grid samples over 'scale').
	Features analyse(double cx, double cy, double scale, int maxIter = 300, int grid = 8);
	//! Escape value 0..1 used to draw the set (1 = inside).
	float pixel(double cx, double cy, int maxIter);
}

// ------------------------------------------------------------------ scales
namespace scales
{
	enum Kind
	{
		Major = 0, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian,
		HarmonicMinor, MelodicMinor, WholeTone, PentatonicMinor, PentatonicMajor, Hirajoshi,
		InSen, Chromatic, NumKinds
	};
	const char* name(int kind);
	int mask(int kind);
	std::vector<int> degrees(int kind);
	bool contains(int midiPitch, int root, int kind);
	const char* rootName(int pc);
}

//! A key: root pitch class + the scale's semitone offsets.
struct Key
{
	int root = 9;                 //!< pitch class 0..11 (9 = A)
	int kind = scales::Dorian;
	std::array<int, 12> degs {0, 2, 3, 5, 7, 9, 10};
	int numDegs = 7;

	void set(int rootPc, int scaleKind);
	//! scale degree (any integer, wraps into octaves) -> MIDI pitch, octave = MIDI octave of degree 0
	int pitch(int degree, int octave) const;
	//! nearest scale degree for a MIDI pitch (used for voice leading)
	int degreeOf(int midiPitch, int octave) const;
};

} // namespace lmms::fractal

#endif // LMMS_FRACTAL_MATH_H
