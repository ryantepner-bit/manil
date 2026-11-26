/*
 * FractalGenerator.h - per instrument track: plays the fractal engine's music for this track's
 *                      role, live, while the song plays. Shown in the FRACTAL tab.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_GENERATOR_H
#define LMMS_FRACTAL_GENERATOR_H

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "AutomatableModel.h"
#include "ComboBoxModel.h"
#include "JournallingObject.h"
#include "LmmsTypes.h"
#include "Model.h"
#include "fractal/Conductor.h"
#include "fractal/PyoGen.h"
#include "lmms_export.h"

namespace lmms
{

class InstrumentTrack;
class TimePos;

class LMMS_EXPORT FractalGenerator : public Model, public JournallingObject
{
	Q_OBJECT
public:
	explicit FractalGenerator(InstrumentTrack* track);

	//! Audio thread: from InstrumentTrack::play, once per tick
	void processTick(int offsetFrames, float framesPerTick);

	//! GUI: the notes of the phrase now playing (copy) and its info
	struct Status
	{
		uint64_t phrase = 0;
		char form = 'A';
		float similarity = 0;
		int attempts = 0;
		int notes = 0;
		int bars = 0;
	};
	Status status() const;
	std::vector<fractal::GenNote> currentPhrase() const;

	//! GUI: write the phrase now playing (or the last `phrases` phrases) into a MIDI clip on
	//! this track at the start of the bar under the play head. Returns the number of notes.
	int captureToClip();
	//! GUI: write any remembered phrase into a clip on this track
	int writeClip(const std::vector<fractal::GenNote>& notes, int bars, const QString& name);

	fractal::Role role() const { return static_cast<fractal::Role>(m_roleModel.value()); }
	void setRole(fractal::Role r) { m_roleModel.setValue(static_cast<int>(r)); }

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return "fractalgen"; }

	ComboBoxModel m_roleModel;
	FloatModel m_densityModel;
	IntModel m_octaveModel;
	FloatModel m_variationModel;
	FloatModel m_velocityModel;
	FloatModel m_gateModel;

	// ---- where the notes come from
	enum Source { SourceFractal = 0, SourceEuclid, SourceBeat, SourceRandom, NumSources };
	ComboBoxModel m_sourceModel;
	FloatModel m_steerModel;      //!< how much the fractal steers the Euclid / Beat / Random sources
	IntModel m_euHitsModel, m_euStepsModel, m_euRotateModel;
	IntModel m_beatTapsModel;
	FloatModel m_w1Model, m_w2Model, m_w3Model;
	ComboBoxModel m_distModel, m_rateModel;
	FloatModel m_x1Model, m_x2Model;
	IntModel m_rangeModel;        //!< Random source: spread in scale degrees
	// ---- what gets through
	static constexpr int ChanceSteps = 16;
	FloatModel* chanceModel(int step) { return m_chance[static_cast<size_t>(step & 15)].get(); }
	FloatModel m_trackChanceModel; //!< % chance for every note of this track
	FloatModel m_humanizeModel;    //!< random velocity spread
	IntModel m_lowKeyModel, m_highKeyModel; //!< note range (MIDI); notes outside fold by octaves

private:
	fractal::PartSettings settings() const;
	void buildAlternative(const fractal::PhrasePlan& plan, const fractal::PartSettings& ps);
	const std::vector<fractal::GenNote>& activeNotes() const;

	std::array<std::unique_ptr<FloatModel>, ChanceSteps> m_chance;
	std::vector<fractal::GenNote> m_altNotes;
	uint64_t m_altBuiltFor = UINT64_MAX;
	fractal::pyo::Random m_rng;
	fractal::pyo::Xnoise m_xnoise;
	fractal::pyo::Urn m_urn;

	InstrumentTrack* m_track;
	fractal::PartGenerator m_gen;
	mutable std::mutex m_statusLock;
	std::vector<fractal::GenNote> m_phraseCopy; //!< for the GUI / capture (guarded)
	Status m_status;
	size_t m_nextNote = 0;
	int m_lastTick = -1;
};

} // namespace lmms

#endif // LMMS_FRACTAL_GENERATOR_H
