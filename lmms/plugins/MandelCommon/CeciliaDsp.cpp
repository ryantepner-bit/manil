/*
 * CeciliaDsp.cpp - real-time DSP ported from pyo (the C engine behind cecilia5).
 * See CeciliaDsp.h for provenance and licence.
 */

#include "CeciliaDsp.h"

#include <algorithm>
#include <cmath>

namespace lmms::mandeldsp
{

namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double TwoPi = 2.0 * Pi;
}

// ================================================================== Waveguide (pyo delaymodule.c)

void Waveguide::prepare(float sampleRate, float minFreq)
{
	m_sr = sampleRate;
	m_minFreq = std::max(1.0f, minFreq);
	m_size = static_cast<int>(m_sr / m_minFreq + 0.5f) + 8;
	m_buffer.assign(static_cast<std::size_t>(m_size) + 1, 0.0f);
	clear();
	m_dirty = true;
}

void Waveguide::clear()
{
	std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
	std::fill(std::begin(m_lagrange), std::end(m_lagrange), 0.0f);
	m_write = 0;
	m_lp = m_xn1 = m_yn1 = 0.0f;
}

void Waveguide::update()
{
	const float fr = std::clamp(m_freq, m_minFreq, m_sr * 0.5f - 1.0f);
	const float dur = m_dur <= 0.0f ? 0.1f : m_dur;
	// pyo: sampdel = sr / freq - 0.5, 5-point Lagrange coefficients on the fraction
	const double sampdel = m_sr / fr - 0.5;
	m_isamp = std::clamp(static_cast<int>(sampdel), 1, m_size - 1);
	const double frac = sampdel - static_cast<int>(sampdel);
	m_coeffs[0] = static_cast<float>((frac - 1) * (frac - 2) * (frac - 3) * (frac - 4) / 24.0);
	m_coeffs[1] = static_cast<float>(-frac * (frac - 2) * (frac - 3) * (frac - 4) / 6.0);
	m_coeffs[2] = static_cast<float>(frac * (frac - 1) * (frac - 3) * (frac - 4) / 4.0);
	m_coeffs[3] = static_cast<float>(-frac * (frac - 1) * (frac - 2) * (frac - 4) / 6.0);
	m_coeffs[4] = static_cast<float>(frac * (frac - 1) * (frac - 2) * (frac - 3) / 24.0);
	// pyo: feed = 100^(-1 / (freq * dur))  (-40 dB after dur seconds)
	m_feed = static_cast<float>(std::pow(100.0, -1.0 / (fr * dur)));
	m_dirty = false;
}

float Waveguide::process(float in)
{
	if (m_dirty) { update(); }
	int ind = m_write - m_isamp;
	if (ind < 0) { ind += m_size; }
	float val = m_buffer[static_cast<std::size_t>(ind)];

	// simple lowpass filtering
	const float tmp = val;
	val = (val + m_lp) * 0.5f;
	m_lp = tmp;

	// lagrange filtering
	const float x = val * m_coeffs[0] + m_lagrange[0] * m_coeffs[1] + m_lagrange[1] * m_coeffs[2]
		+ m_lagrange[2] * m_coeffs[3] + m_lagrange[3] * m_coeffs[4];
	m_lagrange[3] = m_lagrange[2];
	m_lagrange[2] = m_lagrange[1];
	m_lagrange[1] = m_lagrange[0];
	m_lagrange[0] = val;

	// DC filtering
	const float y = x - m_xn1 + 0.995f * m_yn1;
	m_xn1 = x;
	m_yn1 = y;

	m_buffer[static_cast<std::size_t>(m_write)] = in + x * m_feed;
	if (m_write == 0) { m_buffer[static_cast<std::size_t>(m_size)] = m_buffer[0]; }
	if (++m_write == m_size) { m_write = 0; }
	return y;
}

// ================================================================== LiveGranulator (pyo granulatormodule.c)

