/*
 * MandelPhaser.h - multi-stage allpass phaser (pyo Phaser, cecilia5 Filters/Phaser) with a sweep; the
 *                  fractal can move its centre frequency.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDELPHASER_H
#define LMMS_MANDELPHASER_H

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelPhaserEffect;

class MandelPhaserControls : public EffectControls
{
	Q_OBJECT
public:
	MandelPhaserControls(MandelPhaserEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelPhaserControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 9; }

	FloatModel m_freq, m_spread, m_q, m_feedback, m_rate, m_depth, m_fractal, m_gain;
	IntModel m_stages;
};

class MandelPhaserEffect : public Effect
{
public:
	MandelPhaserEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelPhaserControls m_controls;
	float m_sr = 0.0f;
	mandeldsp::Phaser m_ph[2];
	double m_lfo = 0.0;
};

} // namespace lmms

#endif
