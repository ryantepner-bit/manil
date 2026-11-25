/*
 * FractalGenerator.cpp - per-track live fractal note generation.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "FractalGenerator.h"

#include <QDomElement>
#include <QHash>

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
	m_track(track)
{
	for (int r = 0; r < static_cast<int>(fractal::Role::NumRoles); ++r) { m_roleModel.addItem(fractal::roleName(r)); }
	m_roleModel.setValue(0);
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

void FractalGenerator::processTick(int offsetFrames, float framesPerTick)
{
	auto* fe = FractalEngine::instance();
	if (fe == nullptr || !fe->running() || m_roleModel.value() == 0 || m_track == nullptr || m_track->isMuted()) { return; }
	auto& c = fe->conductor();
	const auto& plan = c.plan();
	if (m_gen.builtFor() != plan.serial)
	{
		const auto& notes = m_gen.build(plan, settings(), c.history(), m_track->name().toStdString());
		std::unique_lock<std::mutex> g(m_statusLock, std::try_to_lock);
		if (g.owns_lock())
		{
			m_phraseCopy = notes;
			m_status.phrase = plan.serial;
			m_status.form = plan.formLetter;
			m_status.similarity = m_gen.lastSimilarity();
			m_status.attempts = m_gen.lastAttempts();
			m_status.notes = static_cast<int>(notes.size());
			m_status.bars = plan.bars;
		}
		m_nextNote = 0;
		m_lastTick = -1;
	}
	const int t = c.tickInPhrase();
	const auto& notes = m_gen.notes();
	if (t < m_lastTick) { m_nextNote = 0; }
	m_lastTick = t;
	while (m_nextNote < notes.size() && notes[m_nextNote].tick < t) { ++m_nextNote; }
	const bool drum = fractal::isDrumRole(role());
	for (size_t i = m_nextNote; i < notes.size() && notes[i].tick == t; ++i)
	{
		const auto& n = notes[i];
		const int key = n.pitch - 12;
		if (key < 0 || key >= NumKeys) { continue; }
		const auto volume = static_cast<volume_t>(fractal::clampv(n.velocity * 100 / 112, 1, static_cast<int>(MaxVolume)));
		Note note(TimePos(n.length), TimePos(0), key, volume, DefaultPanning);
		// drums play the whole sample (step note), everything else is held for its length
		if (drum) { note.setType(Note::Type::Step); }
		const f_cnt_t frames = drum ? 0 : static_cast<f_cnt_t>(n.length * framesPerTick);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, offsetFrames, frames, note);
		Engine::audioEngine()->addPlayHandle(nph);
	}
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
}

void FractalGenerator::loadSettings(const QDomElement& element)
{
	m_roleModel.loadSettings(element, "role");
	m_densityModel.loadSettings(element, "density");
	m_octaveModel.loadSettings(element, "octave");
	m_variationModel.loadSettings(element, "variation");
	m_velocityModel.loadSettings(element, "velocity");
	m_gateModel.loadSettings(element, "gate");
	m_gen.reset();
}

} // namespace lmms
