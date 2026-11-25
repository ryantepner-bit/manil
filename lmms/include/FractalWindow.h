/*
 * FractalWindow.h - MANDELBROT's fractal window: the set with the drifting probe, the engine's
 *                   global controls, what the set is reading right now, and the phrase history.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_GUI_FRACTAL_WINDOW_H
#define LMMS_GUI_FRACTAL_WINDOW_H

#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;

namespace lmms::gui
{

class ComboBox;
class Knob;
class LedCheckBox;

//! The Mandelbrot view: renders the region around the probe, draws its trail; click to move it
class FractalSetView : public QWidget
{
	Q_OBJECT
public:
	explicit FractalSetView(QWidget* parent);
	QSize sizeHint() const override { return {420, 420}; }
	void setViewScale(double s) { m_viewScale = s; m_dirty = true; }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* e) override;
	void wheelEvent(QWheelEvent* e) override;
	void resizeEvent(QResizeEvent*) override { m_dirty = true; }

private slots:
	void tick();

private:
	void render();
	QPointF toScreen(double x, double y) const;
	QImage m_image;
	double m_centreX = -0.7436, m_centreY = 0.1318, m_viewScale = 0.25;
	bool m_dirty = true;
	QTimer* m_timer;
};

class FractalWindow : public QWidget
{
	Q_OBJECT
public:
	FractalWindow();

private slots:
	void refresh();
	void refreshHistory();
	void sendToTrack();
	void toggleFavourite();
	void clearHistory();
	void newDirection();

private:
	FractalSetView* m_set;
	ComboBox* m_style;
	ComboBox* m_root;
	ComboBox* m_scale;
	ComboBox* m_noveltyMode;
	ComboBox* m_chordSpeed;
	ComboBox* m_phraseBars;
	LedCheckBox* m_enabled;
	LedCheckBox* m_fractalKey;
	Knob* m_drift;
	Knob* m_zoom;
	Knob* m_complexity;
	Knob* m_control;
	Knob* m_swing;
	Knob* m_novelty;
	QLabel* m_reading;
	QLabel* m_historyCount;
	QTreeWidget* m_history;
	QComboBox* m_targetTrack;
	QTimer* m_timer;
	uint64_t m_lastPhrase = 0;
};

} // namespace lmms::gui

#endif // LMMS_GUI_FRACTAL_WINDOW_H
