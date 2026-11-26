/*
 * MandelHarmonizer.cpp - see MandelHarmonizer.h.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "MandelHarmonizer.h"

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
Plugin::Descriptor PLUGIN_EXPORT mandelharmonizer_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel Harmonizer",
	QT_TRANSLATE_NOOP("PluginBrowser", "Two-voice pitch shifter (pyo Harmonizer) that can follow the fractal chords"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelHarmonizerControls::MandelHarmonizerControls(MandelHarmonizerEffect* effect) :
	EffectControls(effect),
	m_voiceA(7.0f, -24.0f, 24.0f, 1.0f, this, tr("Voice A (semitones)")),
	m_voiceB(12.0f, -24.0f, 24.0f, 1.0f, this, tr("Voice B (semitones)")),
	m_mixB(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Voice B level")),
	m_feedback(0.0f, 0.0f, 0.95f, 0.01f, this, tr("Feedback")),
	m_window(0.1f, 0.02f, 0.5f, 0.001f, this, tr("Window")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output")),
	m_follow(false, this, tr("Voices follow the chord"))
{
}

void MandelHarmonizerControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_voiceA.saveSettings(doc, parent, "voicea");
	m_voiceB.saveSettings(doc, parent, "voiceb");
	m_mixB.saveSettings(doc, parent, "mixb");
	m_feedback.saveSettings(doc, parent, "feedback");
	m_window.saveSettings(doc, parent, "window");
	m_gain.saveSettings(doc, parent, "gain");
	m_follow.saveSettings(doc, parent, "follow");
}

void MandelHarmonizerControls::loadSettings(const QDomElement& parent)
{
	m_voiceA.loadSettings(parent, "voicea");
	m_voiceB.loadSettings(parent, "voiceb");
	m_mixB.loadSettings(parent, "mixb");
	m_feedback.loadSettings(parent, "feedback");
	m_window.loadSettings(parent, "window");
	m_gain.loadSettings(parent, "gain");
	m_follow.loadSettings(parent, "follow");
}

gui::EffectControlDialog* MandelHarmonizerControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL HARMONIZER"), tr("pitch shift"));
	p->knob(tr("VOICE A"), tr("Voice A:"), " st", &m_voiceA, 0, 0);
	p->knob(tr("VOICE B"), tr("Voice B:"), " st", &m_voiceB, 0, 1);
	p->knob(tr("B LEVEL"), tr("Voice B level:"), "", &m_mixB, 0, 2);
	p->knob(tr("FEEDBK"), tr("Feedback:"), "", &m_feedback, 0, 3);
	p->knob(tr("WINDOW"), tr("Window:"), " s", &m_window, 1, 0);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 1);
	p->led(tr("Follow chords"), &m_follow, 1, 2, 2);
	return p;
}

MandelHarmonizerEffect::MandelHarmonizerEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelharmonizer_plugin_descriptor, parent, key),
	m_controls(this)
{
}

Effect::ProcessStatus MandelHarmonizerEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		for (int c = 0; c < 2; ++c) { m_hA[c].prepare(sr); m_hB[c].prepare(sr); }
	}
	const auto fr = mandeldsp::readFractal();
	const float d = dryLevel(), w = wetLevel();
	float a = m_controls.m_voiceA.value(), b = m_controls.m_voiceB.value();
	if (m_controls.m_follow.value() && fr.live)
	{
		// the voices become the sounding chord's 3rd and 5th above its root (or the nearest chord tones)
		std::vector<float> iv;
		iv.reserve(16);
		mandeldsp::chordIntervals(fr, 12.0f, iv);
		std::vector<float> up;
		for (float x : iv) { if (x > 0.0f && x <= 12.0f) { up.push_back(x); } }
		std::sort(up.begin(), up.end());
		if (!up.empty()) { a = up[0]; b = up.size() > 1 ? up[1] : up[0] + 12.0f; }
	}
	const float fb = m_controls.m_feedback.value(), win = m_controls.m_window.value();
	const float mb = m_controls.m_mixB.value(), g = m_controls.m_gain.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 2; ++c)
		{
			const float x = buf[f][c];
			const float y = m_hA[c].process(x, a, fb, win) + mb * m_hB[c].process(x, b, fb, win);
			buf[f][c] = x * d + y * w * g * 0.7f;
		}
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelHarmonizerEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
