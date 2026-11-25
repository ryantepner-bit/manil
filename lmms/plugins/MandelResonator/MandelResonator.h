/*
 * MandelResonator.h - a bank of tuned waveguide resonators (pyo Waveguide, cecilia5
 *                     Resonators/WGuideBank) that follows the chord the fractal engine is playing,
 *                     so any sound fed in rings in key.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_RESONATOR_H
#define LMMS_MANDEL_RESONATOR_H

#include <array>

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelResonatorEffect;

class MandelResonatorControls : public EffectControls
{
	Q_OBJECT
public:
	MandelResonatorControls(MandelResonatorEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelResonatorControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 9; }

	FloatModel m_decay, m_voices, m_octave, m_root, m_bright, m_drive, m_fractal, m_gain;
	BoolModel m_follow;
};

class MandelResonatorEffect : public Effect
{
public:
	static constexpr int MaxVoices = 6;
	MandelResonatorEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelResonatorControls m_controls;
	std::array<mandeldsp::Waveguide, MaxVoices> m_wgL, m_wgR;
	std::array<float, MaxVoices> m_freq {};
	float m_decay = -1.0f;
	float m_sr = 0.0f;
	float m_lpL = 0.0f, m_lpR = 0.0f;
};

} // namespace lmms

#endif
