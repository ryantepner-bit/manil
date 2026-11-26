/*
 * MandelModulator.h - a controller (like LMMS's LFO controller) that can be linked to any knob:
 *                     its output is a live reading of the Mandelbrot set, or a pyo Xnoise random
 *                     stream, shaped by range, curve, slew and sample & hold timing.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_MODULATOR_H
#define LMMS_MANDEL_MODULATOR_H

#include "ComboBoxModel.h"
#include "Controller.h"
#include "ControllerDialog.h"
#include "fractal/PyoGen.h"

namespace lmms
{

namespace gui
{
class MandelModulatorDialog;
}

class MandelModulator : public Controller
{
	Q_OBJECT
public:
	enum Source
	{
		// readings of the set (from the fractal engine, live)
		BarEscape = 0, BarChaos, BarBoundary, BarEntropy, PhraseRoughness, ProbeX, ProbeY,
		PhrasePosition, BarPosition, ChordDegree,
		// pyo Xnoise random streams
		RandomFirst,
		NumSources = RandomFirst + static_cast<int>(fractal::pyo::Dist::Count)
	};

	explicit MandelModulator(Model* parent);
	~MandelModulator() override = default;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "mandelmodulator"; }

	static const char* sourceName(int s);

public slots:
	gui::ControllerDialog* createDialog(QWidget* parent) override;

protected:
	void updateValueBuffer() override;

private:
	float target();

	ComboBoxModel m_sourceModel;
	ComboBoxModel m_rateModel;   //!< how often a new value is taken (sample & hold)
	FloatModel m_minModel;
	FloatModel m_maxModel;
	FloatModel m_curveModel;     //!< -1 log .. 0 linear .. 1 exp
	FloatModel m_slewModel;      //!< 0 = jumps, 1 = very slow glide
	FloatModel m_x1Model, m_x2Model; //!< pyo distribution parameters
	BoolModel m_invertModel;

	fractal::pyo::Xnoise m_noise;
	float m_held = 0.5f;
	float m_out = 0.5f;
	long m_lastSlot = -1;

	friend class gui::MandelModulatorDialog;
};

namespace gui
{

class MandelModulatorDialog : public ControllerDialog
{
	Q_OBJECT
public:
	MandelModulatorDialog(Controller* controller, QWidget* parent);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_MANDEL_MODULATOR_H