void LiveGranulator::prepare(float sampleRate, float seconds)
{
	m_sr = sampleRate;
	m_size = std::max(1024, static_cast<int>(sampleRate * seconds));
	m_bufL.assign(static_cast<std::size_t>(m_size), 0.0f);
	m_bufR.assign(static_cast<std::size_t>(m_size), 0.0f);
	// pyo's default grain envelope: a Hanning table (8192 + guard point)
	constexpr int envSize = 8192;
	m_env.resize(envSize + 1);
	for (int i = 0; i <= envSize; ++i)
	{
		m_env[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(TwoPi * i / envSize));
	}
	m_write = 0;
	m_pointer = 0.0;
	m_last.fill(1.0);
	m_start.fill(0.0);
	m_rate.fill(1.0);
	m_gsize.fill(0.0);
	m_panL.fill(0.7071f);
	m_panR.fill(0.7071f);
}

void LiveGranulator::process(const float* inL, const float* inR, float* outL, float* outR, int frames, const Params& p)
{
	if (m_size == 0) { return; }
	const int ngrains = std::clamp(p.grains, 1, MaxGrains);
	const double grainSamps = std::max(4.0, static_cast<double>(p.grainMs) * 0.001 * m_sr);
	// pyo: the pointer runs through the envelope once per grain duration; grain j is offset by j / ngrains
	const double inc = 1.0 / grainSamps;
	const float norm = 1.0f / std::sqrt(static_cast<float>(ngrains) * 0.5f);
	const auto envSize = static_cast<double>(m_env.size() - 1);

	for (int i = 0; i < frames; ++i)
	{
		if (!p.freeze)
		{
			m_bufL[static_cast<std::size_t>(m_write)] = inL[i];
			m_bufR[static_cast<std::size_t>(m_write)] = inR[i];
			if (++m_write >= m_size) { m_write = 0; }
		}

		m_pointer += inc;
		if (m_pointer >= 1.0) { m_pointer -= 1.0; }

		float l = 0.0f, r = 0.0f;
		for (int j = 0; j < ngrains; ++j)
		{
			double ppos = m_pointer + static_cast<double>(j) / ngrains;
			if (ppos >= 1.0) { ppos -= 1.0; }

			if (ppos < m_last[static_cast<std::size_t>(j)])
			{
				// a new grain starts: choose its pitch, its place in the buffer and its pan
				float semis = p.pitch;
				if (p.pitchSet != nullptr && !p.pitchSet->empty())
				{
					const auto idx = static_cast<std::size_t>(m_rand.uni() * p.pitchSet->size()) % p.pitchSet->size();
					semis += (*p.pitchSet)[idx]; // intervals of the current chord / key, already limited to +-spray
				}
				else
				{
					semis += m_rand.bi() * p.pitchSpray;
				}
				const double rate = std::pow(2.0, semis / 12.0);
				const double gsize = grainSamps;
				const double back = gsize * std::max(1.0, rate) + 64.0
					+ m_rand.uni() * std::max(0.0f, p.posSpray) * m_sr;
				double start = static_cast<double>(m_write) - std::min(back, static_cast<double>(m_size - 4));
				if (start < 0) { start += m_size; }
				m_start[static_cast<std::size_t>(j)] = start;
				m_rate[static_cast<std::size_t>(j)] = rate;
				m_gsize[static_cast<std::size_t>(j)] = gsize;
				const float pan = 0.5f + 0.5f * m_rand.bi() * std::clamp(p.stereo, 0.0f, 1.0f);
				m_panL[static_cast<std::size_t>(j)] = std::sqrt(1.0f - pan);
				m_panR[static_cast<std::size_t>(j)] = std::sqrt(pan);
			}
			m_last[static_cast<std::size_t>(j)] = ppos;

			// envelope (linear interpolation in the table, as in pyo)
			const double eidx = ppos * envSize;
			const auto eip = static_cast<std::size_t>(eidx);
			const float ef = static_cast<float>(eidx - eip);
			const float amp = m_env[eip] + (m_env[std::min(eip + 1, m_env.size() - 1)] - m_env[eip]) * ef;

			// sample read (linear interpolation, circular)
			double idx = m_start[static_cast<std::size_t>(j)] + ppos * m_gsize[static_cast<std::size_t>(j)] * m_rate[static_cast<std::size_t>(j)];
			idx = std::fmod(idx, static_cast<double>(m_size));
			const auto ip = static_cast<int>(idx);
			const float fp = static_cast<float>(idx - ip);
			const int ip1 = ip + 1 >= m_size ? 0 : ip + 1;
			const float sl = m_bufL[static_cast<std::size_t>(ip)] + (m_bufL[static_cast<std::size_t>(ip1)] - m_bufL[static_cast<std::size_t>(ip)]) * fp;
			const float sr = m_bufR[static_cast<std::size_t>(ip)] + (m_bufR[static_cast<std::size_t>(ip1)] - m_bufR[static_cast<std::size_t>(ip)]) * fp;
			const float mono = 0.5f * (sl + sr);
			l += mono * amp * m_panL[static_cast<std::size_t>(j)];
			r += mono * amp * m_panR[static_cast<std::size_t>(j)];
		}
		outL[i] = l * norm;
		outR[i] = r * norm;
	}
}

