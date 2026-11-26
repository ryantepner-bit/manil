/*
 * MandelFreqShift.cpp - see MandelFreqShift.h.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "MandelFreqShift.h"

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
Plugin::Descriptor PLUGIN_EXPORT mandelfreqshift_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel FreqShift",
	QT_TRANSLATE_NOOP("PluginBrowser", "Frequency shifter (pyo Hilbert + FreqShift): inharmonic, metallic shifts, steered by the fractal"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelFreqShiftControls::MandelFreqShiftControls(MandelFreqShiftEffect* effect) :
	EffectControls(effect),
	m_shift(50.0f, -2000.0f, 2000.0f, 0.1f, this, tr("Shift")),
	m_spread(0.0f, -1.0f, 1.0f, 0.01f, this, tr("Stereo spread")),
	m_fractal(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Fractal amount")),
	m_fractalRange(200.0f, 0.0f, 2000.0f, 1.0f, this, tr("Fractal range")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output"))
{
}

void MandelFreqShiftControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_shift.saveSettings(doc, parent, "shift");
	m_spread.saveSettings(doc, parent, "spread");
	m_fractal.saveSettings(doc, parent, "fractal");
	m_fractalRange.saveSettings(doc, parent, "fractalrange");
	m_gain.saveSettings(doc, parent, "gain");
}

void MandelFreqShiftControls::loadSettings(const QDomElement& parent)
{
	m_shift.loadSettings(parent, "shift");
	m_spread.loadSettings(parent, "spread");
	m_fractal.loadSettings(parent, "fractal");
	m_fractalRange.loadSettings(parent, "fractalrange");
	m_gain.loadSettings(parent, "gain");
}

gui::EffectControlDialog* MandelFreqShiftControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL FREQSHIFT"), tr("frequency shifter"));
	p->knob(tr("SHIFT"), tr("Shift:"), " Hz", &m_shift, 0, 0);
	p->knob(tr("SPREAD"), tr("Stereo spread (-1 right channel shifts the other way, +1 three times as far):"), "", &m_spread, 0, 1);
	p->knob(tr("FRACT"), tr("How much the set's chaos moves the shift:"), "", &m_fractal, 0, 2);
	p->knob(tr("RANGE"), tr("Fractal range:"), " Hz", &m_fractalRange, 0, 3);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 0);
	return p;
}

MandelFreqShiftEffect::MandelFreqShiftEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelfreqshift_plugin_descriptor, parent, key),
	m_controls(this)
{
}

Effect::ProcessStatus MandelFreqShiftEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		m_fs[0].prepare(sr); m_fs[1].prepare(sr);
	}
	const auto fr = mandeldsp::readFractal();
	const float d = dryLevel(), w = wetLevel();
	// the set's chaos moves the shift (smoothed per block so it glides)
	const float amt = fr.live ? m_controls.m_fractal.value() : 0.0f;
	const float target = m_controls.m_shift.value() + amt * (fr.chaos - 0.5f) * 2.0f * m_controls.m_fractalRange.value();
	m_smooth += (target - m_smooth) * 0.2f;
	const float spread = m_controls.m_spread.value(), g = m_controls.m_gain.value();
	// right channel: spread -1 shifts the opposite way, 0 the same, +1 three times as far
	const float shL = m_smooth, shR = m_smooth * (1.0f + 2.0f * spread);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float l = m_fs[0].process(buf[f][0], shL);
		const float r = m_fs[1].process(buf[f][1], shR);
		buf[f][0] = buf[f][0] * d + l * w * g;
		buf[f][1] = buf[f][1] * d + r * w * g;
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelFreqShiftEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
