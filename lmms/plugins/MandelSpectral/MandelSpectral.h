/*
 * MandelSpectral.h - phase-vocoder effect for MANDELBROT: spectral reverb (pyo PVVerb), freeze,
 *                    blur and bin shift (pyo PVAnal / PVSynth / PVShift, cecilia5 Spectral modules).
 *                    The fractal engine can thicken the smear as the probe gets chaotic.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_SPECTRAL_H
#define LMMS_MANDEL_SPECTRAL_H

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelSpectralEffect;

class MandelSpectralControls : public EffectControls
{
	Q_OBJECT
public:
	MandelSpectralControls(MandelSpectralEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelSpectralControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 7; }

	FloatModel m_decay, m_damp, m_blur, m_shift, m_fractal, m_gain;
	BoolModel m_freeze;
};

class MandelSpectralEffect : public Effect
{
public:
	MandelSpectralEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelSpectralControls m_controls;
	mandeldsp::PhaseVocoder m_pv[2];
	float m_sr = 0.0f;
};

} // namespace lmms

#endif
