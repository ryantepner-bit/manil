/*
 * MandelShared.h - shared by MANDELBROT's effects: the link to the fractal engine (what the set is
 *                  reading right now, the sounding chord and key) and a simple knob panel.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_MANDEL_SHARED_H
#define LMMS_MANDEL_SHARED_H

#include <algorithm>
#include <vector>

#include <QGridLayout>
#include <QLabel>

#include "AutomatableModel.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "FractalEngine.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"

namespace lmms::mandeldsp
{

//! What the fractal engine is doing right now. live = false when the song is stopped (then the
//! effects use their own settings only).
struct FractalReading
{
	bool live = false;
	float escape = 0.5f, chaos = 0.5f, boundary = 0.5f, entropy = 0.5f;
	int chordMask = 0x091;    //!< C E G
	int scaleMask = 0xab5;    //!< C major
	int bass = 48;            //!< MIDI note of the chord's root, low octave
};

inline FractalReading readFractal()
{
	FractalReading r;
	FractalEngine* e = FractalEngine::instance();
	if (e == nullptr) { return r; }
	auto& c = e->conductor();
	r.live = c.running();
	r.escape = c.barEscape.load(std::memory_order_relaxed);
	r.chaos = c.barChaos.load(std::memory_order_relaxed);
	r.boundary = c.barBoundary.load(std::memory_order_relaxed);
	r.entropy = c.barEntropy.load(std::memory_order_relaxed);
	const int cm = c.currentChordMask.load(std::memory_order_relaxed);
	if (cm != 0) { r.chordMask = cm; }
	const int sm = c.currentScaleMask.load(std::memory_order_relaxed);
	if (sm != 0) { r.scaleMask = sm; }
	r.bass = c.currentChordBass.load(std::memory_order_relaxed);
	return r;
}

//! Semitone offsets from the chord root that are chord tones, within +-range (always includes 0).
//! Fills `out` without allocating once it has capacity.
inline void chordIntervals(const FractalReading& r, float range, std::vector<float>& out)
{
	out.clear();
	out.push_back(0.0f);
	const int rootPc = ((r.bass % 12) + 12) % 12;
	for (int oct = -2; oct <= 2; ++oct)
	{
		for (int pc = 0; pc < 12; ++pc)
		{
			if (!((r.chordMask >> pc) & 1)) { continue; }
			const int iv = ((pc - rootPc) % 12 + 12) % 12 + oct * 12;
			if (iv != 0 && std::abs(iv) <= range + 0.01f) { out.push_back(static_cast<float>(iv)); }
		}
	}
}

inline float lerpMod(float base, float amount, float reading, float lo, float hi)
{
	//! reading 0..1 pushes base by up to +-amount*(range/2) around its value
	return std::clamp(base + amount * (reading - 0.5f) * (hi - lo), lo, hi);
}

} // namespace lmms::mandeldsp

namespace lmms::gui
{

//! A dark panel of knobs and LEDs with a title line
class MandelPanel : public EffectControlDialog
{
public:
	MandelPanel(EffectControls* controls, const QString& title, const QString& subtitle) :
		EffectControlDialog(controls)
	{
		setAutoFillBackground(true);
		QPalette pal = palette();
		pal.setColor(backgroundRole(), QColor(14, 16, 24));
		setPalette(pal);
		m_grid = new QGridLayout(this);
		m_grid->setContentsMargins(10, 8, 10, 10);
		m_grid->setHorizontalSpacing(8);
		m_grid->setVerticalSpacing(6);
		auto t = new QLabel(QString("<b style='color:#28dcff'>%1</b> <span style='color:#8a90a8'>%2</span>")
			.arg(title, subtitle), this);
		m_grid->addWidget(t, 0, 0, 1, 6);
		m_grid->setSizeConstraint(QLayout::SetFixedSize);
	}

	void knob(const QString& label, const QString& hint, const QString& unit, FloatModel* model, int row, int col)
	{
		auto k = new Knob(KnobType::Bright26, label, this);
		k->setModel(model);
		k->setHintText(hint, unit);
		m_grid->addWidget(k, row + 1, col, Qt::AlignHCenter);
	}

	void lcd(const QString& label, IntModel* model, int digits, int row, int col)
	{
		auto l = new LcdSpinBox(digits, this, label);
		l->setLabel(label);
		l->setModel(model);
		m_grid->addWidget(l, row + 1, col, Qt::AlignHCenter);
	}

	void led(const QString& label, BoolModel* model, int row, int col, int span = 2)
	{
		auto l = new LedCheckBox(label, this);
		l->setModel(model);
		m_grid->addWidget(l, row + 1, col, 1, span, Qt::AlignLeft);
	}

private:
	QGridLayout* m_grid;
};

} // namespace lmms::gui

#endif // LMMS_MANDEL_SHARED_H
