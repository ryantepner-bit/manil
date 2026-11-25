/*
 * FractalGeneratorView.cpp - the FRACTAL tab of the instrument window.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalGeneratorView.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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

FractalGeneratorView::FractalGeneratorView(FractalGenerator* gen, QWidget* parent) :
	QWidget(parent),
	ModelView(nullptr, this),
	m_gen(gen)
{
	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	auto title = new QLabel(tr("FRACTAL ENGINE"));
	title->setFont(adjustedToPixelSize(title->font(), DEFAULT_FONT_SIZE + 2));
	layout->addWidget(title);

	auto help = new QLabel(tr("Pick what this track plays. While the song plays, the Mandelbrot set writes "
		"it live, never repeating."));
	help->setWordWrap(true);
	help->setFont(adjustedToPixelSize(help->font(), DEFAULT_FONT_SIZE));
	layout->addWidget(help);

	auto roleLabel = new QLabel(tr("Role:"));
	roleLabel->setFont(adjustedToPixelSize(roleLabel->font(), DEFAULT_FONT_SIZE));
	layout->addWidget(roleLabel);
	m_roleBox = new ComboBox(this);
	m_roleBox->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	layout->addWidget(m_roleBox);

	auto knobs = new QGridLayout();
	knobs->setHorizontalSpacing(4);
	m_density = new Knob(KnobType::Bright26, tr("DENS"), this, Knob::LabelRendering::LegacyFixedFontSize);
	m_density->setHintText(tr("Density:"), "");
	m_variation = new Knob(KnobType::Bright26, tr("VAR"), this, Knob::LabelRendering::LegacyFixedFontSize);
	m_variation->setHintText(tr("Variation:"), "");
	m_velocity = new Knob(KnobType::Bright26, tr("VEL"), this, Knob::LabelRendering::LegacyFixedFontSize);
	m_velocity->setHintText(tr("Velocity:"), "");
	m_gate = new Knob(KnobType::Bright26, tr("GATE"), this, Knob::LabelRendering::LegacyFixedFontSize);
	m_gate->setHintText(tr("Note length:"), "");
	m_octave = new LcdSpinBox(2, this, tr("Octave"));
	m_octave->setLabel(tr("OCT"));
	knobs->addWidget(m_density, 0, 0, Qt::AlignHCenter);
	knobs->addWidget(m_variation, 0, 1, Qt::AlignHCenter);
	knobs->addWidget(m_velocity, 0, 2, Qt::AlignHCenter);
	knobs->addWidget(m_gate, 0, 3, Qt::AlignHCenter);
	knobs->addWidget(m_octave, 0, 4, Qt::AlignHCenter);
	layout->addLayout(knobs);

	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setFont(adjustedToPixelSize(m_status->font(), DEFAULT_FONT_SIZE));
	m_status->setMinimumHeight(44);
	layout->addWidget(m_status);

	m_engine = new QLabel(this);
	m_engine->setWordWrap(true);
	m_engine->setFont(adjustedToPixelSize(m_engine->font(), DEFAULT_FONT_SIZE));
	layout->addWidget(m_engine);

	m_captureButton = new QPushButton(tr("Capture this phrase to a clip"), this);
	m_captureButton->setToolTip(tr("Writes the phrase that is playing now into a MIDI clip on this track"));
	connect(m_captureButton, SIGNAL(clicked()), this, SLOT(capture()));
	layout->addWidget(m_captureButton);

	m_windowButton = new QPushButton(tr("Open the fractal window"), this);
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
	m_gen = castModel<FractalGenerator>();
	if (m_gen == nullptr) { return; }
	m_roleBox->setModel(&m_gen->m_roleModel);
	m_density->setModel(&m_gen->m_densityModel);
	m_variation->setModel(&m_gen->m_variationModel);
	m_velocity->setModel(&m_gen->m_velocityModel);
	m_gate->setModel(&m_gen->m_gateModel);
	m_octave->setModel(&m_gen->m_octaveModel);
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
