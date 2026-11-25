/*
 * MandelCloud.cpp - granular cloud effect for MANDELBROT. See MandelCloud.h.
 */

#include "MandelCloud.h"

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
Plugin::Descriptor PLUGIN_EXPORT mandelcloud_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel Cloud",
	QT_TRANSLATE_NOOP("PluginBrowser", "Granular cloud (pyo / Cecilia5 granulator) steered by the fractal"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelCloudControls::MandelCloudControls(MandelCloudEffect* effect) :
	EffectControls(effect),
	m_grain(140.0f, 10.0f, 1000.0f, 1.0f, this, tr("Grain size")),
	m_density(12.0f, 1.0f, 32.0f, 1.0f, this, tr("Density")),
	m_pitch(0.0f, -24.0f, 24.0f, 1.0f, this, tr("Pitch")),
	m_spray(7.0f, 0.0f, 24.0f, 0.1f, this, tr("Pitch spray")),
	m_scatter(0.8f, 0.0f, 4.0f, 0.01f, this, tr("Scatter")),
	m_width(0.8f, 0.0f, 1.0f, 0.01f, this, tr("Width")),
	m_fractal(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Fractal amount")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output")),
	m_freeze(false, this, tr("Freeze")),
	m_inKey(true, this, tr("Grains in chord"))
{
	m_grain.setScaleLogarithmic(true);
}

void MandelCloudControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_grain.saveSettings(doc, parent, "grain");
	m_density.saveSettings(doc, parent, "density");
	m_pitch.saveSettings(doc, parent, "pitch");
	m_spray.saveSettings(doc, parent, "spray");
	m_scatter.saveSettings(doc, parent, "scatter");
	m_width.saveSettings(doc, parent, "width");
	m_fractal.saveSettings(doc, parent, "fractal");
	m_gain.saveSettings(doc, parent, "gain");
	m_freeze.saveSettings(doc, parent, "freeze");
	m_inKey.saveSettings(doc, parent, "inkey");
}

void MandelCloudControls::loadSettings(const QDomElement& parent)
{
	m_grain.loadSettings(parent, "grain");
	m_density.loadSettings(parent, "density");
	m_pitch.loadSettings(parent, "pitch");
	m_spray.loadSettings(parent, "spray");
	m_scatter.loadSettings(parent, "scatter");
	m_width.loadSettings(parent, "width");
	m_fractal.loadSettings(parent, "fractal");
	m_gain.loadSettings(parent, "gain");
	m_freeze.loadSettings(parent, "freeze");
	m_inKey.loadSettings(parent, "inkey");
}

gui::EffectControlDialog* MandelCloudControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL CLOUD"), tr("granular"));
	p->knob(tr("GRAIN"), tr("Grain size:"), " ms", &m_grain, 0, 0);
	p->knob(tr("DENS"), tr("Overlapping grains:"), "", &m_density, 0, 1);
	p->knob(tr("PITCH"), tr("Pitch:"), " st", &m_pitch, 0, 2);
	p->knob(tr("SPRAY"), tr("Pitch spray:"), " st", &m_spray, 0, 3);
	p->knob(tr("SCAT"), tr("Scatter back in time:"), " s", &m_scatter, 0, 4);
	p->knob(tr("WIDTH"), tr("Stereo width:"), "", &m_width, 1, 0);
	p->knob(tr("FRACT"), tr("How much the fractal steers the cloud:"), "", &m_fractal, 1, 1);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 2);
	p->led(tr("Freeze"), &m_freeze, 2, 0);
	p->led(tr("In chord"), &m_inKey, 2, 2);
	return p;
}

MandelCloudEffect::MandelCloudEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelcloud_plugin_descriptor, parent, key),
	m_controls(this)
{
	m_pitchSet.reserve(64);
}

Effect::ProcessStatus MandelCloudEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		m_gran.prepare(sr, 6.0f);
	}
	if (m_inL.size() < frames)
	{
		m_inL.resize(frames); m_inR.resize(frames); m_outL.resize(frames); m_outR.resize(frames);
	}
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		m_inL[f] = buf[f].left();
		m_inR[f] = buf[f].right();
	}

	const auto fr = mandeldsp::readFractal();
	const float amt = fr.live ? m_controls.m_fractal.value() : 0.0f;

	mandeldsp::LiveGranulator::Params p;
	// the set steers the cloud: near the boundary it thickens, chaos scatters it, entropy stretches grains
	p.grainMs = mandeldsp::lerpMod(m_controls.m_grain.value(), amt, fr.entropy, 10.0f, 1000.0f);
	p.grains = static_cast<int>(std::lround(mandeldsp::lerpMod(m_controls.m_density.value(), amt, fr.boundary, 1.0f, 32.0f)));
	p.pitch = m_controls.m_pitch.value();
	p.pitchSpray = m_controls.m_spray.value();
	p.posSpray = mandeldsp::lerpMod(m_controls.m_scatter.value(), amt, fr.chaos, 0.0f, 4.0f);
	p.stereo = m_controls.m_width.value();
	p.freeze = m_controls.m_freeze.value();
	if (m_controls.m_inKey.value())
	{
		mandeldsp::chordIntervals(fr, p.pitchSpray, m_pitchSet);
		p.pitchSet = &m_pitchSet;
	}
	m_gran.process(m_inL.data(), m_inR.data(), m_outL.data(), m_outR.data(), static_cast<int>(frames), p);

	const float d = dryLevel(), w = wetLevel() * m_controls.m_gain.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buf[f].left() = buf[f].left() * d + m_outL[f] * w;
		buf[f].right() = buf[f].right() * d + m_outR[f] * w;
	}
	// a frozen cloud keeps sounding with no input
	return p.freeze ? ProcessStatus::Continue : ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelCloudEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
