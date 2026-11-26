/*
 * FractalEngine.h - the song-wide MANDELBROT fractal engine (probe, key, rhythm and harmony
 *                   controls, novelty, phrase history) and its settings, saved with the project.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_ENGINE_H
#define LMMS_FRACTAL_ENGINE_H

#include <array>

#include <QObject>
#include <QTimer>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "JournallingObject.h"
#include "Model.h"
#include "fractal/Conductor.h"
#include "lmms_export.h"

namespace lmms
{

class LMMS_EXPORT FractalEngine : public Model, public JournallingObject
{
	Q_OBJECT
public:
	//! Created by the Song (main thread); never null after that
	static FractalEngine* instance();
	static void create();

	~FractalEngine() override;

	fractal::Conductor& conductor() { return m_conductor; }
	fractal::PhraseHistory& history() { return m_conductor.history(); }

	//! Audio thread, first frame of every song tick (Song::processNextBuffer)
	void processTick(int songTick);
	//! Transport stopped
	void transportStopped();
	bool running() const { return m_conductor.running(); }

	//! Back to the defaults (new project)
	void reset();

	//! GUI: move the probe there and make it the anchor (orbit centre, box centre)
	void placeProbe(double x, double y);
	double anchorX() const { return m_anchorX; }
	double anchorY() const { return m_anchorY; }
	//! Bookmarks 0..3: store the probe's current position / jump there / forget
	void storeBookmark(int slot);
	void recallBookmark(int slot);
	void clearBookmark(int slot);
	bool bookmarkUsed(int slot) const { return m_bookUsed[static_cast<size_t>(slot & 3)]; }
	double bookmarkX(int slot) const { return m_bookX[static_cast<size_t>(slot & 3)]; }
	double bookmarkY(int slot) const { return m_bookY[static_cast<size_t>(slot & 3)]; }

	QString historyFile() const;
	void saveHistory();

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "fractalengine"; }

	BoolModel m_enabledModel;
	// key and harmony
	ComboBoxModel m_rootModel;
	ComboBoxModel m_scaleModel;
	BoolModel m_fractalKeyModel;
	ComboBoxModel m_paletteModel;
	ComboBoxModel m_chordSpeedModel;
	// rhythm and phrasing
	FloatModel m_sustainModel;
	FloatModel m_syncopationModel;
	ComboBoxModel m_gridModel;
	FloatModel m_swingModel;
	ComboBoxModel m_phraseBarsModel;
	FloatModel m_complexityModel;
	FloatModel m_fractalControlModel;
	// novelty
	FloatModel m_noveltyModel;
	ComboBoxModel m_noveltyModeModel;
	// the probe
	ComboBoxModel m_probeModeModel;
	FloatModel m_driftModel;
	FloatModel m_turnModel;
	FloatModel m_edgePullModel;
	IntModel m_stepsModel;
	FloatModel m_zoomModel;
	FloatModel m_breatheModel;
	IntModel m_maxIterModel;
	IntModel m_readGridModel;
	FloatModel m_orbitRadiusModel;
	IntModel m_jumpBarsModel;
	FloatModel m_boxModel;
	BoolModel m_freezeModel;
	FloatModel m_probeXModel;  //!< saved start position of the probe
	FloatModel m_probeYModel;

signals:
	void bookmarksChanged();

private slots:
	void updateConductor();
	void periodicSave();

private:
	FractalEngine();
	fractal::Conductor m_conductor;
	QTimer m_saveTimer;
	double m_anchorX = -0.7436, m_anchorY = 0.1318;
	std::array<double, 4> m_bookX {}, m_bookY {};
	std::array<bool, 4> m_bookUsed {};
	bool m_loading = false;
};

} // namespace lmms

#endif // LMMS_FRACTAL_ENGINE_H
