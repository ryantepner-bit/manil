/*
 * MandelFreqShift.h - frequency shifter (pyo Hilbert + FreqShift, cecilia5 Filters/FreqShift): moves every
 *                     partial by the same number of Hz. The fractal can move the shift.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDELFREQSHIFT_H
#define LMMS_MANDELFREQSHIFT_H

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelFreqShiftEffect;

class MandelFreqShiftControls : public EffectControls
{
	Q_OBJECT
public:
	MandelFreqShiftControls(MandelFreqShiftEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelFreqShiftControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 5; }

	FloatModel m_shift, m_spread, m_fractal, m_fractalRange, m_gain;
};

class MandelFreqShiftEffect : public Effect
{
public:
	MandelFreqShiftEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelFreqShiftControls m_controls;
	float m_sr = 0.0f;
	mandeldsp::FreqShifter m_fs[2];
	float m_smooth = 0.0f;
};

} // namespace lmms

#endif
