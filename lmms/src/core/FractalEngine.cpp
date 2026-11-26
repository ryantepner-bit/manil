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
	m_rootModel(this, tr("Key")),
	m_scaleModel(this, tr("Scale")),
	m_fractalKeyModel(false, this, tr("The set chooses the mode")),
	m_paletteModel(this, tr("Mode palette")),
	m_chordSpeedModel(this, tr("Harmonic rhythm")),
	m_sustainModel(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Sustain")),
	m_syncopationModel(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Syncopation")),
	m_gridModel(this, tr("Rhythmic grid")),
	m_swingModel(0.0f, 0.0f, 0.6f, 0.01f, this, tr("Swing")),
	m_phraseBarsModel(this, tr("Phrase length")),
	m_complexityModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Complexity")),
	m_fractalControlModel(0.6f, 0.0f, 1.0f, 0.01f, this, tr("Fractal control")),
	m_noveltyModel(0.8f, 0.0f, 1.0f, 0.01f, this, tr("Novelty")),
	m_noveltyModeModel(this, tr("Avoid repeating")),
	m_probeModeModel(this, tr("Probe movement")),
	m_driftModel(0.35f, 0.0f, 1.0f, 0.01f, this, tr("Probe speed")),
	m_turnModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Probe turning")),
	m_edgePullModel(0.6f, 0.0f, 1.0f, 0.01f, this, tr("Pull to the edge")),
	m_stepsModel(1, 1, 8, this, tr("Probe steps per bar")),
	m_zoomModel(0.02f, 0.00001f, 0.3f, 0.00001f, this, tr("Read radius")),
	m_breatheModel(0.3f, 0.0f, 1.0f, 0.01f, this, tr("Radius breathing")),
	m_maxIterModel(300, 60, 2000, this, tr("Iteration depth")),
	m_readGridModel(8, 4, 16, this, tr("Read resolution")),
	m_orbitRadiusModel(0.5f, 0.05f, 4.0f, 0.01f, this, tr("Orbit radius")),
	m_jumpBarsModel(8, 1, 64, this, tr("Bars between jumps")),
	m_boxModel(0.0f, 0.0f, 0.5f, 0.0005f, this, tr("Boundary box")),
	m_freezeModel(false, this, tr("Freeze probe")),
	m_probeXModel(-0.7436f, -2.5f, 1.5f, 0.000001f, this, tr("Probe X")),
	m_probeYModel(0.1318f, -1.5f, 1.5f, 0.000001f, this, tr("Probe Y"))
{
	for (int r = 0; r < 12; ++r) { m_rootModel.addItem(fractal::scales::rootName(r)); }
	for (int k = 0; k < fractal::scales::NumKinds; ++k) { m_scaleModel.addItem(fractal::scales::name(k)); }
	m_paletteModel.addItem(tr("Dark modes"));
	m_paletteModel.addItem(tr("Bright modes"));
	m_noveltyModeModel.addItem(tr("Off (allow repeats)"));
	m_noveltyModeModel.addItem(tr("This session"));
	m_noveltyModeModel.addItem(tr("Everything ever played"));
	m_chordSpeedModel.addItem(tr("Set decides"));
	for (int b = 1; b <= 4; ++b) { m_chordSpeedModel.addItem(tr("Every %1 bar(s)").arg(b)); }
	m_phraseBarsModel.addItem(tr("Auto (4, 8 when sustained)"));
	for (int b : {1, 2, 4, 8}) { m_phraseBarsModel.addItem(tr("%1 bar(s)").arg(b)); }
	m_gridModel.addItem(tr("Set decides"));
	m_gridModel.addItem(tr("Straight 16ths"));
	m_gridModel.addItem(tr("Triplets"));
	for (int m = 0; m < fractal::GlobalSettings::NumProbeModes; ++m) { m_probeModeModel.addItem(fractal::probeModeName(m)); }
	m_zoomModel.setScaleLogarithmic(true);
	m_boxModel.setScaleLogarithmic(true);
	m_rootModel.setValue(9);
	m_scaleModel.setValue(fractal::scales::Dorian);
	m_noveltyModeModel.setValue(1);

	for (auto* m : std::initializer_list<AutomatableModel*>{&m_rootModel, &m_scaleModel, &m_fractalKeyModel,
		&m_paletteModel, &m_chordSpeedModel, &m_sustainModel, &m_syncopationModel, &m_gridModel, &m_swingModel,
		&m_phraseBarsModel, &m_complexityModel, &m_fractalControlModel, &m_noveltyModel, &m_noveltyModeModel,
		&m_probeModeModel, &m_driftModel, &m_turnModel, &m_edgePullModel, &m_stepsModel, &m_zoomModel,
		&m_breatheModel, &m_maxIterModel, &m_readGridModel, &m_orbitRadiusModel, &m_jumpBarsModel, &m_boxModel,
		&m_freezeModel})
	{
		connect(m, SIGNAL(dataChanged()), this, SLOT(updateConductor()), Qt::DirectConnection);
	}

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
	if (m_loading) { return; }
	fractal::GlobalSettings s;
	s.root = m_rootModel.value();
	s.scale = m_scaleModel.value();
	s.fractalKey = m_fractalKeyModel.value();
	s.palette = m_paletteModel.value();
	s.chordSpeed = m_chordSpeedModel.value();
	s.sustain = m_sustainModel.value();
	s.syncopation = m_syncopationModel.value();
	s.grid = m_gridModel.value();
	s.swing = m_swingModel.value();
	static const int bars[] = {0, 1, 2, 4, 8};
	s.phraseBars = bars[fractal::clampv(m_phraseBarsModel.value(), 0, 4)];
	s.complexity = m_complexityModel.value();
	s.fractalControl = m_fractalControlModel.value();
	s.novelty = m_noveltyModel.value();
	s.noveltyMode = static_cast<fractal::NoveltyMode>(m_noveltyModeModel.value());
	s.probeMode = m_probeModeModel.value();
	s.drift = m_driftModel.value();
	s.turn = m_turnModel.value();
	s.edgePull = m_edgePullModel.value();
	s.stepsPerBar = m_stepsModel.value();
	s.zoom = m_zoomModel.value();
	s.breathe = m_breatheModel.value();
	s.maxIter = m_maxIterModel.value();
	s.readGrid = m_readGridModel.value();
	s.orbitRadius = m_orbitRadiusModel.value();
	s.jumpBars = m_jumpBarsModel.value();
	s.boxSize = m_boxModel.value();
	s.freeze = m_freezeModel.value();
	s.anchorX = m_anchorX;
	s.anchorY = m_anchorY;
	s.bookX = m_bookX;
	s.bookY = m_bookY;
	s.bookUsed = m_bookUsed;
	m_conductor.setSettings(s);
}

void FractalEngine::placeProbe(double x, double y)
{
	m_anchorX = x;
	m_anchorY = y;
	m_conductor.moveProbe(x, y);
	updateConductor();
}

void FractalEngine::storeBookmark(int slot)
{
	const auto i = static_cast<size_t>(slot & 3);
	m_bookX[i] = m_conductor.probeX.load();
	m_bookY[i] = m_conductor.probeY.load();
	m_bookUsed[i] = true;
	updateConductor();
	emit bookmarksChanged();
}

void FractalEngine::recallBookmark(int slot)
{
	const auto i = static_cast<size_t>(slot & 3);
	if (!m_bookUsed[i]) { return; }
	placeProbe(m_bookX[i], m_bookY[i]);
}

void FractalEngine::clearBookmark(int slot)
{
	m_bookUsed[static_cast<size_t>(slot & 3)] = false;
	updateConductor();
	emit bookmarksChanged();
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
	m_conductor.publishCurrentBar();
}

void FractalEngine::transportStopped()
{
	m_conductor.stop();
}

void FractalEngine::reset()
{
	m_loading = true;
	m_enabledModel.setValue(true);
	m_rootModel.setValue(9);
	m_scaleModel.setValue(fractal::scales::Dorian);
	m_fractalKeyModel.setValue(false);
	m_paletteModel.setValue(0);
	m_chordSpeedModel.setValue(0);
	m_sustainModel.setValue(0.0f);
	m_syncopationModel.setValue(0.0f);
	m_gridModel.setValue(0);
	m_swingModel.setValue(0.0f);
	m_phraseBarsModel.setValue(0);
	m_complexityModel.setValue(0.5f);
	m_fractalControlModel.setValue(0.6f);
	m_noveltyModel.setValue(0.8f);
	m_noveltyModeModel.setValue(1);
	m_probeModeModel.setValue(0);
	m_driftModel.setValue(0.35f);
	m_turnModel.setValue(0.5f);
	m_edgePullModel.setValue(0.6f);
	m_stepsModel.setValue(1);
	m_zoomModel.setValue(0.02f);
	m_breatheModel.setValue(0.3f);
	m_maxIterModel.setValue(300);
	m_readGridModel.setValue(8);
	m_orbitRadiusModel.setValue(0.5f);
	m_jumpBarsModel.setValue(8);
	m_boxModel.setValue(0.0f);
	m_freezeModel.setValue(false);
	m_bookUsed.fill(false);
	m_loading = false;
	updateConductor();
	emit bookmarksChanged();
}

void FractalEngine::saveSettings(QDomDocument& doc, QDomElement& element)
{
	m_probeXModel.setValue(static_cast<float>(m_conductor.probeX.load()));
	m_probeYModel.setValue(static_cast<float>(m_conductor.probeY.load()));
	m_enabledModel.saveSettings(doc, element, "enabled");
	m_rootModel.saveSettings(doc, element, "root");
	m_scaleModel.saveSettings(doc, element, "scale");
	m_fractalKeyModel.saveSettings(doc, element, "fractalkey");
	m_paletteModel.saveSettings(doc, element, "palette");
	m_chordSpeedModel.saveSettings(doc, element, "chordspeed");
	m_sustainModel.saveSettings(doc, element, "sustain");
	m_syncopationModel.saveSettings(doc, element, "syncopation");
	m_gridModel.saveSettings(doc, element, "grid");
	m_swingModel.saveSettings(doc, element, "swing");
	m_phraseBarsModel.saveSettings(doc, element, "phrasebars");
	m_complexityModel.saveSettings(doc, element, "complexity");
	m_fractalControlModel.saveSettings(doc, element, "fractalcontrol");
	m_noveltyModel.saveSettings(doc, element, "novelty");
	m_noveltyModeModel.saveSettings(doc, element, "noveltymode");
	m_probeModeModel.saveSettings(doc, element, "probemode");
	m_driftModel.saveSettings(doc, element, "drift");
	m_turnModel.saveSettings(doc, element, "turn");
	m_edgePullModel.saveSettings(doc, element, "edgepull");
	m_stepsModel.saveSettings(doc, element, "steps");
	m_zoomModel.saveSettings(doc, element, "zoom");
	m_breatheModel.saveSettings(doc, element, "breathe");
	m_maxIterModel.saveSettings(doc, element, "maxiter");
	m_readGridModel.saveSettings(doc, element, "readgrid");
	m_orbitRadiusModel.saveSettings(doc, element, "orbitradius");
	m_jumpBarsModel.saveSettings(doc, element, "jumpbars");
	m_boxModel.saveSettings(doc, element, "box");
	m_freezeModel.saveSettings(doc, element, "freeze");
	element.setAttribute("probex", QString::number(m_conductor.probeX.load(), 'g', 15));
	element.setAttribute("probey", QString::number(m_conductor.probeY.load(), 'g', 15));
	element.setAttribute("anchorx", QString::number(m_anchorX, 'g', 15));
	element.setAttribute("anchory", QString::number(m_anchorY, 'g', 15));
	for (int i = 0; i < 4; ++i)
	{
		if (!m_bookUsed[static_cast<size_t>(i)]) { continue; }
		element.setAttribute(QString("book%1").arg(i),
			QString("%1,%2").arg(m_bookX[static_cast<size_t>(i)], 0, 'g', 15).arg(m_bookY[static_cast<size_t>(i)], 0, 'g', 15));
	}
}

void FractalEngine::loadSettings(const QDomElement& element)
{
	m_loading = true;
	auto load = [&](AutomatableModel& m, const char* name) {
		if (element.hasAttribute(name) || !element.firstChildElement(name).isNull()) { m.loadSettings(element, name); }
	};
	load(m_enabledModel, "enabled");
	load(m_rootModel, "root");
	load(m_scaleModel, "scale");
	load(m_fractalKeyModel, "fractalkey");
	load(m_paletteModel, "palette");
	load(m_chordSpeedModel, "chordspeed");
	load(m_sustainModel, "sustain");
	load(m_syncopationModel, "syncopation");
	load(m_gridModel, "grid");
	load(m_swingModel, "swing");
	load(m_phraseBarsModel, "phrasebars");
	load(m_complexityModel, "complexity");
	load(m_fractalControlModel, "fractalcontrol");
	load(m_noveltyModel, "novelty");
	load(m_noveltyModeModel, "noveltymode");
	load(m_probeModeModel, "probemode");
	load(m_driftModel, "drift");
	load(m_turnModel, "turn");
	load(m_edgePullModel, "edgepull");
	load(m_stepsModel, "steps");
	load(m_zoomModel, "zoom");
	load(m_breatheModel, "breathe");
	load(m_maxIterModel, "maxiter");
	load(m_readGridModel, "readgrid");
	load(m_orbitRadiusModel, "orbitradius");
	load(m_jumpBarsModel, "jumpbars");
	load(m_boxModel, "box");
	load(m_freezeModel, "freeze");
	// projects from before the technical controls stored a style preset
	if (element.hasAttribute("style") && !element.hasAttribute("sustain"))
	{
		const int style = element.attribute("style").toInt();
		m_sustainModel.setValue(style == 1 ? 0.8f : 0.0f);
		m_syncopationModel.setValue(style == 2 ? 0.6f : 0.0f);
		m_paletteModel.setValue(style == 1 ? 1 : 0);
	}
	if (element.hasAttribute("anchorx"))
	{
		m_anchorX = element.attribute("anchorx").toDouble();
		m_anchorY = element.attribute("anchory").toDouble();
	}
	for (int i = 0; i < 4; ++i)
	{
		const QString v = element.attribute(QString("book%1").arg(i));
		const auto parts = v.split(',');
		m_bookUsed[static_cast<size_t>(i)] = parts.size() == 2;
		if (parts.size() == 2)
		{
			m_bookX[static_cast<size_t>(i)] = parts[0].toDouble();
			m_bookY[static_cast<size_t>(i)] = parts[1].toDouble();
		}
	}
	m_loading = false;
	if (element.hasAttribute("probex"))
	{
		const double x = element.attribute("probex").toDouble(), y = element.attribute("probey").toDouble();
		m_conductor.moveProbe(x, y);
		if (!element.hasAttribute("anchorx")) { m_anchorX = x; m_anchorY = y; }
	}
	updateConductor();
	emit bookmarksChanged();
}

} // namespace lmms
