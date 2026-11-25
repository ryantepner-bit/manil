/*
 * MandelCloud.h - granular cloud effect for MANDELBROT (pyo Granulator / cecilia5 granulator
 *                 modules, run on the live signal). The fractal engine can steer the grains and
 *                 keep their pitches inside the sounding chord.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_CLOUD_H
#define LMMS_MANDEL_CLOUD_H

#include <vector>

#include "CeciliaDsp.h"
#include "Effect.h"
#include "EffectControls.h"

namespace lmms
{

class MandelCloudEffect;

class MandelCloudControls : public EffectControls
{
	Q_OBJECT
public:
	MandelCloudControls(MandelCloudEffect* effect);
	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;
	QString nodeName() const override { return "MandelCloudControls"; }
	gui::EffectControlDialog* createView() override;
	int controlCount() override { return 10; }

	FloatModel m_grain, m_density, m_pitch, m_spray, m_scatter, m_width, m_fractal, m_gain;
	BoolModel m_freeze, m_inKey;
};

class MandelCloudEffect : public Effect
{
public:
	MandelCloudEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;
	EffectControls* controls() override { return &m_controls; }

private:
	MandelCloudControls m_controls;
	mandeldsp::LiveGranulator m_gran;
	float m_sr = 0.0f;
	std::vector<float> m_inL, m_inR, m_outL, m_outR, m_pitchSet;
};

} // namespace lmms

#endif
