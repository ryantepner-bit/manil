/*
 * FractalGenerator.cpp - per-track live fractal note generation.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalGenerator.h"

#include <QDomElement>
#include <QHash>

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "FractalEngine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"

namespace lmms
{

FractalGenerator::FractalGenerator(InstrumentTrack* track) :
	Model(nullptr, tr("Fractal generator")),
	m_roleModel(this, tr("Fractal role")),
	m_densityModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Density")),
	m_octaveModel(0, -3, 3, this, tr("Octave")),
	m_variationModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Variation")),
	m_velocityModel(1.0f, 0.2f, 1.5f, 0.01f, this, tr("Velocity")),
	m_gateModel(1.0f, 0.2f, 3.0f, 0.01f, this, tr("Gate")),
	m_sourceModel(this, tr("Note source")),
	m_steerModel(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Fractal steering")),
	m_euHitsModel(5, 0, 32, this, tr("Euclid hits")),
	m_euStepsModel(16, 1, 32, this, tr("Euclid steps")),
	m_euRotateModel(0, 0, 31, this, tr("Euclid rotation")),
	m_beatTapsModel(16, 2, 32, this, tr("Beat taps")),
	m_w1Model(80.0f, 0.0f, 100.0f, 1.0f, this, tr("Downbeat chance")),
	m_w2Model(50.0f, 0.0f, 100.0f, 1.0f, this, tr("Upbeat chance")),
	m_w3Model(30.0f, 0.0f, 100.0f, 1.0f, this, tr("Off-beat chance")),
	m_distModel(this, tr("Distribution")),
	m_rateModel(this, tr("Note rate")),
	m_x1Model(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Distribution x1")),
	m_x2Model(0.5f, 0.0f, 1.0f, 0.01f, this, tr("Distribution x2")),
	m_rangeModel(7, 1, 24, this, tr("Range (scale degrees)")),
	m_trackChanceModel(100.0f, 0.0f, 100.0f, 1.0f, this, tr("Track chance")),
	m_humanizeModel(0.0f, 0.0f, 1.0f, 0.01f, this, tr("Humanize velocity")),
	m_lowKeyModel(0, 0, 127, this, tr("Lowest note")),
	m_highKeyModel(127, 0, 127, this, tr("Highest note")),
	m_rng(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(track) * 2654435761u) ^ 0x5bd1e995u),
	m_xnoise(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(track)) ^ 0x27d4eb2du),
	m_urn(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(track)) ^ 0x165667b1u),
	m_track(track)
{
	for (int r = 0; r < static_cast<int>(fractal::Role::NumRoles); ++r) { m_roleModel.addItem(fractal::roleName(r)); }
	m_roleModel.setValue(0);
	m_sourceModel.addItem(tr("Fractal (phrase engine)"));
	m_sourceModel.addItem(tr("Euclidean (pyo Euclide)"));
	m_sourceModel.addItem(tr("Weighted beat (pyo Beat)"));
	m_sourceModel.addItem(tr("Random (pyo Xnoise)"));
	for (int d = 0; d < static_cast<int>(fractal::pyo::Dist::Count); ++d) { m_distModel.addItem(fractal::pyo::distName(d)); }
	for (const char* r : {"16ths", "8ths", "Quarters", "Half notes", "Triplet 8ths"}) { m_rateModel.addItem(tr(r)); }
	for (int i = 0; i < ChanceSteps; ++i)
	{
		m_chance[static_cast<size_t>(i)] = std::make_unique<FloatModel>(100.0f, 0.0f, 100.0f, 1.0f, this,
			tr("Chance step %1").arg(i + 1));
	}
	// any change to the source settings rebuilds the alternative phrase at the next phrase
	for (auto* m : std::initializer_list<AutomatableModel*>{&m_sourceModel, &m_euHitsModel, &m_euStepsModel,
		&m_euRotateModel, &m_beatTapsModel, &m_w1Model, &m_w2Model, &m_w3Model, &m_distModel, &m_rateModel,
		&m_x1Model, &m_x2Model, &m_rangeModel})
	{
		connect(m, &AutomatableModel::dataChanged, this, [this] { m_altBuiltFor = UINT64_MAX; }, Qt::DirectConnection);
	}
	// a new role / setting takes effect at the next phrase; a role change restarts the motif
	connect(&m_roleModel, &ComboBoxModel::dataChanged, this, [this] { m_gen.reset(); }, Qt::DirectConnection);
}

fractal::PartSettings FractalGenerator::settings() const
{
	fractal::PartSettings ps;
	ps.role = role();
	ps.density = m_densityModel.value();
	ps.octave = m_octaveModel.value();
	ps.variation = m_variationModel.value();
	ps.velocity = m_velocityModel.value();
	ps.gate = m_gateModel.value();
	ps.channelSeed = m_track != nullptr ? static_cast<int>(qHash(m_track->name()) & 0xffff) : 0;
	// LMMS keys are MIDI - 12 (A4 = 57); the generator works in MIDI
	ps.baseKey = (m_track != nullptr ? m_track->baseNote() : 57) + 12;
	if (ps.role == fractal::Role::Break && m_track != nullptr && m_track->instrument() != nullptr)
	{
		const auto points = m_track->instrument()->fractalSlicePoints();
		const float beats = m_track->instrument()->fractalSliceBeats();
		if (points.size() >= 2 && beats > 0.0f)
		{
			ps.breakBeats = beats;
			for (size_t i = 0; i + 1 < points.size(); ++i) { ps.sliceBeats.push_back(points[i] * beats); }
		}
	}
	return ps;
}

const std::vector<fractal::GenNote>& FractalGenerator::activeNotes() const
{
	return m_sourceModel.value() == SourceFractal ? m_gen.notes() : m_altNotes;
}

void FractalGenerator::processTick(int offsetFrames, float framesPerTick)
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr || !fe->running() || m_roleModel.value() == 0 || m_track == nullptr || m_track->isMuted()) { return; }
	auto& c = fe->conductor();
	const auto& plan = c.plan();
	const bool fractalSource = m_sourceModel.value() == SourceFractal;
	const bool stale = fractalSource ? m_gen.builtFor() != plan.serial : m_altBuiltFor != plan.serial;
	if (stale)
	{
		const auto ps = settings();
		if (fractalSource) { m_gen.build(plan, ps, c.history(), m_track->name().toStdString()); }
		else { buildAlternative(plan, ps); }
		const auto& notes = activeNotes();
		std::unique_lock<std::mutex> g(m_statusLock, std::try_to_lock);
		if (g.owns_lock())
		{
			m_phraseCopy = notes;
			m_status.phrase = plan.serial;
			m_status.form = plan.formLetter;
			m_status.similarity = fractalSource ? m_gen.lastSimilarity() : 0.0f;
			m_status.attempts = fractalSource ? m_gen.lastAttempts() : 1;
			m_status.notes = static_cast<int>(notes.size());
			m_status.bars = plan.bars;
		}
		m_nextNote = 0;
		m_lastTick = -1;
	}
	const int t = c.tickInPhrase();
	const auto& notes = activeNotes();
	if (t < m_lastTick) { m_nextNote = 0; }
	m_lastTick = t;
	while (m_nextNote < notes.size() && notes[m_nextNote].tick < t) { ++m_nextNote; }
	const bool drum = fractal::isDrumRole(role());
	const float trackChance = m_trackChanceModel.value() * 0.01f;
	const float human = m_humanizeModel.value();
	const int lowKey = std::min(m_lowKeyModel.value(), m_highKeyModel.value());
	const int highKey = std::max(m_lowKeyModel.value(), m_highKeyModel.value());
	for (size_t i = m_nextNote; i < notes.size() && notes[i].tick == t; ++i)
	{
		const auto& n = notes[i];
		// chance: per 16th step of the bar and for the whole track, rolled every time it plays
		const int step = (n.tick % fractal::TicksPerBar) / fractal::TicksPer16th;
		const float chance = m_chance[static_cast<size_t>(fractal::clampv(step, 0, ChanceSteps - 1))]->value() * 0.01f * trackChance;
		if (chance < 1.0f && m_rng.uniform() >= chance) { continue; }
		// note range: fold by octaves into [low, high] (drums keep their sample key)
		int pitch = n.pitch;
		if (!drum && highKey - lowKey >= 11)
		{
			while (pitch < lowKey) { pitch += 12; }
			while (pitch > highKey) { pitch -= 12; }
		}
		const int key = pitch - 12;
		if (key < 0 || key >= NumKeys) { continue; }
		int vel = n.velocity;
		if (human > 0.0f) { vel = static_cast<int>(vel * (1.0 + (m_rng.uniform() - 0.5) * human * 0.7)); }
		const auto volume = static_cast<volume_t>(fractal::clampv(vel * 100 / 112, 1, static_cast<int>(MaxVolume)));
		Note note(TimePos(n.length), TimePos(0), key, volume, DefaultPanning);
		// drums play the whole sample (step note), everything else is held for its length
		if (drum) { note.setType(Note::Type::Step); }
		const f_cnt_t frames = drum ? 0 : static_cast<f_cnt_t>(n.length * framesPerTick);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, offsetFrames, frames, note);
		Engine::audioEngine()->addPlayHandle(nph);
	}
}

void FractalGenerator::buildAlternative(const fractal::PhrasePlan& plan, const fractal::PartSettings& ps)
{
	using namespace fractal;
	m_altNotes.clear();
	m_altBuiltFor = plan.serial;
	const Role r = ps.role;
	const bool drum = isDrumRole(r) || r == Role::Break;
	const int oct = PartGenerator::naturalOctave(r) + ps.octave;
	const float steer = m_steerModel.value();
	const int source = m_sourceModel.value();
	auto velOf = [&](float v01) { return clampv(static_cast<int>(v01 * 112.0f * ps.velocity), 1, 127); };
	// pitch for a hit: drums their key; Break walks the slices without repeats; the rest the bar's chord tones
	int chordIdx = 0;
	auto pitchFor = [&](const BarInfo& bi, int idx) {
		if (r == Role::Break)
		{
			const int slices = std::max(1, static_cast<int>(ps.sliceBeats.size()));
			return ps.baseKey + m_urn.next(slices);
		}
		if (drum) { return ps.baseKey; }
		const int size = std::max(1, bi.chordSize);
		const int deg = bi.chordDegs[static_cast<size_t>(((idx % size) + size) % size)];
		return plan.key.pitch(deg, oct);
	};

	if (source == SourceEuclid)
	{
		for (int b = 0; b < plan.bars; ++b)
		{
			const auto& bi = plan.bar[static_cast<size_t>(b)];
			const int n = m_euStepsModel.value();
			// the set nudges the pattern: escape time rotates it, the boundary adds or removes hits
			const int rot = m_euRotateModel.value() + static_cast<int>(std::lround(steer * bi.feat.escape * n));
			const int hits = clampv(m_euHitsModel.value() + static_cast<int>(std::lround(steer * (bi.feat.boundary - 0.5) * n * 0.5)), 0, n);
			const auto pat = pyo::euclid(hits, n, rot);
			const int stepTicks = std::max(1, TicksPerBar / n);
			for (int s = 0; s < n; ++s)
			{
				if (!pat[static_cast<size_t>(s)]) { continue; }
				const int len = drum ? TicksPer16th : std::max(3, static_cast<int>(stepTicks * 0.9f * ps.gate));
				m_altNotes.push_back({b * TicksPerBar + s * stepTicks, len, pitchFor(bi, chordIdx++), velOf(s == 0 ? 1.0f : 0.8f)});
			}
		}
	}
	else if (source == SourceBeat)
	{
		// one pyo Beat pattern per phrase (pyo regenerates on demand); the set's chaos shifts the weights
		const auto& f0 = plan.bar[0].feat;
		const int shift = static_cast<int>(steer * (f0.chaos - 0.5) * 60.0);
		const auto pat = pyo::beat(m_beatTapsModel.value(), clampv(static_cast<int>(m_w1Model.value()) + shift, 0, 100),
			clampv(static_cast<int>(m_w2Model.value()) + shift, 0, 100), clampv(static_cast<int>(m_w3Model.value()) + shift, 0, 100), m_rng);
		const int taps = static_cast<int>(pat.hit.size());
		const int stepTicks = std::max(1, TicksPerBar / taps);
		for (int b = 0; b < plan.bars; ++b)
		{
			const auto& bi = plan.bar[static_cast<size_t>(b)];
			for (int s = 0; s < taps; ++s)
			{
				if (!pat.hit[static_cast<size_t>(s)]) { continue; }
				const int len = drum ? TicksPer16th : std::max(3, static_cast<int>(stepTicks * 0.9f * ps.gate));
				m_altNotes.push_back({b * TicksPerBar + s * stepTicks, len, pitchFor(bi, chordIdx++), velOf(pat.accent[static_cast<size_t>(s)])});
			}
		}
	}
	else if (source == SourceRandom)
	{
		static const int rateTicks[] = {TicksPer16th, TicksPer16th * 2, TicksPer16th * 4, TicksPer16th * 8, 16};
		const int stepTicks = rateTicks[clampv(m_rateModel.value(), 0, 4)];
		m_xnoise.dist = static_cast<pyo::Dist>(m_distModel.value());
		const int range = m_rangeModel.value();
		for (int b = 0; b < plan.bars; ++b)
		{
			const auto& bi = plan.bar[static_cast<size_t>(b)];
			// the set moves the distribution's parameters: escape time x1, chaos x2
			m_xnoise.x1 = clampv(m_x1Model.value() + steer * (static_cast<float>(bi.feat.escape) - 0.5f), 0.0f, 1.0f);
			m_xnoise.x2 = clampv(m_x2Model.value() + steer * (static_cast<float>(bi.feat.chaos) - 0.5f), 0.0f, 1.0f);
			for (int t = 0; t < TicksPerBar; t += stepTicks)
			{
				const double v = m_xnoise.next();
				if (drum)
				{
					if (v < 1.0 - ps.density) { continue; }
					m_altNotes.push_back({b * TicksPerBar + t, TicksPer16th, pitchFor(bi, 0), velOf(0.6f + 0.4f * static_cast<float>(v))});
					continue;
				}
				if (m_rng.uniform() > 0.25 + 0.75 * ps.density) { continue; } // rests
				const int deg = bi.chordRoot + static_cast<int>(std::lround(v * range)) - range / 2;
				const int len = std::max(3, static_cast<int>(stepTicks * 0.9f * ps.gate));
				m_altNotes.push_back({b * TicksPerBar + t, len, plan.key.pitch(deg, oct), velOf(0.65f + 0.3f * static_cast<float>(v))});
			}
		}
	}
	std::sort(m_altNotes.begin(), m_altNotes.end(), [](const GenNote& a, const GenNote& b) { return a.tick < b.tick; });
}

FractalGenerator::Status FractalGenerator::status() const
{
	std::lock_guard<std::mutex> g(m_statusLock);
	return m_status;
}

std::vector<fractal::GenNote> FractalGenerator::currentPhrase() const
{
	std::lock_guard<std::mutex> g(m_statusLock);
	return m_phraseCopy;
}

int FractalGenerator::writeClip(const std::vector<fractal::GenNote>& notes, int bars, const QString& name)
{
	if (m_track == nullptr || notes.empty()) { return 0; }
	auto* song = Engine::getSong();
	const int barTicks = TimePos::ticksPerBar();
	const int pos = (song->getPlayPos(Song::PlayMode::Song).getTicks() / barTicks) * barTicks;
	auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(pos)));
	if (clip == nullptr) { return 0; }
	const bool drum = fractal::isDrumRole(role());
	int count = 0;
	for (const auto& n : notes)
	{
		const int key = n.pitch - 12;
		if (key < 0 || key >= NumKeys) { continue; }
		Note note(TimePos(drum ? 12 : n.length), TimePos(n.tick * barTicks / fractal::TicksPerBar), key,
			static_cast<volume_t>(fractal::clampv(n.velocity * 100 / 112, 1, static_cast<int>(MaxVolume))), DefaultPanning);
		clip->addNote(note, false);
		++count;
	}
	clip->changeLength(TimePos(bars * barTicks));
	clip->setName(name);
	return count;
}

int FractalGenerator::captureToClip()
{
	const auto st = status();
	return writeClip(currentPhrase(), std::max(1, st.bars),
		QString("%1 %2%3").arg(fractal::roleName(m_roleModel.value())).arg(QChar(st.form)).arg(st.phrase));
}

void FractalGenerator::saveSettings(QDomDocument& doc, QDomElement& element)
{
	m_roleModel.saveSettings(doc, element, "role");
	m_densityModel.saveSettings(doc, element, "density");
	m_octaveModel.saveSettings(doc, element, "octave");
	m_variationModel.saveSettings(doc, element, "variation");
	m_velocityModel.saveSettings(doc, element, "velocity");
	m_gateModel.saveSettings(doc, element, "gate");
	m_sourceModel.saveSettings(doc, element, "source");
	m_steerModel.saveSettings(doc, element, "steer");
	m_euHitsModel.saveSettings(doc, element, "euhits");
	m_euStepsModel.saveSettings(doc, element, "eusteps");
	m_euRotateModel.saveSettings(doc, element, "eurotate");
	m_beatTapsModel.saveSettings(doc, element, "beattaps");
	m_w1Model.saveSettings(doc, element, "w1");
	m_w2Model.saveSettings(doc, element, "w2");
	m_w3Model.saveSettings(doc, element, "w3");
	m_distModel.saveSettings(doc, element, "dist");
	m_rateModel.saveSettings(doc, element, "rate");
	m_x1Model.saveSettings(doc, element, "x1");
	m_x2Model.saveSettings(doc, element, "x2");
	m_rangeModel.saveSettings(doc, element, "range");
	m_trackChanceModel.saveSettings(doc, element, "chance");
	m_humanizeModel.saveSettings(doc, element, "humanize");
	m_lowKeyModel.saveSettings(doc, element, "lowkey");
	m_highKeyModel.saveSettings(doc, element, "highkey");
	for (int i = 0; i < ChanceSteps; ++i) { m_chance[static_cast<size_t>(i)]->saveSettings(doc, element, QString("chance%1").arg(i)); }
}

void FractalGenerator::loadSettings(const QDomElement& element)
{
	m_roleModel.loadSettings(element, "role");
	m_densityModel.loadSettings(element, "density");
	m_octaveModel.loadSettings(element, "octave");
	m_variationModel.loadSettings(element, "variation");
	m_velocityModel.loadSettings(element, "velocity");
	m_gateModel.loadSettings(element, "gate");
	auto load = [&](AutomatableModel& m, const QString& name) {
		if (element.hasAttribute(name) || !element.firstChildElement(name).isNull()) { m.loadSettings(element, name); }
	};
	load(m_sourceModel, "source");
	load(m_steerModel, "steer");
	load(m_euHitsModel, "euhits");
	load(m_euStepsModel, "eusteps");
	load(m_euRotateModel, "eurotate");
	load(m_beatTapsModel, "beattaps");
	load(m_w1Model, "w1");
	load(m_w2Model, "w2");
	load(m_w3Model, "w3");
	load(m_distModel, "dist");
	load(m_rateModel, "rate");
	load(m_x1Model, "x1");
	load(m_x2Model, "x2");
	load(m_rangeModel, "range");
	load(m_trackChanceModel, "chance");
	load(m_humanizeModel, "humanize");
	load(m_lowKeyModel, "lowkey");
	load(m_highKeyModel, "highkey");
	for (int i = 0; i < ChanceSteps; ++i) { load(*m_chance[static_cast<size_t>(i)], QString("chance%1").arg(i)); }
	m_gen.reset();
	m_altBuiltFor = UINT64_MAX;
}

} // namespace lmms
