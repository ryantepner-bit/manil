/*
 * CeciliaDsp.h - real-time DSP ported from pyo (the C engine behind cecilia5) for MANDELBROT's
 *                ambient effects.
 *
 *   Waveguide       <- pyo Waveguide (delaymodule.c): Lagrange-interpolated tuned delay with
 *                      lowpass and DC blocking, feedback from the decay time
 *   LiveGranulator  <- pyo Granulator (granulatormodule.c) reading a live circular buffer instead
 *                      of a table; per-grain position / pitch / pan scatter after cecilia5's
 *                      Time/Granulator and Synthesis/StochGrains modules
 *   PhaseVocoder    <- pyo PVAnal / PVSynth (phasevocmodule.c) with PVVerb's spectral decay,
 *                      a spectral freeze and PVShift-style bin shifting (cecilia5 Spectral modules)
 *   Harmonizer      <- pyo Harmonizer (harmonizermodule.c): two overlapping sine-windowed taps
 *                      on a 1 s delay line, feedback through a DC blocker
 *   Hilbert         <- pyo Hilbert (hilbertmodule.c): two 6th-order allpass chains (90 degrees
 *                      apart); FreqShift = pyo's FreqShift (Hilbert + quadrature oscillator)
 *   Phaser          <- pyo Phaser (filtremodule.c): cascaded 2-pole allpass stages with spread,
 *                      Q and feedback
 *
 * pyo: Copyright 2009-2015 Olivier Belanger, LGPL-3.0-or-later.
 * cecilia5: Copyright Olivier Belanger, Jean-Michel Dumas, GPL-3.0-or-later.
 * This port is part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_CECILIA_DSP_H
#define LMMS_MANDEL_CECILIA_DSP_H

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

namespace lmms::mandeldsp
{

struct Rand
{
	uint32_t s = 0x12345678u;
	explicit Rand(uint32_t seed = 0x12345678u) : s(seed ? seed : 1u) {}
	float uni() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s >> 8) * (1.0f / 16777216.0f); }
	float bi() { return uni() * 2.0f - 1.0f; }
};

//! pyo Waveguide: a tuned comb with Lagrange fractional delay, lowpass and DC blocker
class Waveguide
{
public:
	void prepare(float sampleRate, float minFreq = 20.0f);
	void setFreq(float hz) { m_freq = hz; m_dirty = true; }
	void setDecay(float seconds) { m_dur = seconds; m_dirty = true; }
	float process(float in);
	void clear();

private:
	void update();
	std::vector<float> m_buffer;
	int m_size = 0, m_write = 0, m_isamp = 1;
	float m_sr = 44100.0f, m_freq = 220.0f, m_dur = 2.0f, m_minFreq = 20.0f;
	float m_coeffs[5] {}, m_lagrange[4] {}, m_feed = 0.0f, m_lp = 0.0f, m_xn1 = 0.0f, m_yn1 = 0.0f;
	bool m_dirty = true;
};

//! Granular cloud over the last few seconds of live input
class LiveGranulator
{
public:
	static constexpr int MaxGrains = 32;
	struct Params
	{
		float grainMs = 120.0f;     //!< grain duration
		int grains = 12;            //!< overlapping grains (density)
		float pitch = 0.0f;         //!< semitones
		float pitchSpray = 0.0f;    //!< semitones of random deviation per grain
		float posSpray = 0.5f;      //!< seconds back into the buffer the grains may start
		float stereo = 0.7f;        //!< random pan width
		bool freeze = false;        //!< stop recording: the cloud keeps playing the frozen buffer
		const std::vector<float>* pitchSet = nullptr; //!< if set, each grain picks one of these semitone offsets (in key) instead of a random detune
	};

	void prepare(float sampleRate, float seconds = 6.0f);
	//! interleaved-free stereo processing: writes wet signal to outL/outR
	void process(const float* inL, const float* inR, float* outL, float* outR, int frames, const Params& p);

private:
	std::vector<float> m_bufL, m_bufR, m_env;
	int m_size = 0, m_write = 0;
	float m_sr = 44100.0f;
	double m_pointer = 0.0;
	std::array<double, MaxGrains> m_phase {}, m_last {}, m_start {}, m_rate {}, m_gsize {};
	std::array<float, MaxGrains> m_panL {}, m_panR {};
	Rand m_rand;
};

//! Phase vocoder (analysis -> spectral processing -> resynthesis), one channel
class PhaseVocoder
{
public:
	struct Params
	{
		float decay = 0.5f;     //!< PVVerb revtime 0..1 (spectral reverb)
		float damp = 0.5f;      //!< PVVerb damp 0..1 (high bins decay faster)
		bool freeze = false;    //!< hold the current spectrum
		float shiftHz = 0.0f;   //!< move every bin by this much
		float blur = 0.0f;      //!< 0..1 smear magnitudes across neighbouring bins
	};

	void prepare(float sampleRate, int fftSize = 2048, int overlaps = 4);
	float process(float in, const Params& p);
	void reset();
	int latency() const { return m_size; }

private:
	void frame(const Params& p);
	static void fft(std::vector<std::complex<float>>& a, bool inverse);

	int m_size = 2048, m_hsize = 1024, m_olaps = 4, m_hop = 512;
	float m_sr = 44100.0f, m_scale = 0.0f, m_factor = 0.0f, m_ampscl = 1.0f;
	std::vector<float> m_window, m_in, m_outAccum, m_out;
	std::vector<float> m_lastPhase, m_sumPhase, m_magn, m_freq, m_lmagn, m_lfreq, m_fmagn, m_ffreq;
	std::vector<std::complex<float>> m_buf;
	int m_inCount = 0, m_outCount = 0;
	bool m_frozen = false;
};

//! pyo Harmonizer: pitch shifting without changing length
class Harmonizer
{
public:
	void prepare(float sampleRate);
	//! semitones, feedback 0..1, window in seconds (pyo default 0.1)
	float process(float in, float semitones, float feedback, float winsize);

private:
	std::vector<float> m_buffer;
	int m_size = 0, m_inCount = 0;
	float m_sr = 44100.0f;
	double m_pointer = 0.0;
	float m_x1 = 0.0f, m_y1 = 0.0f;
};

//! pyo Hilbert: analytic signal (real, imaginary)
class Hilbert
{
public:
	void prepare(float sampleRate);
	void process(float in, float& re, float& im);

private:
	float m_coefs[12] {}, m_x1[12] {}, m_y1[12] {};
};

//! pyo FreqShift: moves every partial by a fixed number of Hz (not harmonic: bells, metal)
class FreqShifter
{
public:
	void prepare(float sampleRate) { m_sr = sampleRate; m_hilbert.prepare(sampleRate); }
	float process(float in, float shiftHz);

private:
	Hilbert m_hilbert;
	float m_sr = 44100.0f;
	double m_phase = 0.0;
};

//! pyo Phaser: up to 24 allpass stages
class Phaser
{
public:
	static constexpr int MaxStages = 24;
	void prepare(float sampleRate);
	//! call before process when freq / spread / q / stages change (cheap enough per block)
	void set(float freq, float spread, float q, int stages);
	float process(float in, float feedback);

private:
	float m_sr = 44100.0f;
	int m_stages = 8;
	float m_alpha[MaxStages] {}, m_beta[MaxStages] {}, m_y1[MaxStages] {}, m_y2[MaxStages] {};
	float m_tmp = 0.0f;
};

} // namespace lmms::mandeldsp

#endif // LMMS_MANDEL_CECILIA_DSP_H
