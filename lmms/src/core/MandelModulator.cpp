/*
 * MandelModulator.cpp - the fractal / random modulation controller. See MandelModulator.h.
 *
 * Part of MANDELBROT, a fork of LMMS. Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "MandelModulator.h"

#include <QDomElement>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "ComboBox.h"
#include "Engine.h"
#include "FractalEngine.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "Song.h"

namespace lmms
{

const char* MandelModulator::sourceName(int s)
{
	static const char* n[] = {"Set: escape time (bar)", "Set: chaos (bar)", "Set: boundary distance (bar)",
		"Set: neighbourhood entropy (bar)", "Set: roughness (phrase)", "Probe X", "Probe Y", "Position in phrase",
		"Position in bar", "Chord degree"};
	if (s < RandomFirst) { return n[std::clamp(s, 0, RandomFirst - 1)]; }
	return fractal::pyo::distName(s - RandomFirst);
}

MandelModulator::MandelModulator(Model* parent) :
	Controller(ControllerType::Mandel, parent, tr("Mandel modulator")),
	m_sourceModel(this, tr("Source")),
	m_rateModel(this, tr("New value every")),
	m_minModel(0.0f, 0.0f, 1.0f, 0.001f, this, tr("Minimum")),
	m_maxModel(1.0f, 0.0f, 1.0f, 0.001f, this, tr("Maximum")),
	m_curveModel(0.0f, -1.0f, 1.0f, 0.01f, this, tr("Curve")),
	m_slewModel(0.2f, 0.0f, 1.0f, 0.01f, this, tr("Slew")),
	m_x1Model(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Random x1")),
	m_x2Model(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Random x2")),
	m_invertModel(false, this, tr("Invert")),
	m_noise(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)) ^ 0x2545f491u)
{
	for (int s = 0; s < NumSources; ++s)
	{
		m_sourceModel.addItem(s < RandomFirst ? tr(sourceName(s)) : tr("Random: %1").arg(sourceName(s)));
	}
	for (const char* r : {"16th", "8th", "Beat", "Bar", "2 bars", "4 bars", "Continuous"}) { m_rateModel.addItem(tr(r)); }
	m_rateModel.setValue(3);
	setSampleExact(false);
}

float MandelModulator::target()
{
	const int src = m_sourceModel.value();
	if (src >= RandomFirst)
	{
		m_noise.dist = static_cast<fractal::pyo::Dist>(src - RandomFirst);
		m_noise.x1 = m_x1Model.value();
		m_noise.x2 = m_x2Model.value();
		return static_cast<float>(m_noise.next());
	}
	auto* fe = FractalEngine::instance();
	if (fe == nullptr) { return 0.5f; }
	auto& c = fe->conductor();
	switch (src)
	{
	case BarEscape: return c.barEscape.load();
	case BarChaos: return c.barChaos.load();
	case BarBoundary: return c.barBoundary.load();
	case BarEntropy: return c.barEntropy.load();
	case PhraseRoughness: return c.currentRoughness.load();
	case ProbeX: return static_cast<float>((c.probeX.load() + 2.5) / 4.0);
	case ProbeY: return static_cast<float>((c.probeY.load() + 1.5) / 3.0);
	case PhrasePosition:
	{
		const int bars = std::max(1, c.currentBars.load());
		return static_cast<float>(c.tickInPhrase()) / (bars * fractal::TicksPerBar);
	}
	case BarPosition: return static_cast<float>(c.tickInPhrase() % fractal::TicksPerBar) / fractal::TicksPerBar;
	case ChordDegree: return static_cast<float>(((c.currentChordDegree.load() % 7) + 7) % 7) / 6.0f;
	default: return 0.5f;
	}
}

void MandelModulator::updateValueBuffer()
{
	static const int rateTicks[] = {12, 24, 48, 192, 384, 768, 0};
	const int rate = rateTicks[std::clamp(m_rateModel.value(), 0, 6)];
	auto* song = Engine::getSong();
	const long tick = song != nullptr ? song->getPlayPos().getTicks() : 0;
	const long slot = rate > 0 ? tick / rate : tick;
	if (slot != m_lastSlot || rate == 0)
	{
		m_lastSlot = slot;
		m_held = std::clamp(target(), 0.0f, 1.0f);
	}
	// shape: curve, invert, range
	float v = m_held;
	const float curve = m_curveModel.value();
	if (curve > 0.001f) { v = std::pow(v, 1.0f + curve * 3.0f); }
	else if (curve < -0.001f) { v = 1.0f - std::pow(1.0f - v, 1.0f - curve * 3.0f); }
	if (m_invertModel.value()) { v = 1.0f - v; }
	const float lo = m_minModel.value(), hi = m_maxModel.value();
	const float goal = lo + (hi - lo) * v;
	// slew per frame: 0 = jump, 1 = ~4 s glide
	const float sr = static_cast<float>(Engine::audioEngine()->outputSampleRate());
	const float slew = m_slewModel.value();
	const float coef = slew <= 0.0001f ? 1.0f : 1.0f - std::exp(-1.0f / (slew * slew * 4.0f * sr));
	for (float& f : m_valueBuffer)
	{
		m_out += (goal - m_out) * coef;
		f = std::clamp(m_out, 0.0f, 1.0f);
	}
	m_currentValue = m_out;
	m_bufferLastUpdated = s_periods;
}

void MandelModulator::saveSettings(QDomDocument& doc, QDomElement& element)
{
	Controller::saveSettings(doc, element);
	m_sourceModel.saveSettings(doc, element, "source");
	m_rateModel.saveSettings(doc, element, "rate");
	m_minModel.saveSettings(doc, element, "min");
	m_maxModel.saveSettings(doc, element, "max");
	m_curveModel.saveSettings(doc, element, "curve");
	m_slewModel.saveSettings(doc, element, "slew");
	m_x1Model.saveSettings(doc, element, "x1");
	m_x2Model.saveSettings(doc, element, "x2");
	m_invertModel.saveSettings(doc, element, "invert");
}

void MandelModulator::loadSettings(const QDomElement& element)
{
	Controller::loadSettings(element);
	m_sourceModel.loadSettings(element, "source");
	m_rateModel.loadSettings(element, "rate");
	m_minModel.loadSettings(element, "min");
	m_maxModel.loadSettings(element, "max");
	m_curveModel.loadSettings(element, "curve");
	m_slewModel.loadSettings(element, "slew");
	m_x1Model.loadSettings(element, "x1");
	m_x2Model.loadSettings(element, "x2");
	m_invertModel.loadSettings(element, "invert");
}

gui::ControllerDialog* MandelModulator::createDialog(QWidget* parent)
{
	return new gui::MandelModulatorDialog(this, parent);
}

namespace gui
{

MandelModulatorDialog::MandelModulatorDialog(Controller* controller, QWidget* parent) :
	ControllerDialog(controller, parent)
{
	auto* m = static_cast<MandelModulator*>(controller);
	setWindowTitle(tr("Mandel modulator"));
	auto v = new QVBoxLayout(this);
	v->setContentsMargins(8, 8, 8, 8);
	auto t = new QLabel(tr("<b style='color:#28dcff'>MANDEL MODULATOR</b>  "
		"<span style='color:#8a93a6'>link any knob to it (right-click a knob, Connect to controller)</span>"), this);
	t->setWordWrap(true);
	v->addWidget(t);
	auto g = new QGridLayout();
	auto src = new ComboBox(this);
	src->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	src->setModel(&m->m_sourceModel);
	auto rate = new ComboBox(this);
	rate->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	rate->setModel(&m->m_rateModel);
	g->addWidget(new QLabel(tr("Source"), this), 0, 0);
	g->addWidget(src, 0, 1, 1, 3);
	g->addWidget(new QLabel(tr("New value every"), this), 1, 0);
	g->addWidget(rate, 1, 1, 1, 3);
	v->addLayout(g);
	auto k = new QHBoxLayout();
	auto mk = [&](const QString& label, const QString& hint, FloatModel* model) {
		auto kn = new Knob(KnobType::Bright26, label, this, Knob::LabelRendering::LegacyFixedFontSize);
		kn->setHintText(hint, "");
		kn->setModel(model);
		k->addWidget(kn);
	};
	mk(tr("MIN"), tr("Output minimum:"), &m->m_minModel);
	mk(tr("MAX"), tr("Output maximum:"), &m->m_maxModel);
	mk(tr("CURVE"), tr("Curve (-1 log, 0 linear, 1 exponential):"), &m->m_curveModel);
	mk(tr("SLEW"), tr("Slew (0 jumps, 1 slow glide):"), &m->m_slewModel);
	mk(tr("X1"), tr("Random x1 (pyo Xnoise: slope / locator / max value):"), &m->m_x1Model);
	mk(tr("X2"), tr("Random x2 (pyo Xnoise: shape / bandwidth / max step):"), &m->m_x2Model);
	v->addLayout(k);
	auto inv = new LedCheckBox(tr("Invert"), this);
	inv->setModel(&m->m_invertModel);
	v->addWidget(inv);
	setFixedSize(sizeHint());
}

} // namespace gui

} // namespace lmms
