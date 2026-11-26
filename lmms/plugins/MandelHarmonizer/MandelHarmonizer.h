/*
 * MandelHarmonizer.h - two-voice pitch shifter (pyo Harmonizer, cecilia5 Pitch/Harmonizer). The voices can follow
 *                      the chord the fractal engine is playing.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDELHARMONIZER_H
#define LMMS_MANDELHARMONIZER_H

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelHarmonizerEffect;

class MandelHarmonizerControls : public EffectControls
{
	Q_OBJECT
public:
	MandelHarmonizerControls(MandelHarmonizerEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelHarmonizerControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 7; }

	FloatModel m_voiceA, m_voiceB, m_mixB, m_feedback, m_window, m_gain;
	BoolModel m_follow;
};

class MandelHarmonizerEffect : public Effect
{
public:
	MandelHarmonizerEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelHarmonizerControls m_controls;
	float m_sr = 0.0f;
	mandeldsp::Harmonizer m_hA[2], m_hB[2];
};

} // namespace lmms

#endif
