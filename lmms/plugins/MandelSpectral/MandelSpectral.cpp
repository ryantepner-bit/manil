/*
 * MandelSpectral.cpp - phase-vocoder effect for MANDELBROT. See MandelSpectral.h.
 */

#include "MandelSpectral.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "MandelShared.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{
Plugin::Descriptor PLUGIN_EXPORT mandelspectral_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Mandel Spectral",
	QT_TRANSLATE_NOOP("PluginBrowser", "Spectral reverb, freeze, blur and shift (pyo phase vocoder)"),
	"MANDELBROT, after Olivier Belanger's pyo",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("lmms-plugin-logo"),
	nullptr,
	nullptr,
};
}

MandelSpectralControls::MandelSpectralControls(MandelSpectralEffect* effect) :
	EffectControls(effect),
	m_decay(0.7f, 0.0f, 1.0f, 0.01f, this, tr("Spectral decay")),
	m_damp(0.5f, 0.0f, 1.0f, 0.01f, this, tr("High damping")),
	m_blur(0.3f, 0.0f, 0.98f, 0.01f, this, tr("Blur")),
	m_shift(0.0f, -1000.0f, 1000.0f, 1.0f, this, tr("Shift")),
	m_fractal(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Fractal amount")),
	m_gain(1.0f, 0.0f, 2.0f, 0.01f, this, tr("Output")),
	m_freeze(false, this, tr("Freeze"))
{
}

void MandelSpectralControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	m_decay.saveSettings(doc, parent, "decay");
	m_damp.saveSettings(doc, parent, "damp");
	m_blur.saveSettings(doc, parent, "blur");
	m_shift.saveSettings(doc, parent, "shift");
	m_fractal.saveSettings(doc, parent, "fractal");
	m_gain.saveSettings(doc, parent, "gain");
	m_freeze.saveSettings(doc, parent, "freeze");
}

void MandelSpectralControls::loadSettings(const QDomElement& parent)
{
	m_decay.loadSettings(parent, "decay");
	m_damp.loadSettings(parent, "damp");
	m_blur.loadSettings(parent, "blur");
	m_shift.loadSettings(parent, "shift");
	m_fractal.loadSettings(parent, "fractal");
	m_gain.loadSettings(parent, "gain");
	m_freeze.loadSettings(parent, "freeze");
}

gui::EffectControlDialog* MandelSpectralControls::createView()
{
	auto p = new gui::MandelPanel(this, tr("MANDEL SPECTRAL"), tr("phase vocoder"));
	p->knob(tr("DECAY"), tr("Spectral decay:"), "", &m_decay, 0, 0);
	p->knob(tr("DAMP"), tr("High damping:"), "", &m_damp, 0, 1);
	p->knob(tr("BLUR"), tr("Blur:"), "", &m_blur, 0, 2);
	p->knob(tr("SHIFT"), tr("Shift every partial by:"), " Hz", &m_shift, 0, 3);
	p->knob(tr("FRACT"), tr("How much the fractal steers decay and blur:"), "", &m_fractal, 1, 0);
	p->knob(tr("OUT"), tr("Output:"), "", &m_gain, 1, 1);
	p->led(tr("Freeze"), &m_freeze, 1, 2);
	return p;
}

MandelSpectralEffect::MandelSpectralEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&mandelspectral_plugin_descriptor, parent, key),
	m_controls(this)
{
}

Effect::ProcessStatus MandelSpectralEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
{
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	if (sr != m_sr)
	{
		m_sr = sr;
		const int size = sr > 60000.0f ? 4096 : 2048;
		m_pv[0].prepare(sr, size, 4);
		m_pv[1].prepare(sr, size, 4);
	}
	const auto fr = mandeldsp::readFractal();
	const float amt = fr.live ? m_controls.m_fractal.value() : 0.0f;
	mandeldsp::PhaseVocoder::Params p;
	// chaotic orbits smear the spectrum, the boundary lengthens the tail
	p.decay = mandeldsp::lerpMod(m_controls.m_decay.value(), amt * 0.5f, fr.boundary, 0.0f, 1.0f);
	p.damp = m_controls.m_damp.value();
	p.blur = mandeldsp::lerpMod(m_controls.m_blur.value(), amt * 0.6f, fr.chaos, 0.0f, 0.98f);
	p.shiftHz = m_controls.m_shift.value();
	p.freeze = m_controls.m_freeze.value();

	const float d = dryLevel(), w = wetLevel() * m_controls.m_gain.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float l = m_pv[0].process(buf[f].left(), p);
		const float r = m_pv[1].process(buf[f].right(), p);
		buf[f].left() = buf[f].left() * d + l * w;
		buf[f].right() = buf[f].right() * d + r * w;
	}
	return p.freeze ? ProcessStatus::Continue : ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new MandelSpectralEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
