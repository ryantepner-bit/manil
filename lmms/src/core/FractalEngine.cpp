/*
 * FractalEngine.cpp - the song-wide MANDELBROT fractal engine.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalEngine.h"

#include <QDir>
#include <QDomElement>

#include "ConfigManager.h"
#include "Engine.h"
#include "Song.h"

namespace lmms
{

namespace
{
FractalEngine* s_instance = nullptr;
}

FractalEngine* FractalEngine::instance()
{
	return s_instance;
}

void FractalEngine::create()
{
	if (s_instance == nullptr) { s_instance = new FractalEngine(); }
}

FractalEngine::FractalEngine() :
	Model(nullptr, tr("Fractal engine")),
	m_enabledModel(true, this, tr("Fractal engine on")),
	m_styleModel(this, tr("Style")),
	m_rootModel(this, tr("Key")),
	m_scaleModel(this, tr("Scale")),
	m_fractalKeyModel(false, this, tr("Fractal chooses the mode")),
	m_driftModel(0.35f, 0.0f, 1.0f, 0.01f, this, tr("Drift")),
	m_zoomModel(0.02f, 0.0005f, 0.2f, 0.0005f, this, tr("Zoom")),
	m_complexityModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Complexity")),
	m_fractalControlModel(0.6f, 0.0f, 1.0f, 0.01f, this, tr("Fractal control")),
	m_swingModel(0.0f, 0.0f, 0.6f, 0.01f, this, tr("Swing")),
	m_noveltyModel(0.8f, 0.0f, 1.0f, 0.01f, this, tr("Novelty")),
	m_noveltyModeModel(this, tr("Avoid repeating")),
	m_chordSpeedModel(this, tr("Chord changes")),
	m_phraseBarsModel(this, tr("Phrase length")),
	m_probeXModel(-0.7436f, -2.5f, 1.5f, 0.000001f, this, tr("Probe X")),
	m_probeYModel(0.1318f, -1.5f, 1.5f, 0.000001f, this, tr("Probe Y"))
{
	for (int s = 0; s < static_cast<int>(fractal::Style::NumStyles); ++s) { m_styleModel.addItem(fractal::styleName(s)); }
	for (int r = 0; r < 12; ++r) { m_rootModel.addItem(fractal::scales::rootName(r)); }
	for (int k = 0; k < fractal::scales::NumKinds; ++k) { m_scaleModel.addItem(fractal::scales::name(k)); }
	m_noveltyModeModel.addItem(tr("Off (allow repeats)"));
	m_noveltyModeModel.addItem(tr("This session"));
	m_noveltyModeModel.addItem(tr("Everything ever played"));
	m_chordSpeedModel.addItem(tr("Fractal decides"));
	for (int b = 1; b <= 4; ++b) { m_chordSpeedModel.addItem(tr("Every %1 bar(s)").arg(b)); }
	m_phraseBarsModel.addItem(tr("Style default"));
	for (int b : {1, 2, 4, 8}) { m_phraseBarsModel.addItem(tr("%1 bar(s)").arg(b)); }
	m_rootModel.setValue(9);
	m_scaleModel.setValue(fractal::scales::Dorian);
	m_noveltyModeModel.setValue(1);

	for (auto* m : std::initializer_list<AutomatableModel*>{&m_styleModel, &m_rootModel, &m_scaleModel, &m_fractalKeyModel,
		&m_driftModel, &m_zoomModel, &m_complexityModel, &m_fractalControlModel, &m_swingModel, &m_noveltyModel,
		&m_noveltyModeModel, &m_chordSpeedModel, &m_phraseBarsModel})
	{
		connect(m, SIGNAL(dataChanged()), this, SLOT(updateConductor()), Qt::DirectConnection);
	}
	connect(&m_styleModel, &ComboBoxModel::dataChanged, this, [this]
	{
		if (!m_applyingStyle) { applyStyle(m_styleModel.value()); }
	});

	m_conductor.history().load(historyFile().toStdString());
	m_conductor.history().markSessionStart();
	m_conductor.moveProbe(m_probeXModel.value(), m_probeYModel.value());
	updateConductor();

	connect(&m_saveTimer, SIGNAL(timeout()), this, SLOT(periodicSave()));
	m_saveTimer.start(20000);
}

FractalEngine::~FractalEngine()
{
	saveHistory();
	if (s_instance == this) { s_instance = nullptr; }
}

QString FractalEngine::historyFile() const
{
	QDir dir(ConfigManager::inst()->workingDir());
	dir.mkpath("mandelbrot");
	return dir.filePath("mandelbrot/phrase-history.txt");
}

void FractalEngine::saveHistory()
{
	m_conductor.history().save(historyFile().toStdString());
}

void FractalEngine::periodicSave()
{
	if (m_conductor.history().dirty()) { saveHistory(); }
}

void FractalEngine::updateConductor()
{
	fractal::GlobalSettings s;
	s.style = static_cast<fractal::Style>(m_styleModel.value());
	s.root = m_rootModel.value();
	s.scale = m_scaleModel.value();
	s.fractalKey = m_fractalKeyModel.value();
	s.drift = m_driftModel.value();
	s.zoom = m_zoomModel.value();
	s.complexity = m_complexityModel.value();
	s.fractalControl = m_fractalControlModel.value();
	s.swing = m_swingModel.value();
	s.novelty = m_noveltyModel.value();
	s.noveltyMode = static_cast<fractal::NoveltyMode>(m_noveltyModeModel.value());
	s.chordSpeed = m_chordSpeedModel.value();
	static const int bars[] = {0, 1, 2, 4, 8};
	s.phraseBars = bars[fractal::clampv(m_phraseBarsModel.value(), 0, 4)];
	m_conductor.setSettings(s);
}

void FractalEngine::applyStyle(int style)
{
	// style presets: tempo and the engine's character (the tracks' roles are set by the templates)
	m_applyingStyle = true;
	m_styleModel.setValue(style);
	switch (static_cast<fractal::Style>(style))
	{
	case fractal::Style::Ambient:
		m_driftModel.setValue(0.25f); m_complexityModel.setValue(0.35f); m_swingModel.setValue(0.0f);
		m_chordSpeedModel.setValue(0);
		if (Engine::getSong()) { Engine::getSong()->tempoModel().setValue(76); }
		break;
	case fractal::Style::Breakbeat:
		m_driftModel.setValue(0.4f); m_complexityModel.setValue(0.6f); m_swingModel.setValue(0.05f);
		if (Engine::getSong()) { Engine::getSong()->tempoModel().setValue(170); }
		break;
	default:
		m_driftModel.setValue(0.35f); m_complexityModel.setValue(0.5f); m_swingModel.setValue(0.0f);
		if (Engine::getSong()) { Engine::getSong()->tempoModel().setValue(128); }
		break;
	}
	m_applyingStyle = false;
	updateConductor();
	emit styleApplied(style);
}

void FractalEngine::processTick(int songTick)
{
	if (!m_enabledModel.value())
	{
		if (m_conductor.running()) { m_conductor.stop(); }
		return;
	}
	if (!m_conductor.running()) { m_conductor.start(songTick); }
	else { m_conductor.tick(); }
}

void FractalEngine::transportStopped()
{
	m_conductor.stop();
}

void FractalEngine::reset()
{
	m_enabledModel.setValue(true);
	m_applyingStyle = true;
	m_styleModel.setValue(0);
	m_applyingStyle = false;
	m_rootModel.setValue(9);
	m_scaleModel.setValue(fractal::scales::Dorian);
	m_fractalKeyModel.setValue(false);
	m_driftModel.setValue(0.35f);
	m_zoomModel.setValue(0.02f);
	m_complexityModel.setValue(0.5f);
	m_fractalControlModel.setValue(0.6f);
	m_swingModel.setValue(0.0f);
	m_noveltyModel.setValue(0.8f);
	m_noveltyModeModel.setValue(1);
	m_chordSpeedModel.setValue(0);
	m_phraseBarsModel.setValue(0);
	updateConductor();
}

void FractalEngine::saveSettings(QDomDocument& doc, QDomElement& element)
{
	m_probeXModel.setValue(static_cast<float>(m_conductor.probeX.load()));
	m_probeYModel.setValue(static_cast<float>(m_conductor.probeY.load()));
	m_enabledModel.saveSettings(doc, element, "enabled");
	m_styleModel.saveSettings(doc, element, "style");
	m_rootModel.saveSettings(doc, element, "root");
	m_scaleModel.saveSettings(doc, element, "scale");
	m_fractalKeyModel.saveSettings(doc, element, "fractalkey");
	m_driftModel.saveSettings(doc, element, "drift");
	m_zoomModel.saveSettings(doc, element, "zoom");
	m_complexityModel.saveSettings(doc, element, "complexity");
	m_fractalControlModel.saveSettings(doc, element, "fractalcontrol");
	m_swingModel.saveSettings(doc, element, "swing");
	m_noveltyModel.saveSettings(doc, element, "novelty");
	m_noveltyModeModel.saveSettings(doc, element, "noveltymode");
	m_chordSpeedModel.saveSettings(doc, element, "chordspeed");
	m_phraseBarsModel.saveSettings(doc, element, "phrasebars");
	element.setAttribute("probex", QString::number(m_conductor.probeX.load(), 'g', 15));
	element.setAttribute("probey", QString::number(m_conductor.probeY.load(), 'g', 15));
}

void FractalEngine::loadSettings(const QDomElement& element)
{
	m_applyingStyle = true;
	m_enabledModel.loadSettings(element, "enabled");
	m_styleModel.loadSettings(element, "style");
	m_rootModel.loadSettings(element, "root");
	m_scaleModel.loadSettings(element, "scale");
	m_fractalKeyModel.loadSettings(element, "fractalkey");
	m_driftModel.loadSettings(element, "drift");
	m_zoomModel.loadSettings(element, "zoom");
	m_complexityModel.loadSettings(element, "complexity");
	m_fractalControlModel.loadSettings(element, "fractalcontrol");
	m_swingModel.loadSettings(element, "swing");
	m_noveltyModel.loadSettings(element, "novelty");
	m_noveltyModeModel.loadSettings(element, "noveltymode");
	m_chordSpeedModel.loadSettings(element, "chordspeed");
	m_phraseBarsModel.loadSettings(element, "phrasebars");
	m_applyingStyle = false;
	if (element.hasAttribute("probex"))
	{
		m_conductor.moveProbe(element.attribute("probex").toDouble(), element.attribute("probey").toDouble());
	}
	updateConductor();
}

} // namespace lmms
