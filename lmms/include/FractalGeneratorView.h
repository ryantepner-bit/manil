/*
 * FractalGeneratorView.h - the FRACTAL tab of the instrument window.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_GUI_FRACTAL_GENERATOR_VIEW_H
#define LMMS_GUI_FRACTAL_GENERATOR_VIEW_H

#include <QWidget>

#include "ModelView.h"

class QLabel;
class QPushButton;
class QTimer;

namespace lmms
{
class FractalGenerator;

namespace gui
{
class ComboBox;
class Knob;
class LcdSpinBox;

class FractalGeneratorView : public QWidget, public ModelView
{
	Q_OBJECT
public:
	FractalGeneratorView(FractalGenerator* gen, QWidget* parent);
	~FractalGeneratorView() override = default;

private slots:
	void refreshStatus();
	void capture();

private:
	void modelChanged() override;

	FractalGenerator* m_gen;
	ComboBox* m_roleBox;
	Knob* m_density;
	LcdSpinBox* m_octave;
	Knob* m_variation;
	Knob* m_velocity;
	Knob* m_gate;
	QLabel* m_status;
	QLabel* m_engine;
	QPushButton* m_captureButton;
	QPushButton* m_windowButton;
	QTimer* m_timer;
};

} // namespace gui
} // namespace lmms

#endif // LMMS_GUI_FRACTAL_GENERATOR_VIEW_H