// ================================================================== PhaseVocoder (pyo phasevocmodule.c)

void PhaseVocoder::fft(std::vector<std::complex<float>>& a, bool inverse)
{
	const std::size_t n = a.size();
	for (std::size_t i = 1, j = 0; i < n; ++i)
	{
		std::size_t bit = n >> 1;
		for (; j & bit; bit >>= 1) { j ^= bit; }
		j ^= bit;
		if (i < j) { std::swap(a[i], a[j]); }
	}
	for (std::size_t len = 2; len <= n; len <<= 1)
	{
		const double ang = TwoPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
		const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
		for (std::size_t i = 0; i < n; i += len)
		{
			std::complex<float> w(1.0f, 0.0f);
			for (std::size_t k = 0; k < len / 2; ++k)
			{
				const auto u = a[i + k];
				const auto v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

void PhaseVocoder::prepare(float sampleRate, int fftSize, int overlaps)
{
	m_sr = sampleRate;
	m_size = fftSize;
	m_hsize = fftSize / 2;
	m_olaps = overlaps;
	m_hop = fftSize / overlaps;
	// pyo PVAnal: scale = 2pi * hop / size, factor = sr / (hop * 2pi)
	m_scale = static_cast<float>(TwoPi * m_hop / m_size);
	m_factor = static_cast<float>(m_sr / (m_hop * TwoPi));
	m_window.resize(static_cast<std::size_t>(m_size));
	double wsum = 0.0;
	for (int k = 0; k < m_size; ++k)
	{
		m_window[static_cast<std::size_t>(k)] = static_cast<float>(0.5 - 0.5 * std::cos(TwoPi * k / m_size));
		wsum += static_cast<double>(m_window[static_cast<std::size_t>(k)]) * m_window[static_cast<std::size_t>(k)];
	}
	// analysis * synthesis window overlap-added every hop sums to wsum / hop; ifft is unnormalised (x size)
	m_ampscl = static_cast<float>(1.0 / (wsum / m_hop) / m_size);
	const auto hs = static_cast<std::size_t>(m_hsize + 1);
	m_lastPhase.assign(hs, 0.0f);
	m_sumPhase.assign(hs, 0.0f);
	m_magn.assign(hs, 0.0f);
	m_freq.assign(hs, 0.0f);
	m_lmagn.assign(hs, 0.0f);
	m_lfreq.assign(hs, 0.0f);
	m_fmagn.assign(hs, 0.0f);
	m_ffreq.assign(hs, 0.0f);
	m_in.assign(static_cast<std::size_t>(m_size), 0.0f);
	m_outAccum.assign(static_cast<std::size_t>(m_size), 0.0f);
	m_out.assign(static_cast<std::size_t>(m_hop), 0.0f);
	m_buf.assign(static_cast<std::size_t>(m_size), {0.0f, 0.0f});
	m_inCount = 0;
	m_outCount = 0;
	m_frozen = false;
}

void PhaseVocoder::reset()
{
	prepare(m_sr, m_size, m_olaps);
}

float PhaseVocoder::process(float in, const Params& p)
{
	const float out = m_out[static_cast<std::size_t>(m_outCount)];
	m_in[static_cast<std::size_t>(m_inCount)] = in;
	m_inCount = (m_inCount + 1) % m_size;
	if (++m_outCount >= m_hop)
	{
		m_outCount = 0;
		frame(p);
	}
	return out;
}

void PhaseVocoder::frame(const Params& p)
{
	const auto n = static_cast<std::size_t>(m_size);
	// ---- analysis (PVAnal): window the last `size` samples, oldest first
	for (std::size_t k = 0; k < n; ++k)
	{
		const float s = m_in[(static_cast<std::size_t>(m_inCount) + k) % n];
		m_buf[k] = {s * m_window[k], 0.0f};
	}
	fft(m_buf, false);
	const auto hs = static_cast<std::size_t>(m_hsize);
	for (std::size_t k = 0; k <= hs; ++k)
	{
		const float re = m_buf[k].real(), im = m_buf[k].imag();
		const float mag = std::sqrt(re * re + im * im);
		const float phase = std::atan2(im, re);
		float tmp = phase - m_lastPhase[k];
		m_lastPhase[k] = phase;
		// expected advance of bin k is k * scale; wrap the deviation into [-pi, pi]
		tmp -= static_cast<float>(k) * m_scale;
		tmp = static_cast<float>(tmp - TwoPi * std::floor((tmp + Pi) / TwoPi));
		m_magn[k] = mag;
		m_freq[k] = (tmp + static_cast<float>(k) * m_scale) * m_factor;
	}

	// ---- spectral freeze: hold the spectrum from the moment freeze was switched on
	if (p.freeze && !m_frozen)
	{
		m_fmagn = m_magn;
		m_ffreq = m_freq;
	}
	m_frozen = p.freeze;
	const std::vector<float>& srcM = m_frozen ? m_fmagn : m_magn;
	const std::vector<float>& srcF = m_frozen ? m_ffreq : m_freq;

	// ---- PVVerb: magnitudes decay instead of dropping; higher bins decay faster (damp)
	const float revtime = std::clamp(p.decay, 0.0f, 1.0f) * 0.25f + 0.75f;
	const float damp = std::clamp(1.0f - p.damp, 0.0f, 1.0f) * 0.003f + 0.997f;
	// blur: a slower, uniform smear of every bin over time
	const float blur = std::clamp(p.blur, 0.0f, 0.98f);
	float amp = 1.0f;
	for (std::size_t k = 0; k <= hs; ++k)
	{
		float mag = srcM[k], fre = srcF[k];
		if (blur > 0.0f)
		{
			mag = mag + (m_lmagn[k] - mag) * blur;
		}
		if (p.decay > 0.0f && mag <= m_lmagn[k])
		{
			mag = mag + (m_lmagn[k] - mag) * revtime * amp;
			fre = fre + (m_lfreq[k] - fre) * revtime * amp;
		}
		m_lmagn[k] = mag;
		m_lfreq[k] = fre;
		amp *= damp;
	}

	// ---- bin shift (PVShift): move every partial by shiftHz
	const float binHz = m_sr / static_cast<float>(m_size);
	const int shiftBins = static_cast<int>(std::lround(p.shiftHz / binHz));

	// ---- synthesis (PVSynth): accumulate each partial's phase from its frequency
	const auto hop = static_cast<float>(m_hop);
	for (std::size_t k = 0; k < n; ++k) { m_buf[k] = {0.0f, 0.0f}; }
	for (std::size_t k = 0; k <= hs; ++k)
	{
		const int tk = static_cast<int>(k) + shiftBins;
		if (tk < 1 || tk >= m_hsize) { continue; }
		const auto t = static_cast<std::size_t>(tk);
		const float f = m_lfreq[k] + p.shiftHz;
		m_sumPhase[t] = static_cast<float>(std::fmod(m_sumPhase[t] + TwoPi * f * hop / m_sr, TwoPi));
		const float mag = m_lmagn[k];
		m_buf[t] += std::polar(mag, m_sumPhase[t]);
	}
	for (std::size_t k = 1; k < hs; ++k) { m_buf[n - k] = std::conj(m_buf[k]); }
	fft(m_buf, true);
	for (std::size_t k = 0; k < n; ++k)
	{
		m_outAccum[k] += m_buf[k].real() * m_window[k] * m_ampscl;
	}
	const auto h = static_cast<std::size_t>(m_hop);
	for (std::size_t k = 0; k < h; ++k) { m_out[k] = m_outAccum[k]; }
	std::copy(m_outAccum.begin() + static_cast<long>(h), m_outAccum.end(), m_outAccum.begin());
	std::fill(m_outAccum.end() - static_cast<long>(h), m_outAccum.end(), 0.0f);
}

} // namespace lmms::mandeldsp
