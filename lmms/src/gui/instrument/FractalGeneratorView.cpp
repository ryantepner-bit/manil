/*
 * FractalGeneratorView.cpp - the FRACTAL tab of the instrument window.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalGeneratorView.h"

#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ComboBox.h"
#include "FontHelper.h"
#include "FractalEngine.h"
#include "FractalGenerator.h"
#include "GuiApplication.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "MainWindow.h"

namespace lmms::gui
{

// ============================================================================ ChanceGrid
ChanceGrid::ChanceGrid(QWidget* parent) : QWidget(parent)
{
	setMinimumSize(200, 56);
	setCursor(Qt::PointingHandCursor);
	setToolTip(tr("Chance per 16th of the bar that a note there plays.\nDrag to draw, right-click a step to set it back to 100%."));
}

void ChanceGrid::setGenerator(FractalGenerator* gen)
{
	if (m_gen != nullptr)
	{
		for (int i = 0; i < FractalGenerator::ChanceSteps; ++i) { disconnect(m_gen->chanceModel(i), nullptr, this, nullptr); }
	}
	m_gen = gen;
	if (m_gen != nullptr)
	{
		for (int i = 0; i < FractalGenerator::ChanceSteps; ++i)
		{
			connect(m_gen->chanceModel(i), &Model::dataChanged, this, [this] { update(); });
		}
	}
	update();
}

void ChanceGrid::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(13, 15, 21));
	if (m_gen == nullptr) { return; }
	const int n = FractalGenerator::ChanceSteps;
	const double w = static_cast<double>(width()) / n;
	const int h = height() - 12;
	for (int i = 0; i < n; ++i)
	{
		const float v = m_gen->chanceModel(i)->value() * 0.01f;
		const QRectF bar(i * w + 1, 2 + h * (1.0 - v), w - 2, h * v);
		const bool beat = i % 4 == 0;
		p.fillRect(QRectF(i * w + 1, 2, w - 2, h), beat ? QColor(30, 34, 46) : QColor(22, 25, 34));
		p.fillRect(bar, v >= 0.999f ? QColor(40, 220, 255) : QColor(169, 139, 255));
		if (v < 0.999f && w > 11)
		{
			p.setPen(QColor(220, 225, 240));
			QFont f = p.font();
			f.setPixelSize(8);
			p.setFont(f);
			p.drawText(QRectF(i * w, 2, w, 10), Qt::AlignCenter, QString::number(static_cast<int>(v * 100.0f + 0.5f)));
		}
	}
	p.setPen(QColor(120, 130, 150));
	QFont f = p.font();
	f.setPixelSize(8);
	p.setFont(f);
	for (int b = 0; b < 4; ++b) { p.drawText(QRectF(b * 4 * w, height() - 10, w * 4, 10), Qt::AlignLeft, QString::number(b + 1)); }
}

void ChanceGrid::setFromMouse(const QPoint& pos, bool reset)
{
	if (m_gen == nullptr) { return; }
	const int n = FractalGenerator::ChanceSteps;
	const int i = std::clamp(static_cast<int>(pos.x() * n / std::max(1, width())), 0, n - 1);
	const int h = std::max(1, height() - 12);
	const float v = reset ? 100.0f : std::clamp(100.0f * (1.0f - static_cast<float>(pos.y() - 2) / h), 0.0f, 100.0f);
	m_gen->chanceModel(i)->setValue(std::round(v / 5.0f) * 5.0f);
}

void ChanceGrid::mousePressEvent(QMouseEvent* e)
{
	setFromMouse(e->pos(), e->button() == Qt::RightButton);
}

void ChanceGrid::mouseMoveEvent(QMouseEvent* e)
{
	if (e->buttons() & Qt::LeftButton) { setFromMouse(e->pos(), false); }
}

// ============================================================================ FractalGeneratorView
namespace
{
Knob* knob(QWidget* parent, const QString& label, const QString& hint, const QString& unit = "")
{
	auto k = new Knob(KnobType::Bright26, label, parent, Knob::LabelRendering::LegacyFixedFontSize);
	k->setHintText(hint, unit);
	return k;
}

LcdSpinBox* lcd(QWidget* parent, int digits, const QString& label, const QString& tip)
{
	auto l = new LcdSpinBox(digits, parent, label);
	l->setLabel(label);
	l->setToolTip(tip);
	return l;
}

QLabel* smallLabel(const QString& t, QWidget* parent)
{
	auto l = new QLabel(t, parent);
	l->setFont(adjustedToPixelSize(l->font(), DEFAULT_FONT_SIZE));
	l->setWordWrap(true);
	return l;
}

QLabel* heading(const QString& t, QWidget* parent)
{
	auto l = new QLabel(QString("<b style='color:#28dcff'>%1</b>").arg(t), parent);
	l->setFont(adjustedToPixelSize(l->font(), DEFAULT_FONT_SIZE + 1));
	return l;
}
}

FractalGeneratorView::FractalGeneratorView(FractalGenerator* gen, QWidget* parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_gen(gen)
{
	auto outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	outer->addWidget(scroll);
	auto page = new QWidget(scroll);
	scroll->setWidget(page);
	auto layout = new QVBoxLayout(page);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	// ---- role
	layout->addWidget(heading(tr("FRACTAL ENGINE"), page));
	layout->addWidget(smallLabel(tr("Role (what this track plays). Off: the track only plays its clips."), page));
	m_roleBox = new ComboBox(page);
	m_roleBox->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	layout->addWidget(m_roleBox);

	auto knobs = new QGridLayout();
	knobs->setHorizontalSpacing(2);
	m_density = knob(page, tr("DENS"), tr("Density:"));
	m_variation = knob(page, tr("VAR"), tr("Variation:"));
	m_velocity = knob(page, tr("VEL"), tr("Velocity:"));
	m_gate = knob(page, tr("GATE"), tr("Note length:"));
	m_octave = lcd(page, 2, tr("OCT"), tr("Octave"));
	knobs->addWidget(m_density, 0, 0, Qt::AlignHCenter);
	knobs->addWidget(m_variation, 0, 1, Qt::AlignHCenter);
	knobs->addWidget(m_velocity, 0, 2, Qt::AlignHCenter);
	knobs->addWidget(m_gate, 0, 3, Qt::AlignHCenter);
	knobs->addWidget(m_octave, 0, 4, Qt::AlignHCenter);
	layout->addLayout(knobs);

	// ---- note source
	layout->addWidget(heading(tr("NOTE SOURCE"), page));
	m_sourceBox = new ComboBox(page);
	m_sourceBox->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	m_sourceBox->setToolTip(tr("Fractal: the phrase engine (motifs, form, anti-repetition).\n"
		"Euclidean: k hits spread over n steps.  Weighted beat: pyo's Beat, chance by beat strength.\n"
		"Random: pyo's Xnoise distributions over the scale. All follow the key and chords."));
	layout->addWidget(m_sourceBox);
	m_sourcePages = new QStackedWidget(page);
	{
		auto p0 = new QWidget(m_sourcePages);
		auto l0 = new QVBoxLayout(p0);
		l0->setContentsMargins(0, 0, 0, 0);
		l0->addWidget(smallLabel(tr("The set writes the phrases (Music tab of the fractal window sets the rules)."), p0));
		m_sourcePages->addWidget(p0);

		auto p1 = new QWidget(m_sourcePages);
		auto l1 = new QHBoxLayout(p1);
		l1->setContentsMargins(0, 0, 0, 0);
		m_euHits = lcd(p1, 2, tr("HITS"), tr("Hits per bar"));
		m_euSteps = lcd(p1, 2, tr("STEPS"), tr("Steps per bar"));
		m_euRotate = lcd(p1, 2, tr("ROT"), tr("Rotation"));
		l1->addWidget(m_euHits);
		l1->addWidget(m_euSteps);
		l1->addWidget(m_euRotate);
		l1->addStretch();
		m_sourcePages->addWidget(p1);

		auto p2 = new QWidget(m_sourcePages);
		auto l2 = new QHBoxLayout(p2);
		l2->setContentsMargins(0, 0, 0, 0);
		m_beatTaps = lcd(p2, 2, tr("TAPS"), tr("Taps per bar (grouped in 7, 6, 5, 4, 3 or 2 like pyo)"));
		m_w1 = knob(p2, tr("DOWN"), tr("Downbeat chance:"), "%");
		m_w2 = knob(p2, tr("UP"), tr("Secondary beat chance:"), "%");
		m_w3 = knob(p2, tr("OFF"), tr("Off-beat chance:"), "%");
		l2->addWidget(m_beatTaps);
		l2->addWidget(m_w1);
		l2->addWidget(m_w2);
		l2->addWidget(m_w3);
		m_sourcePages->addWidget(p2);

		auto p3 = new QWidget(m_sourcePages);
		auto l3 = new QGridLayout(p3);
		l3->setContentsMargins(0, 0, 0, 0);
		m_dist = new ComboBox(p3);
		m_dist->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
		m_rate = new ComboBox(p3);
		m_rate->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
		m_x1 = knob(p3, tr("X1"), tr("Distribution x1 (pyo: slope / locator / max value):"));
		m_x2 = knob(p3, tr("X2"), tr("Distribution x2 (pyo: shape / bandwidth / max step):"));
		m_range = lcd(p3, 2, tr("RANGE"), tr("Spread in scale degrees around the chord root"));
		l3->addWidget(m_dist, 0, 0, 1, 2);
		l3->addWidget(m_rate, 0, 2, 1, 2);
		l3->addWidget(m_x1, 1, 0);
		l3->addWidget(m_x2, 1, 1);
		l3->addWidget(m_range, 1, 2);
		m_sourcePages->addWidget(p3);
	}
	layout->addWidget(m_sourcePages);
	auto steerRow = new QHBoxLayout();
	m_steer = knob(page, tr("STEER"), tr("How much the fractal bends the Euclidean / Beat / Random source:"));
	steerRow->addWidget(m_steer);
	steerRow->addWidget(smallLabel(tr("STEER: how much the set bends the pattern (rotation, hits, weights, distribution)."), page), 1);
	layout->addLayout(steerRow);

	// ---- chance and range
	layout->addWidget(heading(tr("CHANCE"), page));
	m_chanceGrid = new ChanceGrid(page);
	layout->addWidget(m_chanceGrid);
	auto cr = new QHBoxLayout();
	m_trackChance = knob(page, tr("TRACK"), tr("Chance for every note of this track:"), "%");
	m_humanize = knob(page, tr("HUMAN"), tr("Random velocity spread:"));
	m_lowKey = lcd(page, 3, tr("LOW"), tr("Lowest note (MIDI). Notes outside the range fold by octaves"));
	m_highKey = lcd(page, 3, tr("HIGH"), tr("Highest note (MIDI)"));
	cr->addWidget(m_trackChance);
	cr->addWidget(m_humanize);
	cr->addWidget(m_lowKey);
	cr->addWidget(m_highKey);
	cr->addStretch();
	layout->addLayout(cr);

	// ---- status
	m_status = smallLabel("", page);
	m_status->setMinimumHeight(40);
	layout->addWidget(m_status);
	m_engine = smallLabel("", page);
	layout->addWidget(m_engine);

	m_captureButton = new QPushButton(tr("Capture this phrase to a clip"), page);
	m_captureButton->setToolTip(tr("Writes the phrase that is playing now into a MIDI clip on this track"));
	connect(m_captureButton, SIGNAL(clicked()), this, SLOT(capture()));
	layout->addWidget(m_captureButton);

	m_windowButton = new QPushButton(tr("Open the fractal window"), page);
	connect(m_windowButton, SIGNAL(clicked()), getGUI()->mainWindow(), SLOT(toggleFractalWin()));
	layout->addWidget(m_windowButton);
	layout->addStretch();

	m_timer = new QTimer(this);
	connect(m_timer, SIGNAL(timeout()), this, SLOT(refreshStatus()));
	m_timer->start(300);
	setModel(gen);
}

void FractalGeneratorView::modelChanged()
{
	if (m_gen != nullptr) { disconnect(&m_gen->m_sourceModel, nullptr, this, nullptr); }
	m_gen = castModel<FractalGenerator>();
	if (m_gen == nullptr) { return; }
	m_roleBox->setModel(&m_gen->m_roleModel);
	m_density->setModel(&m_gen->m_densityModel);
	m_variation->setModel(&m_gen->m_variationModel);
	m_velocity->setModel(&m_gen->m_velocityModel);
	m_gate->setModel(&m_gen->m_gateModel);
	m_octave->setModel(&m_gen->m_octaveModel);
	m_sourceBox->setModel(&m_gen->m_sourceModel);
	m_steer->setModel(&m_gen->m_steerModel);
	m_euHits->setModel(&m_gen->m_euHitsModel);
	m_euSteps->setModel(&m_gen->m_euStepsModel);
	m_euRotate->setModel(&m_gen->m_euRotateModel);
	m_beatTaps->setModel(&m_gen->m_beatTapsModel);
	m_w1->setModel(&m_gen->m_w1Model);
	m_w2->setModel(&m_gen->m_w2Model);
	m_w3->setModel(&m_gen->m_w3Model);
	m_dist->setModel(&m_gen->m_distModel);
	m_rate->setModel(&m_gen->m_rateModel);
	m_x1->setModel(&m_gen->m_x1Model);
	m_x2->setModel(&m_gen->m_x2Model);
	m_range->setModel(&m_gen->m_rangeModel);
	m_trackChance->setModel(&m_gen->m_trackChanceModel);
	m_humanize->setModel(&m_gen->m_humanizeModel);
	m_lowKey->setModel(&m_gen->m_lowKeyModel);
	m_highKey->setModel(&m_gen->m_highKeyModel);
	m_chanceGrid->setGenerator(m_gen);
	m_sourcePages->setCurrentIndex(m_gen->m_sourceModel.value());
	connect(&m_gen->m_sourceModel, &Model::dataChanged, this, [this] {
		if (m_gen != nullptr) { m_sourcePages->setCurrentIndex(m_gen->m_sourceModel.value()); }
	});
	refreshStatus();
}

void FractalGeneratorView::refreshStatus()
{
	if (!isVisible() || m_gen == nullptr) { return; }
	auto* fe = FractalEngine::instance();
	if (m_gen->m_roleModel.value() == 0)
	{
		m_status->setText(tr("Role is Off: this track only plays its clips."));
	}
	else if (fe == nullptr || !fe->running())
	{
		m_status->setText(tr("Press play in the Song Editor: this track joins in as %1.")
			.arg(fractal::roleName(m_gen->m_roleModel.value())));
	}
	else
	{
		const auto st = m_gen->status();
		m_status->setText(tr("Phrase %1 (%2) - %3 notes over %4 bars.\nLikeness to earlier material: %5% (%6 tries).")
			.arg(st.phrase + 1).arg(QChar(st.form)).arg(st.notes).arg(st.bars)
			.arg(static_cast<int>(st.similarity * 100.0f)).arg(st.attempts));
	}
	if (fe != nullptr)
	{
		auto& c = fe->conductor();
		m_engine->setText(tr("Set: %1 %2  |  %3 %4  |  escape %5  chaos %6")
			.arg(c.probeX.load(), 0, 'f', 5).arg(c.probeY.load(), 0, 'f', 5)
			.arg(fractal::scales::rootName(c.currentKeyRoot.load())).arg(fractal::scales::name(c.currentScale.load()))
			.arg(c.currentEscape.load(), 0, 'f', 2).arg(c.currentChaos.load(), 0, 'f', 2));
	}
	m_captureButton->setEnabled(fe != nullptr && fe->running() && m_gen->m_roleModel.value() != 0);
}

void FractalGeneratorView::capture()
{
	if (m_gen == nullptr) { return; }
	const int n = m_gen->captureToClip();
	m_status->setText(n > 0 ? tr("Captured %1 notes into a new clip.").arg(n) : tr("Nothing to capture yet."));
}

} // namespace lmms::gui
