/*
 * MandelResonator.cpp - tuned waveguide bank for MANDELBROT. See MandelResonator.h.
 */

#include "MandelResonator.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "MandelShared.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{
Plugin::Descriptor PLUGIN_EXPORT mandelresonator_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel Resonator",
	QT_TRANSLATE_NOOP("PluginBrowser", "Waveguide resonator bank tuned to the fractal's chords (pyo Waveguide)"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelResonatorControls::MandelResonatorControls(MandelResonatorEffect* effect) :
	EffectControls(effect),
	m_decay(4.0f, 0.1f, 30.0f, 0.01f, this, tr("Decay")),
	m_voices(4.0f, 1.0f, 6.0f, 1.0f, this, tr("Voices")),
	m_octave(0.0f, -2.0f, 2.0f, 1.0f, this, tr("Octave")),
	m_root(9.0f, 0.0f, 11.0f, 1.0f, this, tr("Root (when not following)")),
	m_bright(0.6f, 0.0f, 1.0f, 0.01f, this, tr("Brightness")),
	m_drive(0.3f, 0.0f, 1.0f, 0.01f, this, tr("Drive")),
	m_fractal(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Fractal amount")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output")),
	m_follow(true, this, tr("Follow the fractal's chords"))
{
	m_decay.setScaleLogarithmic(true);
}

void MandelResonatorControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_decay.saveSettings(doc, parent, "decay");
	m_voices.saveSettings(doc, parent, "voices");
	m_octave.saveSettings(doc, parent, "octave");
	m_root.saveSettings(doc, parent, "root");
	m_bright.saveSettings(doc, parent, "bright");
	m_drive.saveSettings(doc, parent, "drive");
	m_fractal.saveSettings(doc, parent, "fractal");
	m_gain.saveSettings(doc, parent, "gain");
	m_follow.saveSettings(doc, parent, "follow");
}

void MandelResonatorControls::loadSettings(const QDomElement& parent)
{
	m_decay.loadSettings(parent, "decay");
	m_voices.loadSettings(parent, "voices");
	m_octave.loadSettings(parent, "octave");
	m_root.loadSettings(parent, "root");
	m_bright.loadSettings(parent, "bright");
	m_drive.loadSettings(parent, "drive");
	m_fractal.loadSettings(parent, "fractal");
	m_gain.loadSettings(parent, "gain");
	m_follow.loadSettings(parent, "follow");
}

gui::EffectControlDialog* MandelResonatorControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL RESONATOR"), tr("waveguide bank"));
	p->knob(tr("DECAY"), tr("Decay:"), " s", &m_decay, 0, 0);
	p->knob(tr("VOICES"), tr("Voices:"), "", &m_voices, 0, 1);
	p->knob(tr("OCT"), tr("Octave:"), "", &m_octave, 0, 2);
	p->knob(tr("ROOT"), tr("Root when not following (0 = C):"), "", &m_root, 0, 3);
	p->knob(tr("BRIGHT"), tr("Brightness:"), "", &m_bright, 1, 0);
	p->knob(tr("DRIVE"), tr("Drive:"), "", &m_drive, 1, 1);
	p->knob(tr("FRACT"), tr("How much the fractal steers the decay:"), "", &m_fractal, 1, 2);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 3);
	p->led(tr("Follow chords"), &m_follow, 2, 0, 3);
	return p;
}

MandelResonatorEffect::MandelResonatorEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelresonator_plugin_descriptor, parent, key),
	m_controls(this)
{
}

Effect::ProcessStatus MandelResonatorEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		for (int i = 0; i < MaxVoices; ++i) { m_wgL[i].prepare(sr, 20.0f); m_wgR[i].prepare(sr, 20.0f); }
		m_freq.fill(0.0f);
		m_decay = -1.0f;
	}

	// ---- the notes to ring: the sounding chord from its bass upwards (or a fixed m9 shape)
	const auto fr = mandeldsp::readFractal();
	const int voices = std::clamp(static_cast<int>(std::lround(m_controls.m_voices.value())), 1, MaxVoices);
	std::array<int, MaxVoices> notes {};
	const int oct = static_cast<int>(std::lround(m_controls.m_octave.value())) * 12;
	if (m_controls.m_follow.value() && fr.live)
	{
		int n = 0;
		int pitch = fr.bass + 12 + oct;
		notes[n++] = pitch;
		for (int step = 1; n < voices && step < 48; ++step)
		{
			if ((fr.chordMask >> ((pitch + step) % 12)) & 1) { notes[n++] = pitch + step; }
		}
		for (; n < voices; ++n) { notes[n] = notes[n - 1] + 12; }
	}
	else
	{
		static constexpr int shape[MaxVoices] = {0, 7, 10, 14, 15, 19};
		const int root = 45 + static_cast<int>(std::lround(m_controls.m_root.value())) % 12 - 9 + oct;
		for (int i = 0; i < voices; ++i) { notes[i] = root + shape[i]; }
	}
	for (int i = 0; i < voices; ++i)
	{
		const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(notes[i]) - 69.0f) / 12.0f);
		if (hz != m_freq[i])
		{
			m_freq[i] = hz;
			m_wgL[i].setFreq(hz);
			m_wgR[i].setFreq(hz * 1.0017f); // a few cents apart: width
		}
	}
	const float amt = fr.live ? m_controls.m_fractal.value() : 0.0f;
	const float decay = std::clamp(m_controls.m_decay.value() * (1.0f + amt * (fr.boundary - 0.5f) * 1.5f), 0.1f, 40.0f);
	if (std::abs(decay - m_decay) > 0.01f)
	{
		m_decay = decay;
		for (int i = 0; i < MaxVoices; ++i) { m_wgL[i].setDecay(decay); m_wgR[i].setDecay(decay); }
	}

	// ---- excite the bank
	const float drive = 0.05f + m_controls.m_drive.value() * 0.6f;
	const float bright = m_controls.m_bright.value();
	const float lpCoef = std::exp(-6.2831853f * (300.0f + bright * bright * 12000.0f) / sr);
	const float norm = 1.0f / std::sqrt(static_cast<float>(voices));
	const float d = dryLevel(), w = wetLevel() * m_controls.m_gain.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		m_lpL = buf[f].left() * drive * (1.0f - lpCoef) + m_lpL * lpCoef;
		m_lpR = buf[f].right() * drive * (1.0f - lpCoef) + m_lpR * lpCoef;
		float l = 0.0f, r = 0.0f;
		for (int i = 0; i < voices; ++i)
		{
			l += m_wgL[i].process(m_lpL);
			r += m_wgR[i].process(m_lpR);
		}
		// soft clip: long decays with loud input must not blow up the mix
		l = std::tanh(l * norm);
		r = std::tanh(r * norm);
		buf[f].left() = buf[f].left() * d + l * w;
		buf[f].right() = buf[f].right() * d + r * w;
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelResonatorEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
