/*
 * MandelPhaser.cpp - see MandelPhaser.h.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "MandelPhaser.h"

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
Plugin::Descriptor PLUGIN_EXPORT mandelphaser_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel Phaser",
	QT_TRANSLATE_NOOP("PluginBrowser", "Multi-stage allpass phaser (pyo Phaser) with sweep and fractal steering"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelPhaserControls::MandelPhaserControls(MandelPhaserEffect* effect) :
	EffectControls(effect),
	m_freq(600.0f, 40.0f, 8000.0f, 1.0f, this, tr("Centre frequency")),
	m_spread(1.3f, 0.5f, 2.5f, 0.01f, this, tr("Spread")),
	m_q(4.0f, 0.5f, 30.0f, 0.1f, this, tr("Q")),
	m_feedback(0.5f, -0.99f, 0.99f, 0.01f, this, tr("Feedback")),
	m_rate(0.15f, 0.0f, 8.0f, 0.01f, this, tr("Sweep rate")),
	m_depth(0.6f, 0.0f, 1.0f, 0.01f, this, tr("Sweep depth")),
	m_fractal(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Fractal amount")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output")),
	m_stages(8, 1, 24, this, tr("Stages"))
{
}

void MandelPhaserControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_freq.saveSettings(doc, parent, "freq");
	m_spread.saveSettings(doc, parent, "spread");
	m_q.saveSettings(doc, parent, "q");
	m_feedback.saveSettings(doc, parent, "feedback");
	m_rate.saveSettings(doc, parent, "rate");
	m_depth.saveSettings(doc, parent, "depth");
	m_fractal.saveSettings(doc, parent, "fractal");
	m_gain.saveSettings(doc, parent, "gain");
	m_stages.saveSettings(doc, parent, "stages");
}

void MandelPhaserControls::loadSettings(const QDomElement& parent)
{
	m_freq.loadSettings(parent, "freq");
	m_spread.loadSettings(parent, "spread");
	m_q.loadSettings(parent, "q");
	m_feedback.loadSettings(parent, "feedback");
	m_rate.loadSettings(parent, "rate");
	m_depth.loadSettings(parent, "depth");
	m_fractal.loadSettings(parent, "fractal");
	m_gain.loadSettings(parent, "gain");
	m_stages.loadSettings(parent, "stages");
}

gui::EffectControlDialog* MandelPhaserControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL PHASER"), tr("allpass phaser"));
	p->knob(tr("FREQ"), tr("Centre frequency:"), " Hz", &m_freq, 0, 0);
	p->knob(tr("SPREAD"), tr("Spread between stages:"), "", &m_spread, 0, 1);
	p->knob(tr("Q"), tr("Q:"), "", &m_q, 0, 2);
	p->knob(tr("FEEDBK"), tr("Feedback:"), "", &m_feedback, 0, 3);
	p->knob(tr("RATE"), tr("Sweep rate:"), " Hz", &m_rate, 1, 0);
	p->knob(tr("DEPTH"), tr("Sweep depth (octaves / 2):"), "", &m_depth, 1, 1);
	p->knob(tr("FRACT"), tr("How much the set's boundary distance moves the centre:"), "", &m_fractal, 1, 2);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 3);
	p->lcd(tr("STAGES"), &m_stages, 2, 2, 0);
	return p;
}

MandelPhaserEffect::MandelPhaserEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelphaser_plugin_descriptor, parent, key),
	m_controls(this)
{
}

Effect::ProcessStatus MandelPhaserEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		m_ph[0].prepare(sr); m_ph[1].prepare(sr);
	}
	const auto fr = mandeldsp::readFractal();
	const float d = dryLevel(), w = wetLevel();
	// sweep: a slow sine (right channel a quarter turn later); the set's boundary distance shifts the centre
	m_lfo += 6.283185307 * m_controls.m_rate.value() * frames / sr;
	if (m_lfo > 6.283185307) { m_lfo -= 6.283185307; }
	const float amt = fr.live ? m_controls.m_fractal.value() : 0.0f;
	const float centre = m_controls.m_freq.value() * std::pow(2.0f, amt * (fr.boundary - 0.5f) * 4.0f);
	const float depth = m_controls.m_depth.value();
	const int stages = m_controls.m_stages.value();
	for (int c = 0; c < 2; ++c)
	{
		const float sweep = std::pow(2.0f, depth * 2.0f * static_cast<float>(std::sin(m_lfo + c * 1.5707963)));
		m_ph[c].set(centre * sweep, m_controls.m_spread.value(), m_controls.m_q.value(), stages);
	}
	const float fb = m_controls.m_feedback.value(), g = m_controls.m_gain.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		for (int c = 0; c < 2; ++c)
		{
			const float x = buf[f][c];
			buf[f][c] = x * d + m_ph[c].process(x, fb) * w * g;
		}
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelPhaserEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
