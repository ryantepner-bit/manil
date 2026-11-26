/*
 * Conductor.h - the MANDELBROT fractal music engine.
 *
 * A probe wanders over the Mandelbrot set. At every phrase boundary the Conductor reads the
 * set where the probe is (escape time, attracting period, boundary distance, orbit chaos,
 * neighbourhood entropy and roughness, and the orbit of z -> z^2 + c itself) and turns it into
 * a PhrasePlan: the form letter (A, A', B, ...), the chord of every bar, the shared drum groove
 * and the key. Each instrument track owns a PartGenerator that turns the plan into the notes of
 * its role. With "fractal control" up, the set also decides the rules themselves (form, harmonic
 * rhythm, rhythmic cells, mode); with it down, the style's rules hold and the set only fills them.
 *
 * Every phrase is compared against the PhraseHistory before it is used; phrases that are too
 * close to old material are regenerated, so the music keeps moving.
 *
 * Migrated from MANDELBROT v0.3 Source/Live/LiveSequencer (rewritten from per-step to
 * per-phrase generation, plus history and anti-repetition).
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_CONDUCTOR_H
#define LMMS_FRACTAL_CONDUCTOR_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fractal/FractalMath.h"
#include "fractal/PhraseHistory.h"

namespace lmms::fractal
{

constexpr int TicksPerBar = 192;
constexpr int TicksPer16th = 12;
constexpr int MaxPhraseBars = 8;

enum class Style { IDM = 0, Ambient, Breakbeat, NumStyles };
const char* styleName(int s);

enum class Role
{
	Off = 0,
	Kick, Snare, Clap, HatClosed, HatOpen, Rim, Perc, TomLow, TomHigh, Cymbal, Shaker,
	Bass, Chords, Pad, Lead, Arp, Bells, Texture, Drone, Break,
	NumRoles
};
const char* roleName(int r);
bool isDrumRole(Role r);
//! Drum lanes of the shared groove are indexed like the drum roles: lane = role - Kick
constexpr int NumDrumLanes = static_cast<int>(Role::Shaker) - static_cast<int>(Role::Kick) + 1;

struct GlobalSettings
{
	int root = 9;             //!< A
	int scale = scales::Dorian;
	bool fractalKey = false;  //!< let the set choose the mode per section
	float drift = 0.35f;      //!< how fast the probe wanders (0 = still)
	float zoom = 0.02f;       //!< size of the neighbourhood that is read
	float complexity = 0.5f;
	float fractalControl = 0.6f; //!< how much the set decides the rules
	float swing = 0.0f;       //!< 0..0.6
	float novelty = 0.8f;     //!< how strongly old material is avoided
	NoveltyMode noveltyMode = NoveltyMode::Recent;
	int chordSpeed = 0;       //!< 0 = the set decides, else bars per chord
	int phraseBars = 0;       //!< 0 = 4 bars (8 when sustain >= 0.5)

	// ---- music, as technical controls (these replace the old style presets)
	float sustain = 0.0f;     //!< 0 = short, busy notes .. 1 = long, sparse, slow harmony
	float syncopation = 0.0f; //!< 0 = on the grid .. 1 = broken, off-beat (bass avoids the downbeats)
	int palette = 0;          //!< mode palette when the set chooses the mode: 0 dark, 1 bright
	int grid = 0;             //!< 0 = the set decides (odd periods give triplets), 1 straight, 2 triplets

	// ---- the probe
	enum ProbeMode { EdgeWalk = 0, FreeWalk, Orbit, Path, Jumps, Locked, NumProbeModes };
	int probeMode = EdgeWalk;
	float turn = 0.5f;        //!< how much the heading wanders per step
	float edgePull = 0.6f;    //!< how strongly the walk is drawn to the boundary of the set
	int stepsPerBar = 1;      //!< probe moves per bar (1..8)
	int maxIter = 300;        //!< iteration depth of the readings (60..2000)
	int readGrid = 8;         //!< neighbourhood samples per side (4..16)
	float breathe = 0.3f;     //!< how much the read radius swells from phrase to phrase
	float orbitRadius = 0.5f; //!< Orbit mode: radius, in read radii
	int jumpBars = 8;         //!< Jumps mode: bars between jumps
	float boxSize = 0.0f;     //!< > 0: the probe stays inside a box of this half-size around the anchor
	double anchorX = -0.7436, anchorY = 0.1318; //!< orbit centre / box centre (last click or bookmark)
	std::array<double, 4> bookX {}, bookY {};
	std::array<bool, 4> bookUsed {};
	bool freeze = false;      //!< hold the probe where it is (readings and music keep going)
};
const char* probeModeName(int m);

struct BarInfo
{
	Features feat;
	int chordRoot = 0;              //!< scale degree
	int chordSize = 3;
	std::array<int, 5> chordDegs {};//!< scale degrees
	bool chordChange = true;        //!< a new chord starts on this bar
	int chordBars = 1;              //!< how long this chord lasts from here
};

struct DrumHit { int tick; int velocity; };

struct PhrasePlan
{
	uint64_t serial = 0;           //!< phrase number since the session started
	uint64_t sectionFirstHistoryId = UINT64_MAX; //!< history ids >= this belong to the current section
	int bars = 4;
	int form = 0;                  //!< position in the section: 0 = A, others are variations / contrast
	char formLetter = 'A';
	int sectionPhrase = 0;         //!< 0..3
	Style style = Style::IDM;
	Key key;
	float complexity = 0.5f, swing = 0.0f, novelty = 0.8f, fractalControl = 0.6f;
	NoveltyMode noveltyMode = NoveltyMode::Recent;
	float energy = 0.5f;           //!< 0..1 dynamic arc of the section
	float sustain = 0.0f, syncopation = 0.0f;
	int grid = 0;
	uint32_t seed = 1;
	std::array<BarInfo, MaxPhraseBars> bar {};
	std::array<std::vector<DrumHit>, NumDrumLanes> drums;
	float drumSimilarity = 0.0f;   //!< similarity of the groove to old grooves (after regeneration)
	int drumAttempts = 1;

	const Features& feat() const { return bar[0].feat; }
	const BarInfo& barAt(int tick) const { return bar[static_cast<size_t>(clampv(tick / TicksPerBar, 0, bars - 1))]; }
};

//! Per-track generator settings (one set per instrument track)
struct PartSettings
{
	Role role = Role::Off;
	float density = 0.5f;
	int octave = 0;            //!< offset from the role's natural register
	float variation = 0.5f;    //!< how far variations move from the motif
	float velocity = 1.0f;     //!< scale
	float gate = 1.0f;         //!< length scale
	int channelSeed = 0;       //!< makes two tracks with the same role differ
	//! Break role: where the slices of the loaded break start, in beats of the break, and its length
	std::vector<float> sliceBeats;
	float breakBeats = 8.0f;
	int baseKey = 57 + 12;     //!< MIDI key that plays the sample unpitched (drums) / slice 0 base (break)
};

class Conductor
{
public:
	Conductor();

	//! GUI / main thread: new settings (picked up at the next phrase)
	void setSettings(const GlobalSettings& s);
	GlobalSettings settings() const;
	//! GUI: move the probe (applied at the next phrase boundary)
	void moveProbe(double cx, double cy);
	//! GUI: start a new direction / reseed the walk (never the same music twice)
	void reseed(uint32_t extra);

	/** Audio thread, once per song tick while playing. Returns true when a new phrase starts on
	 *  this tick (the plan was rebuilt). */
	bool tick();
	//! Audio thread: transport (re)started at song position `songTick`
	void start(int64_t songTick);
	void stop() { m_running = false; }
	bool running() const { return m_running; }

	const PhrasePlan& plan() const { return *m_plan; }
	int tickInPhrase() const { return m_tickInPhrase; }

	PhraseHistory& history() { return m_history; }
	const PhraseHistory& history() const { return m_history; }

	// for the GUI (lock free)
	std::atomic<double> probeX { -0.7436 }, probeY { 0.1318 };
	std::atomic<double> trailX[64], trailY[64];
	std::atomic<int> trailPos { 0 };
	std::atomic<uint64_t> phraseCounter { 0 };
	std::atomic<int> currentChordDegree { 0 }, currentBar { 0 }, currentForm { 0 };
	std::atomic<float> currentEscape { 0 }, currentChaos { 0 }, currentBoundary { 0 }, currentEntropy { 0 },
		currentRoughness { 0 }, currentDrumSimilarity { 0 };
	std::atomic<int> currentPeriod { 0 }, currentKeyRoot { 9 }, currentScale { 1 }, currentBars { 4 };
	// for MANDELBROT's effects (lock free, updated every tick): the sounding chord and key as
	// pitch-class bit masks (bit 0 = C), the chord's bass note and this bar's fractal readings
	std::atomic<int> currentChordMask { 0 }, currentScaleMask { 0xfff }, currentChordBass { 45 };
	std::atomic<float> barEscape { 0 }, barChaos { 0 }, barBoundary { 0 }, barEntropy { 0 };

	//! Builds a phrase plan now (also used by tests and by "render to clip")
	void buildNextPlan();

	//! Audio thread, every tick: publishes the sounding chord, key and this bar's readings for the
	//! effects (MandelCloud, MandelResonator, MandelSpectral read them lock free)
	void publishCurrentBar()
	{
		if (!m_running || !m_plan) { return; }
		const BarInfo& b = m_plan->barAt(m_tickInPhrase);
		const Key& key = m_plan->key;
		int chord = 0;
		for (int i = 0; i < b.chordSize && i < static_cast<int>(b.chordDegs.size()); ++i)
		{
			chord |= 1 << (((key.pitch(b.chordDegs[static_cast<std::size_t>(i)], 4) % 12) + 12) % 12);
		}
		int sc = 0;
		for (int i = 0; i < key.numDegs; ++i) { sc |= 1 << ((key.root + key.degs[static_cast<std::size_t>(i)]) % 12); }
		currentChordMask.store(chord, std::memory_order_relaxed);
		currentScaleMask.store(sc, std::memory_order_relaxed);
		currentChordBass.store(key.pitch(b.chordRoot, 2), std::memory_order_relaxed);
		barEscape.store(static_cast<float>(b.feat.escape), std::memory_order_relaxed);
		barChaos.store(static_cast<float>(b.feat.chaos), std::memory_order_relaxed);
		barBoundary.store(static_cast<float>(b.feat.boundary), std::memory_order_relaxed);
		barEntropy.store(static_cast<float>(b.feat.entropy), std::memory_order_relaxed);
	}

