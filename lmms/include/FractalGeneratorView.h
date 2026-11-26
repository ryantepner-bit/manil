/*
 * FractalGeneratorView.h - the FRACTAL tab of the instrument window: role, note source
 *                          (fractal / Euclidean / weighted beat / random), chance grid, range.
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
class QStackedWidget;

namespace lmms
{
class FractalGenerator;

namespace gui
{
class ComboBox;
class Knob;
class LcdSpinBox;

//! 16 bars, one per 16th of the bar: the % chance that a note on that step plays. Drag to draw.
class ChanceGrid : public QWidget
{
public:
	explicit ChanceGrid(QWidget* parent);
	void setGenerator(FractalGenerator* gen);
	QSize sizeHint() const override { return {220, 64}; }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;

private:
	void setFromMouse(const QPoint& p, bool reset);
	FractalGenerator* m_gen = nullptr;
};

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
	ComboBox* m_sourceBox;
	QStackedWidget* m_sourcePages;
	Knob* m_density;
	LcdSpinBox* m_octave;
	Knob* m_variation;
	Knob* m_velocity;
	Knob* m_gate;
	Knob* m_steer;
	LcdSpinBox* m_euHits;
	LcdSpinBox* m_euSteps;
	LcdSpinBox* m_euRotate;
	LcdSpinBox* m_beatTaps;
	Knob* m_w1;
	Knob* m_w2;
	Knob* m_w3;
	ComboBox* m_dist;
	ComboBox* m_rate;
	Knob* m_x1;
	Knob* m_x2;
	LcdSpinBox* m_range;
	ChanceGrid* m_chanceGrid;
	Knob* m_trackChance;
	Knob* m_humanize;
	LcdSpinBox* m_lowKey;
	LcdSpinBox* m_highKey;
	QLabel* m_status;
	QLabel* m_engine;
	QPushButton* m_captureButton;
	QPushButton* m_windowButton;
	QTimer* m_timer;
};

} // namespace gui
} // namespace lmms

#endif // LMMS_GUI_FRACTAL_GENERATOR_VIEW_H
