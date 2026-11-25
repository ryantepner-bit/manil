/*
 * FractalEngine.h - the song-wide MANDELBROT fractal engine (probe, style, key, novelty,
 *                   phrase history) and its settings, saved with the project.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_ENGINE_H
#define LMMS_FRACTAL_ENGINE_H

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
	//! Applies a style preset to the global settings (keeps the key)
	void applyStyle(int style);

	QString historyFile() const;
	void saveHistory();

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "fractalengine"; }

	BoolModel m_enabledModel;
	ComboBoxModel m_styleModel;
	ComboBoxModel m_rootModel;
	ComboBoxModel m_scaleModel;
	BoolModel m_fractalKeyModel;
	FloatModel m_driftModel;
	FloatModel m_zoomModel;
	FloatModel m_complexityModel;
	FloatModel m_fractalControlModel;
	FloatModel m_swingModel;
	FloatModel m_noveltyModel;
	ComboBoxModel m_noveltyModeModel;
	ComboBoxModel m_chordSpeedModel;
	ComboBoxModel m_phraseBarsModel;
	FloatModel m_probeXModel;  //!< saved start position of the probe
	FloatModel m_probeYModel;

signals:
	void styleApplied(int style);

private slots:
	void updateConductor();
	void periodicSave();

private:
	FractalEngine();
	fractal::Conductor m_conductor;
	QTimer m_saveTimer;
	bool m_applyingStyle = false;
};

} // namespace lmms

#endif // LMMS_FRACTAL_ENGINE_H