private:
	void walk(int bars);
	void planHarmony(PhrasePlan& p);
	void planDrums(PhrasePlan& p, int attempt);
	void storeDrumsInHistory(PhrasePlan& p);

	mutable std::mutex m_settingsLock;
	GlobalSettings m_settings, m_pending;
	bool m_settingsChanged = true;
	std::atomic<bool> m_probeRequest { false };
	std::atomic<double> m_reqX { 0 }, m_reqY { 0 };
	std::atomic<uint32_t> m_reseed { 0 };

	std::unique_ptr<PhrasePlan> m_plan;
	PhraseHistory m_history;
	bool m_running = false;
	int m_tickInPhrase = 0;
	uint64_t m_serial = 0;
	uint32_t m_sessionSeed;
	// probe walk
	double m_x = -0.7436, m_y = 0.1318, m_heading = 0.0;
	double m_orbitPhase = 0.0;
	int m_pathTarget = 0;
	int m_barsSinceJump = 0;
	Rng m_walkRng;
	// harmony memory
	int m_prevChord = -1;
	int m_sectionForm = 0;
	Key m_sectionKey;
};

//! Turns the conductor's plans into one track's notes.
class PartGenerator
{
public:
	/** Builds this track's phrase for `plan` (checks it against the history and regenerates when
	 *  it is too close to old material), stores it in the history and returns it. */
	const std::vector<GenNote>& build(const PhrasePlan& plan, const PartSettings& ps, PhraseHistory& history,
		const std::string& trackName);
	const std::vector<GenNote>& notes() const { return m_notes; }
	float lastSimilarity() const { return m_lastSimilarity; }
	int lastAttempts() const { return m_attempts; }
	uint64_t builtFor() const { return m_builtSerial; }
	void reset() { m_builtSerial = UINT64_MAX; m_haveMotif = false; }

	//! Natural MIDI octave of a role (for octave 0)
	static int naturalOctave(Role r);

private:
	void generate(const PhrasePlan& p, const PartSettings& ps, int attempt, std::vector<GenNote>& out);
	void genDrum(const PhrasePlan& p, const PartSettings& ps, std::vector<GenNote>& out);
	void genBass(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);
	void genChords(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out, bool pad);
	void genLead(const PhrasePlan& p, const PartSettings& ps, int attempt, std::vector<GenNote>& out);
	void genArp(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);
	void genBells(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);
	void genTexture(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);
	void genDrone(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);
	void genBreak(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out);

	std::vector<GenNote> m_notes;
	uint64_t m_builtSerial = UINT64_MAX;
	float m_lastSimilarity = 0.0f;
	int m_attempts = 0;
	// lead motif memory (the "A" of the section)
	struct MotifNote { int step; int len; int degree; int vel; };
	std::vector<MotifNote> m_motif;
	bool m_haveMotif = false;
	std::vector<int> m_lastVoicing; //!< MIDI pitches of the last chord (voice leading)
	int m_arpIndex = 0;
};

} // namespace lmms::fractal

#endif // LMMS_FRACTAL_CONDUCTOR_H
