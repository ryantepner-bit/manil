/*
 * Conductor.cpp - phrase planning (probe walk, form, harmony, groove) for the fractal engine.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "fractal/Conductor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace lmms::fractal
{

const char* styleName(int s)
{
	static const char* n[] = {"IDM", "Ambient", "Breakbeat"};
	return n[clampv(s, 0, static_cast<int>(Style::NumStyles) - 1)];
}

const char* roleName(int r)
{
	static const char* n[] = {"Off", "Kick", "Snare", "Clap", "Closed hat", "Open hat", "Rim", "Perc", "Low tom",
		"High tom", "Cymbal", "Shaker", "Bass", "Chords", "Pad", "Lead", "Arp", "Bells", "Texture", "Drone", "Break"};
	return n[clampv(r, 0, static_cast<int>(Role::NumRoles) - 1)];
}

bool isDrumRole(Role r) { return r >= Role::Kick && r <= Role::Shaker; }

namespace
{
inline bool euclid(int pulses, int steps, int i)
{
	if (pulses <= 0 || steps <= 0) { return false; }
	if (pulses >= steps) { return true; }
	i = ((i % steps) + steps) % steps;
	return ((i * pulses) % steps) < pulses;
}

inline float orbitA(const Features& f, int i) { return f.orbitAngle[static_cast<size_t>(((i % f.orbitCount) + f.orbitCount) % f.orbitCount)]; }
inline float orbitR(const Features& f, int i) { return f.orbitRadius[static_cast<size_t>(((i % f.orbitCount) + f.orbitCount) % f.orbitCount)]; }

constexpr int GrooveRole = static_cast<int>(Role::NumRoles); //!< history role id of the shared drum groove

enum DrumLane { LKick = 0, LSnare, LClap, LHatC, LHatO, LRim, LPerc, LTomL, LTomH, LCym, LShaker };

// form codes (what the lead and the groove do in each phrase of a 4-phrase section)
enum Form { FormA = 0, FormA1, FormB, FormA2, FormC, FormB1 };
const int formPatterns[4][4] = {
	{FormA, FormA1, FormB, FormA2},
	{FormA, FormB, FormA1, FormB1},
	{FormA, FormA1, FormA2, FormB},
	{FormA, FormB, FormC, FormA2},
};
const char formLetters[] = {'A', 'a', 'B', 'A', 'C', 'b'};

float threshold(float novelty) { return 0.96f - 0.36f * clampv(novelty, 0.0f, 1.0f); }
} // namespace

// =================================================================== Conductor
Conductor::Conductor()
{
	// a fresh seed every session: the music never starts the same way twice
	std::random_device rd;
	const auto now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	m_sessionSeed = hash32(rd() ^ static_cast<uint32_t>(now) ^ static_cast<uint32_t>(now >> 32)) | 1u;
	m_walkRng = Rng(m_sessionSeed);
	m_heading = m_walkRng.uni() * 6.2831853;
	for (auto& t : trailX) { t.store(m_x); }
	for (auto& t : trailY) { t.store(m_y); }
	m_plan = std::make_unique<PhrasePlan>();
	m_sectionKey.set(m_settings.root, m_settings.scale);
}

void Conductor::setSettings(const GlobalSettings& s)
{
	std::lock_guard<std::mutex> g(m_settingsLock);
	m_pending = s;
	m_settingsChanged = true;
}

GlobalSettings Conductor::settings() const
{
	std::lock_guard<std::mutex> g(m_settingsLock);
	return m_settingsChanged ? m_pending : m_settings;
}

void Conductor::moveProbe(double cx, double cy)
{
	m_reqX.store(cx); m_reqY.store(cy);
	m_probeRequest.store(true);
	probeX.store(cx); probeY.store(cy);
}

void Conductor::reseed(uint32_t extra) { m_reseed.store(extra | 1u); }

void Conductor::start(int64_t songTick)
{
	m_running = true;
	buildNextPlan();
	m_tickInPhrase = static_cast<int>(((songTick % TicksPerBar) + TicksPerBar) % TicksPerBar);
}

bool Conductor::tick()
{
	if (!m_running) { return false; }
	++m_tickInPhrase;
	currentBar.store(m_tickInPhrase / TicksPerBar);
	const auto& b = m_plan->barAt(m_tickInPhrase);
	currentChordDegree.store(b.chordRoot);
	if (m_tickInPhrase >= m_plan->bars * TicksPerBar)
	{
		buildNextPlan();
		m_tickInPhrase = 0;
		return true;
	}
	return false;
}

const char* probeModeName(int m)
{
	static const char* n[] = {"Edge walk", "Free walk", "Orbit", "Path (bookmarks)", "Jumps", "Locked"};
	return n[clampv(m, 0, static_cast<int>(GlobalSettings::NumProbeModes) - 1)];
}

void Conductor::walk(int bars)
{
	const GlobalSettings& s = m_settings;
	const double zoom = std::max(1e-9, static_cast<double>(s.zoom));
	if (s.freeze || s.probeMode == GlobalSettings::Locked) { return; }
	const int steps = clampv(s.stepsPerBar, 1, 8);
	const double speed = (0.02 + std::pow(clampv(s.drift, 0.0f, 1.0f), 2.0) * 0.9) * zoom / steps;
	const int searchIter = clampv(s.maxIter / 2, 40, 600);
	for (int b = 0; b < bars; ++b)
	{
		++m_barsSinceJump;
		for (int st = 0; st < steps; ++st)
		{
			switch (s.probeMode)
			{
			case GlobalSettings::Orbit:
			{
				// circle the anchor; drift sets the angular speed
				const double r = std::max(1e-9, static_cast<double>(s.orbitRadius) * zoom);
				m_orbitPhase += (0.05 + clampv(s.drift, 0.0f, 1.0f) * 0.6) / steps + m_walkRng.bi() * 0.05 * s.turn;
				const double rr = r * (1.0 + 0.25 * s.turn * m_walkRng.bi());
				m_x = s.anchorX + std::cos(m_orbitPhase) * rr;
				m_y = s.anchorY + std::sin(m_orbitPhase) * rr;
				break;
			}
			case GlobalSettings::Path:
			{
				// glide from bookmark to bookmark (skips empty slots; without any it walks the edge)
				int target = -1;
				for (int k = 0; k < 4; ++k)
				{
					const int idx = (m_pathTarget + k) % 4;
					if (s.bookUsed[static_cast<size_t>(idx)]) { target = idx; break; }
				}
				if (target < 0) { goto edge; }
				m_pathTarget = target;
				const double tx = s.bookX[static_cast<size_t>(target)], ty = s.bookY[static_cast<size_t>(target)];
				const double dx = tx - m_x, dy = ty - m_y, dist = std::sqrt(dx * dx + dy * dy);
				const double v = std::max(speed * 4.0, 1e-9);
				if (dist <= v) { m_x = tx; m_y = ty; m_pathTarget = (target + 1) % 4; }
				else
				{
					m_heading = std::atan2(dy, dx) + m_walkRng.bi() * 0.3 * s.turn;
					m_x += std::cos(m_heading) * v;
					m_y += std::sin(m_heading) * v;
				}
				break;
			}
			case GlobalSettings::Jumps:
				if (st == 0 && m_barsSinceJump >= std::max(1, s.jumpBars))
				{
					// teleport to a random boundary point (inside the box if there is one)
					m_barsSinceJump = 0;
					for (int tries = 0; tries < 40; ++tries)
					{
						double nx, ny;
						if (s.boxSize > 0.0f)
						{
							nx = s.anchorX + m_walkRng.bi() * s.boxSize;
							ny = s.anchorY + m_walkRng.bi() * s.boxSize;
						}
						else
						{
							nx = -2.0 + m_walkRng.uni() * 2.6;
							ny = m_walkRng.bi() * 1.2;
						}
						const float e = math::pixel(nx, ny, searchIter);
						if (e > 0.2f && e < 1.0f) { m_x = nx; m_y = ny; break; }
					}
					break;
				}
				[[fallthrough]];
			case GlobalSettings::FreeWalk:
			case GlobalSettings::EdgeWalk:
			default:
			edge:
			{
				if (s.drift <= 0.0001f) { break; }
				const bool edgeMode = s.probeMode != GlobalSettings::FreeWalk;
				const float pull = edgeMode ? clampv(s.edgePull, 0.0f, 1.0f) : 0.0f;
				const double spread = 0.3 + 2.5 * clampv(s.turn, 0.0f, 1.0f);
				double bestH = m_heading, bestScore = -1e9;
				const int tries = pull > 0.0f ? 6 : 1;
				for (int k = 0; k < tries; ++k)
				{
					const double h = m_heading + (k == 0 ? 0.0 : (m_walkRng.bi() * spread));
					const double nx = m_x + std::cos(h) * speed, ny = m_y + std::sin(h) * speed;
					if (nx * nx + ny * ny > 3.6) { continue; }
					const auto f = math::analyse(nx, ny, zoom, searchIter, 4);
					const double score = pull * (f.boundary + 0.5 * f.roughness + 0.3 * f.entropy - 0.6 * std::abs(f.insideFraction - 0.5))
						+ 0.25 * m_walkRng.uni() + (k == 0 ? 0.15 : 0.0);
					if (score > bestScore) { bestScore = score; bestH = h; }
				}
				m_heading = bestH + m_walkRng.bi() * 0.5 * spread * 0.25;
				m_x += std::cos(m_heading) * speed;
				m_y += std::sin(m_heading) * speed;
				// lost far outside: head back towards the set
				if (m_x * m_x + m_y * m_y > 3.6 || math::pixel(m_x, m_y, 60) < 0.05f)
				{
					m_heading = std::atan2(0.0 - m_y, -0.5 - m_x);
					m_x += (-0.5 - m_x) * 0.1;
					m_y += (0.0 - m_y) * 0.1;
				}
				break;
			}
			}
			// the box keeps the probe near the anchor (bounces off its walls)
			if (s.boxSize > 0.0f && s.probeMode != GlobalSettings::Orbit)
			{
				const double bx = s.boxSize;
				if (m_x < s.anchorX - bx || m_x > s.anchorX + bx) { m_x = clampv(m_x, s.anchorX - bx, s.anchorX + bx); m_heading = 3.14159265 - m_heading; }
				if (m_y < s.anchorY - bx || m_y > s.anchorY + bx) { m_y = clampv(m_y, s.anchorY - bx, s.anchorY + bx); m_heading = -m_heading; }
			}
		}
	}
}

void Conductor::buildNextPlan()
{
	{
		std::unique_lock<std::mutex> g(m_settingsLock, std::try_to_lock);
		if (g.owns_lock() && m_settingsChanged) { m_settings = m_pending; m_settingsChanged = false; }
	}
	if (m_probeRequest.exchange(false)) { m_x = m_reqX.load(); m_y = m_reqY.load(); }
	if (const auto r = m_reseed.exchange(0)) { m_sessionSeed = hash32(m_sessionSeed ^ r); m_walkRng = Rng(m_sessionSeed); m_heading = m_walkRng.uni() * 6.2831853; }

	const GlobalSettings& s = m_settings;
	auto next = std::make_unique<PhrasePlan>();
	PhrasePlan& p = *next;
	p.serial = m_serial++;
	// the old style presets are gone from the UI: the technical controls decide the character
	p.sustain = clampv(s.sustain, 0.0f, 1.0f);
	p.syncopation = clampv(s.syncopation, 0.0f, 1.0f);
	p.grid = s.grid;
	p.style = p.sustain >= 0.5f ? Style::Ambient : (p.syncopation >= 0.5f ? Style::Breakbeat : Style::IDM);
	p.complexity = s.complexity;
	p.swing = s.swing;
	p.novelty = s.novelty;
	p.noveltyMode = s.noveltyMode;
	p.fractalControl = s.fractalControl;
	const int defaultBars = p.sustain >= 0.5f ? 8 : 4;
	p.bars = clampv(s.phraseBars > 0 ? s.phraseBars : defaultBars, 1, MaxPhraseBars);
	p.sectionPhrase = static_cast<int>(p.serial % 4);
	p.seed = hash32(m_sessionSeed ^ hash32(static_cast<uint32_t>(p.serial) * 2654435761u));

	// walk the probe through the phrase, reading the set at every bar
	const double zoom = std::max(1e-9, static_cast<double>(s.zoom)) * (1.0 + clampv(s.breathe, 0.0f, 1.0f) * std::sin(p.serial * 0.37));
	for (int b = 0; b < p.bars; ++b)
	{
		walk(1);
		p.bar[static_cast<size_t>(b)].feat = math::analyse(m_x, m_y, zoom, clampv(s.maxIter, 60, 2000), clampv(s.readGrid, 4, 16));
		const int tp = trailPos.load();
		trailX[tp % 64].store(m_x); trailY[tp % 64].store(m_y);
		trailPos.store(tp + 1);
	}
	probeX.store(m_x); probeY.store(m_y);
	const Features& f0 = p.bar[0].feat;

	// section start: form pattern and key
	if (p.sectionPhrase == 0)
	{
		Rng rng(p.seed ^ 0x51ed);
		m_sectionForm = 0;
		if (rng.uni() < s.fractalControl)
		{
			const int sel = f0.period > 0 ? f0.period : static_cast<int>(f0.escape * 7.0 + f0.chaos * 3.0);
			m_sectionForm = sel % 4;
		}
		m_sectionKey.set(s.root, s.scale);
		if (s.fractalKey)
		{
			static const int ambientModes[] = {scales::Lydian, scales::Major, scales::Dorian, scales::PentatonicMajor, scales::Mixolydian, scales::PentatonicMinor};
			static const int darkModes[] = {scales::Dorian, scales::Aeolian, scales::Phrygian, scales::HarmonicMinor, scales::Lydian, scales::Mixolydian, scales::InSen, scales::PentatonicMinor};
			const bool amb = s.palette == 1;
			const int n = amb ? 6 : 8;
			const int mode = (amb ? ambientModes : darkModes)[clampv(static_cast<int>(f0.angle * n), 0, n - 1)];
			int root = s.root;
			if (f0.chaos > 0.62) { root += f0.boundary > 0.5 ? 7 : 5; } // modulate up a fifth / fourth in wild regions
			m_sectionKey.set(root, mode);
		}
		p.sectionFirstHistoryId = m_history.nextId();
	}
	else
	{
		p.sectionFirstHistoryId = m_plan ? m_plan->sectionFirstHistoryId : m_history.nextId();
	}
	p.key = m_sectionKey;
	p.form = formPatterns[m_sectionForm][p.sectionPhrase];
	p.formLetter = formLetters[p.form];
	static const float arc[] = {0.55f, 0.7f, 0.9f, 0.62f};
	p.energy = clampv(arc[p.sectionPhrase] + static_cast<float>(f0.chaos - 0.5) * 0.25f, 0.15f, 1.0f);

	planHarmony(p);

	// groove with anti-repetition
	{
		const float thr = threshold(s.novelty);
		float best = 2.0f;
		std::array<std::vector<DrumHit>, NumDrumLanes> bestDrums;
		int attempts = 0;
		for (int a = 0; a < 6; ++a)
		{
			++attempts;
			planDrums(p, a);
			PhraseRecord rec;
			rec.role = GrooveRole; rec.bars = p.bars; rec.root = 0;
			for (int l = 0; l < NumDrumLanes; ++l)
			{
				for (const auto& h : p.drums[static_cast<size_t>(l)]) { rec.notes.push_back({h.tick, 6, 36 + l, h.velocity}); }
			}
			PhraseHistory::fingerprint(rec);
			const float sim = rec.notes.empty() ? 0.0f
				: m_history.maxSimilarity(rec, s.noveltyMode, 128, p.sectionFirstHistoryId);
			if (sim < best) { best = sim; bestDrums = p.drums; }
			if (sim <= thr) { break; }
		}
		p.drums = bestDrums;
		p.drumSimilarity = best > 1.5f ? 0.0f : best;
		p.drumAttempts = attempts;
		storeDrumsInHistory(p);
	}

	// publish for the GUI
	currentEscape.store(static_cast<float>(f0.escape));
	currentChaos.store(static_cast<float>(f0.chaos));
	currentBoundary.store(static_cast<float>(f0.boundary));
	currentEntropy.store(static_cast<float>(f0.entropy));
	currentRoughness.store(static_cast<float>(f0.roughness));
	currentPeriod.store(f0.period);
	currentForm.store(p.form);
	currentKeyRoot.store(p.key.root);
	currentScale.store(p.key.kind);
	currentDrumSimilarity.store(p.drumSimilarity);
	currentBars.store(p.bars);
	m_plan = std::move(next);
	phraseCounter.store(m_plan->serial + 1);
}

void Conductor::planHarmony(PhrasePlan& p)
{
	const GlobalSettings& s = m_settings;
	const bool ambient = p.style == Style::Ambient;
	const int n = std::max(1, p.key.numDegs);
	for (int b = 0; b < p.bars; ++b)
	{
		auto& bi = p.bar[static_cast<size_t>(b)];
		const Features& f = bi.feat;
		int hr = 2;
		if (s.chordSpeed > 0) { hr = s.chordSpeed; }
		else if (ambient) { hr = (f.entropy < 0.45 ? 4 : 2); }
		else if (p.style == Style::Breakbeat) { hr = 2; }
		else { hr = (f.entropy > 0.55 || p.complexity > 0.7f) ? 1 : 2; }
		if (s.chordSpeed == 0 && p.fractalControl > 0.7f && f.period > 0) { hr = clampv(f.period % 4 + 1, 1, 4); }
		const int globalBar = static_cast<int>(p.serial % 1024) * p.bars + b;
		const bool change = (b == 0 && p.sectionPhrase == 0) || (globalBar % hr == 0) || m_prevChord < 0;
		bi.chordChange = change;
		bi.chordBars = hr;
		if (!change)
		{
			bi.chordRoot = p.bar[static_cast<size_t>(std::max(0, b - 1))].chordRoot;
			if (b == 0 && m_plan) { bi.chordRoot = m_plan->bar[static_cast<size_t>(m_plan->bars - 1)].chordRoot; }
		}
		else
		{
			static const int consonant[] = {0, 5, 3, 4};
			static const int colourful[] = {0, 5, 3, 4, 1, 2, 6};
			const bool simple = ambient || (f.insideFraction > 0.5 && f.entropy < 0.5 && p.complexity < 0.6f);
			const int* pool = simple ? consonant : colourful;
			const int poolSize = simple ? 4 : 7;
			int pick = clampv(static_cast<int>(orbitA(f, globalBar * 5 + 3) * poolSize), 0, poolSize - 1);
			int r = pool[pick];
			const bool sectionEnd = p.sectionPhrase == 3 && b + hr >= p.bars;
			if (p.sectionPhrase == 0 && b == 0) { r = 0; }      // sections start at home
			else if (sectionEnd && !ambient) { r = 4; }          // ...and end on the dominant
			else if (r == m_prevChord) { r = pool[(pick + 1) % poolSize]; }
			if (n < 5) { r = r % n; }
			bi.chordRoot = r;
			m_prevChord = r;
		}
		bi.chordSize = ambient ? (p.complexity > 0.6f ? 5 : 4) : ((f.entropy > 0.45 || p.complexity > 0.6f) ? 4 : 3);
		for (int k = 0; k < 5; ++k) { bi.chordDegs[static_cast<size_t>(k)] = bi.chordRoot + 2 * k; }
	}
}

void Conductor::planDrums(PhrasePlan& p, int attempt)
{
	for (auto& l : p.drums) { l.clear(); }
	const bool ambient = p.style == Style::Ambient;
	const bool breaks = p.style == Style::Breakbeat;
	// the groove belongs to the section (A phrases share it); variations add fills / changes
	const uint32_t sectionSeed = hash32(m_sessionSeed ^ static_cast<uint32_t>(p.serial / 4) * 0x9e3779b9u ^ static_cast<uint32_t>(attempt) * 7919u);
	auto add = [&](int lane, int tick, int vel) { p.drums[static_cast<size_t>(lane)].push_back({tick, clampv(vel, 1, 127)}); };
	for (int b = 0; b < p.bars; ++b)
	{
		const Features& f = p.bar[static_cast<size_t>(b)].feat;
		Rng rng(hash32(sectionSeed ^ static_cast<uint32_t>(b % 2) * 104729u));     // 2-bar groove cell
		Rng fx(hash32(p.seed ^ static_cast<uint32_t>(b) * 31337u ^ static_cast<uint32_t>(attempt)));  // per-bar variation
		const float dens = clampv(0.35f + p.energy * 0.5f, 0.0f, 1.0f);
		const float cx = clampv(static_cast<float>(f.chaos) * (0.5f + p.complexity), 0.0f, 1.0f);
		const bool lastBar = b == p.bars - 1;
		const bool fill = lastBar && (p.form != FormA || p.sectionPhrase == 3);
		// fractal-chosen rhythmic cell: pulses / rotation from the period and the orbit
		const int kickPulses = 3 + (f.period > 0 ? f.period % 3 : static_cast<int>(f.escape * 2.99))
			+ (p.form == FormB || p.form == FormC ? 1 : 0);
		const int kickRot = static_cast<int>(orbitA(f, static_cast<int>(p.serial / 4)) * 4.0f * p.fractalControl);
		for (int s = 0; s < 16; ++s)
		{
			const int t = b * TicksPerBar + s * TicksPer16th;
			const float rad = orbitR(f, s + b * 16);
			if (ambient)
			{
				if (s == 0 && (b % 2) == 0 && dens > 0.3f) { add(LKick, t, 56); }
				if ((s % 2) == 0 && rng.uni() < 0.10f + 0.4f * dens) { add(LShaker, t, 28 + static_cast<int>(rng.uni() * 30.0f)); }
				if (euclid(3, 16, s + b % 5) && rng.uni() < 0.35f * dens) { add(LPerc, t, 38 + static_cast<int>(rad * 25.0f)); }
				if (s == 10 && fx.uni() < 0.25f * dens) { add(LRim, t, 42); }
				if (s == 0 && b % 4 == 0 && fx.uni() < 0.5f) { add(LCym, t, 38); }
				continue;
			}
			const bool kick = s == 0 || (euclid(kickPulses, 16, s - kickRot) && rng.uni() < 0.55f + 0.45f * dens);
			if (kick) { add(LKick, t, s == 0 ? 120 : 98); }
			if (breaks) { if (s == 10 && rng.uni() < 0.6f) { add(LKick, t, 90); } continue; }
			if (s == 4 || s == 12) { add(LSnare, t, 112); }
			else if ((s & 1) && rng.uni() < 0.2f * cx * dens) { add(LSnare, t, 36); }
			const bool hats16 = dens > 0.72f || (f.roughness > 0.5 && dens > 0.5f);
			if ((s % 2 == 0 && dens > 0.25f) || (hats16 && rng.uni() < 0.85f))
			{
				add(LHatC, t, (s % 4 == 2 ? 92 : 64) + static_cast<int>(rng.uni() * 18.0f));
				// IDM ratchets: 32nd rolls where the set is rough
				if (f.roughness > 0.45 && fx.uni() < 0.25f * cx) { add(LHatC, t + 6, 50 + static_cast<int>(fx.uni() * 30.0f)); }
			}
			if (s == 14 && rng.uni() < 0.55f * dens) { add(LHatO, t, 80); }
			if (s == 12 && (b & 1) && rng.uni() < 0.6f) { add(LClap, t, 96); }
			if (euclid(5, 16, s + b % 3) && rng.uni() < 0.15f + 0.6f * static_cast<float>(f.roughness) * dens) { add(LRim, t, 74); }
			if (fx.uni() < 0.10f * dens * static_cast<float>(f.boundary + cx)) { add(LPerc, t, 66 + static_cast<int>(rad * 40.0f)); }
			if (fill && s >= 12)
			{
				if (fx.uni() < 0.5f * (0.3f + cx)) { add(s % 2 ? LTomH : LTomL, t, 92); }
				if (s % 2 == 1 && fx.uni() < 0.5f) { add(LSnare, t, 70 + s * 2); }
			}
		}
		if (!ambient && b == 0 && p.sectionPhrase == 0) { add(LCym, 0, 100); }
	}
	for (auto& l : p.drums) { std::sort(l.begin(), l.end(), [](const DrumHit& a, const DrumHit& b) { return a.tick < b.tick; }); }
}

void Conductor::storeDrumsInHistory(PhrasePlan& p)
{
	PhraseRecord rec;
	rec.role = GrooveRole;
	rec.style = static_cast<int>(p.style);
	rec.bars = p.bars;
	rec.cx = p.feat().cx; rec.cy = p.feat().cy;
	rec.time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	rec.track = "groove";
	for (int l = 0; l < NumDrumLanes; ++l)
	{
		for (const auto& h : p.drums[static_cast<size_t>(l)]) { rec.notes.push_back({h.tick, 6, 36 + l, h.velocity}); }
	}
	if (!rec.notes.empty()) { m_history.add(std::move(rec)); }
}

// =================================================================== PartGenerator
int PartGenerator::naturalOctave(Role r)
{
	switch (r)
	{
	case Role::Bass: return 2;
	case Role::Drone: return 2;
	case Role::Chords: return 4;
	case Role::Pad: return 3;
	case Role::Lead: return 5;
	case Role::Arp: return 5;
	case Role::Bells: return 6;
	case Role::Texture: return 4;
	default: return 4;
	}
}

const std::vector<GenNote>& PartGenerator::build(const PhrasePlan& plan, const PartSettings& ps, PhraseHistory& history,
	const std::string& trackName)
{
	m_builtSerial = plan.serial;
	if (ps.role == Role::Off) { m_notes.clear(); return m_notes; }
	const float thr = threshold(plan.novelty);
	std::vector<GenNote> best, cand;
	float bestSim = 2.0f;
	m_attempts = 0;
	const auto motifBackup = m_motif;
	const bool haveMotifBackup = m_haveMotif;
	auto bestMotif = m_motif;
	bool bestHaveMotif = m_haveMotif;
	for (int a = 0; a < 6; ++a)
	{
		++m_attempts;
		m_motif = motifBackup; m_haveMotif = haveMotifBackup;
		cand.clear();
		generate(plan, ps, a, cand);
		PhraseRecord rec;
		rec.role = static_cast<int>(ps.role);
		rec.root = plan.key.root;
		rec.bars = plan.bars;
		rec.notes = cand;
		PhraseHistory::fingerprint(rec);
		// drums follow the shared groove (already checked); everything else is checked here
		const float sim = (isDrumRole(ps.role) || cand.empty()) ? 0.0f
			: history.maxSimilarity(rec, plan.noveltyMode, 256,
				ps.role == Role::Lead ? plan.sectionFirstHistoryId : UINT64_MAX); // the lead may develop its own motif
		if (sim < bestSim) { bestSim = sim; best = cand; bestMotif = m_motif; bestHaveMotif = m_haveMotif; }
		if (sim <= thr || isDrumRole(ps.role)) { break; }
	}
	m_motif = bestMotif; m_haveMotif = bestHaveMotif;
	// never play exactly something that was played before: mutate (in key) until it is new
	if (!isDrumRole(ps.role) && !best.empty())
	{
		Rng mr(hash32(plan.seed ^ 0xbadc0deu ^ static_cast<uint32_t>(ps.role)));
		const int oct = naturalOctave(ps.role) + ps.octave;
		for (int guard = 0; guard < 24; ++guard)
		{
			PhraseRecord rec;
			rec.role = static_cast<int>(ps.role); rec.root = plan.key.root; rec.bars = plan.bars; rec.notes = best;
			PhraseHistory::fingerprint(rec);
			if (!history.containsExact(rec, plan.noveltyMode)) { break; }
			auto& nn = best[static_cast<size_t>(mr.below(static_cast<int>(best.size())))];
			if (ps.role == Role::Break || mr.uni() < 0.4f)
			{
				// move it by a 16th (stays inside the phrase)
				nn.tick = clampv(nn.tick + (mr.uni() < 0.5f ? -TicksPer16th : TicksPer16th), 0, plan.bars * TicksPerBar - 1);
			}
			else
			{
				nn.pitch = plan.key.pitch(plan.key.degreeOf(nn.pitch, oct) + (mr.uni() < 0.5f ? -1 : 1), oct);
			}
		}
	}
	m_notes = std::move(best);
	std::sort(m_notes.begin(), m_notes.end(), [](const GenNote& x, const GenNote& y) { return x.tick < y.tick; });
	m_lastSimilarity = bestSim > 1.5f ? 0.0f : bestSim;
	if (!m_notes.empty() && !isDrumRole(ps.role))
	{
		PhraseRecord rec;
		rec.role = static_cast<int>(ps.role);
		rec.style = static_cast<int>(plan.style);
		rec.root = plan.key.root;
		rec.bars = plan.bars;
		rec.cx = plan.feat().cx; rec.cy = plan.feat().cy;
		rec.time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		rec.track = trackName;
		rec.notes = m_notes;
		history.add(std::move(rec));
	}
	return m_notes;
}

void PartGenerator::generate(const PhrasePlan& p, const PartSettings& ps, int attempt, std::vector<GenNote>& out)
{
	Rng rng(hash32(p.seed ^ (static_cast<uint32_t>(ps.role) * 104729u) ^ (static_cast<uint32_t>(ps.channelSeed) * 7919u)
		^ (static_cast<uint32_t>(attempt) * 0x2545f491u)));
	switch (ps.role)
	{
	case Role::Bass: genBass(p, ps, rng, out); break;
	case Role::Chords: genChords(p, ps, rng, out, p.style == Style::Ambient); break;
	case Role::Pad: genChords(p, ps, rng, out, true); break;
	case Role::Lead: genLead(p, ps, attempt, out); break;
	case Role::Arp: genArp(p, ps, rng, out); break;
	case Role::Bells: genBells(p, ps, rng, out); break;
	case Role::Texture: genTexture(p, ps, rng, out); break;
	case Role::Drone: genDrone(p, ps, rng, out); break;
	case Role::Break: genBreak(p, ps, rng, out); break;
	default: if (isDrumRole(ps.role)) { genDrum(p, ps, out); } break;
	}
	// swing (off 16ths later), velocity + length scaling, clamp
	const int swingTicks = static_cast<int>(std::lround(clampv(p.swing, 0.0f, 0.6f) * 8.0f));
	for (auto& n : out)
	{
		if (swingTicks > 0 && n.tick % 24 == 12) { n.tick += swingTicks; }
		n.velocity = clampv(static_cast<int>(std::lround(n.velocity * ps.velocity)), 1, 127);
		n.length = std::max(2, static_cast<int>(std::lround(n.length * clampv(ps.gate, 0.1f, 4.0f))));
		n.pitch = clampv(n.pitch, 0, 127);
		n.tick = clampv(n.tick, 0, p.bars * TicksPerBar - 1);
	}
}

void PartGenerator::genDrum(const PhrasePlan& p, const PartSettings& ps, std::vector<GenNote>& out)
{
	const int lane = static_cast<int>(ps.role) - static_cast<int>(Role::Kick);
	if (lane < 0 || lane >= NumDrumLanes) { return; }
	Rng rng(hash32(p.seed ^ static_cast<uint32_t>(lane) * 977u ^ static_cast<uint32_t>(ps.channelSeed)));
	const int len = (ps.role == Role::Cymbal || ps.role == Role::HatOpen) ? 96 : 48;
	for (const auto& h : p.drums[static_cast<size_t>(lane)])
	{
		const bool strong = h.tick % 48 == 0;
		if (ps.density < 0.5f && !strong && rng.uni() > ps.density * 2.0f) { continue; } // thin out
		out.push_back({h.tick, len, ps.baseKey, h.velocity});
	}
	// above 0.5: ghost notes between the groove's hits
	if (ps.density > 0.5f && ps.role != Role::Kick && ps.role != Role::Cymbal)
	{
		for (int t = 0; t < p.bars * TicksPerBar; t += TicksPer16th)
		{
			if (rng.uni() < (ps.density - 0.5f) * 0.3f) { out.push_back({t, len, ps.baseKey, 30 + rng.below(30)}); }
		}
	}
}

void PartGenerator::genBass(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	const int oct = naturalOctave(Role::Bass) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	if (p.style == Style::Ambient)
	{
		for (int b = 0; b < p.bars; ++b)
		{
			const auto& bi = p.bar[static_cast<size_t>(b)];
			if (!bi.chordChange && b != 0) { continue; }
			int len = 0;
			for (int k = b; k < p.bars && (k == b || !p.bar[static_cast<size_t>(k)].chordChange); ++k) { len += TicksPerBar; }
			const int lowOct = rng.uni() < 0.3f ? oct - 1 : oct;
			out.push_back({b * TicksPerBar, len - 4, p.key.pitch(bi.chordRoot, lowOct), 68 + static_cast<int>(orbitR(bi.feat, b) * 18.0f) + rng.below(8)});
			if (ps.density > 0.55f || rng.uni() < 0.35f)
			{
				const int t2 = 48 * (1 + rng.below(3));
				out.push_back({b * TicksPerBar + t2, std::max(24, len - t2 - 4), p.key.pitch(bi.chordRoot + (rng.uni() < 0.5f ? 4 : n), oct), 46 + rng.below(10)});
			}
		}
		return;
	}
	for (int b = 0; b < p.bars; ++b)
	{
		const auto& bi = p.bar[static_cast<size_t>(b)];
		const Features& f = bi.feat;
		const float legato = 1.0f - static_cast<float>(f.chaos) * 0.8f;
		const int bp = 2 + static_cast<int>(clampv(ps.density, 0.0f, 1.0f) * 5.0f) + (p.form == 2 ? 1 : 0);
		const int rot = static_cast<int>(orbitA(f, b) * 3.0f * p.fractalControl);
		for (int s = 0; s < 16; ++s)
		{
			const bool on = s == 0 || (euclid(bp, 16, s + 3 + rot) && rng.uni() < 0.85f);
			if (!on) { continue; }
			// in breakbeat the bass lands on the break's kicks
			if (p.syncopation > 0.0f && s != 0 && !(s == 10 || s == 7 || s == 14) && rng.uni() < 0.6f * p.syncopation * 2.0f) { continue; }
			int deg = bi.chordRoot;
			const float ang = orbitA(f, s + b * 16);
			const float r = rng.uni();
			if (s != 0 && r < 0.22f) { deg += 4; }
			else if (s != 0 && ang < 0.12f) { deg += n; }
			else if (s == 15 && r < 0.5f) { deg += (rng.uni() < 0.5f ? 1 : -1); } // approach note into the next bar
			const int len = static_cast<int>(TicksPer16th * clampv(1.2f + legato * 1.8f, 0.6f, 3.5f));
			out.push_back({b * TicksPerBar + s * TicksPer16th, len, p.key.pitch(deg, oct), 90 + static_cast<int>(orbitR(f, s) * 30.0f)});
		}
	}
}

void PartGenerator::genChords(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out, bool pad)
{
	const int oct = naturalOctave(pad ? Role::Pad : Role::Chords) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	for (int b = 0; b < p.bars; ++b)
	{
		const auto& bi = p.bar[static_cast<size_t>(b)];
		const float rad = orbitR(bi.feat, b);
		if (pad)
		{
			if (!bi.chordChange && b != 0) { continue; }
			int len = 0;
			for (int k = b; k < p.bars && (k == b || !p.bar[static_cast<size_t>(k)].chordChange); ++k) { len += TicksPerBar; }
			// open voicing: root, fifth, octave, tenth (+ ninth when dense) - overlaps into the next chord
			const int r = bi.chordRoot;
			const int voicings[3][5] = {
				{r, r + 4, r + n, r + n + 2, r + n + 1},        // open: root fifth octave tenth ninth
				{r, r + 2, r + 6, r + n + 1, r + n + 4},        // spread: root third seventh ninth twelfth
				{r - n + 4, r, r + 4, r + n + 1, r + n + 2},    // low fifth, root, fifth, ninth, tenth
			};
			const int v = clampv(static_cast<int>(orbitA(bi.feat, b + static_cast<int>(p.serial) * 3) * 3.0f + rng.uni()) % 3, 0, 2);
			const int count = ps.density > 0.6f ? 5 : 4;
			for (int k = 0; k < count; ++k)
			{
				// voices enter one after another (a slow strum), the set sets the spacing
				const int enter = k * (2 + rng.below(10));
				out.push_back({b * TicksPerBar + enter, len + 24 - enter, p.key.pitch(voicings[v][k], oct), 46 + static_cast<int>(rad * 14.0f) + rng.below(8)});
			}
			continue;
		}
		// block chords with voice leading: each chord in the inversion nearest the previous one
		// (after synkopa chords.rs voice())
		std::vector<int> pcs;
		for (int k = 0; k < bi.chordSize; ++k) { pcs.push_back(p.key.pitch(bi.chordDegs[static_cast<size_t>(k)], oct)); }
		std::vector<int> voiced = pcs;
		if (!m_lastVoicing.empty())
		{
			int prevCentre = 0;
			for (int x : m_lastVoicing) { prevCentre += x; }
			prevCentre /= static_cast<int>(m_lastVoicing.size());
			int bestCost = 1 << 30;
			for (int inv = 0; inv < static_cast<int>(pcs.size()); ++inv)
			{
				std::vector<int> v;
				for (size_t k = 0; k < pcs.size(); ++k)
				{
					int x = pcs[(k + inv) % pcs.size()];
					if (k + inv >= pcs.size()) { x += 12; }
					v.push_back(x);
				}
				for (int shift : {-12, 0, 12})
				{
					int cost = 0, centre = 0;
					for (int x : v) { centre += x + shift; }
					centre /= static_cast<int>(v.size());
					cost = std::abs(centre - prevCentre) * 4;
					for (int x : v) { int d = 99; for (int y : m_lastVoicing) { d = std::min(d, std::abs(x + shift - y)); } cost += d; }
					if (cost < bestCost) { bestCost = cost; voiced = v; for (auto& x : voiced) { x += shift; } }
				}
			}
		}
		m_lastVoicing = voiced;
		// comping rhythm: a pattern per phrase chosen by the set (roughness, period) and the variation,
		// shifted bar by bar by the orbit. {step, length in 16ths}
		static const int patterns[8][5][2] = {
			{{0, 16}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}},
			{{0, 8}, {8, 8}, {-1, 0}, {-1, 0}, {-1, 0}},
			{{0, 6}, {6, 2}, {10, 6}, {-1, 0}, {-1, 0}},
			{{0, 3}, {3, 3}, {10, 6}, {-1, 0}, {-1, 0}},
			{{2, 3}, {6, 3}, {10, 3}, {14, 2}, {-1, 0}},
			{{0, 12}, {12, 4}, {-1, 0}, {-1, 0}, {-1, 0}},
			{{0, 2}, {3, 2}, {6, 2}, {11, 5}, {-1, 0}},
			{{0, 4}, {7, 5}, {12, 2}, {14, 2}, {-1, 0}},
		};
		const float dens = clampv(ps.density, 0.0f, 1.0f);
		const int maxPat = dens < 0.3f ? 2 : (dens < 0.6f ? 5 : 8);
		const int patBase = static_cast<int>(bi.feat.roughness * 3.0 + (bi.feat.period > 0 ? bi.feat.period : 0)) + rng.below(maxPat);
		const int pat = ((b % 2 == 1 && rng.uni() < 0.4f) ? patBase + 1 : patBase) % maxPat;
		const int vel = 58 + static_cast<int>(rad * 22.0f);
		const bool drop = rng.uni() < 0.3f; // sometimes drop the top voice, sometimes double the bass an octave down
		for (int k = 0; k < 5 && patterns[pat][k][0] >= 0; ++k)
		{
			const int st = patterns[pat][k][0], ln = patterns[pat][k][1];
			const int acc = st == 0 ? 10 : (st % 4 == 0 ? 4 : 0);
			for (size_t v = 0; v < voiced.size(); ++v)
			{
				if (drop && v + 1 == voiced.size() && k > 0) { continue; }
				out.push_back({b * TicksPerBar + st * TicksPer16th, ln * TicksPer16th - 4, voiced[v], vel + acc + rng.below(6)});
			}
			if (!drop && k == 0 && rng.uni() < 0.35f) { out.push_back({b * TicksPerBar, ln * TicksPer16th - 4, voiced[0] - 12, vel}); }
		}
		(void) n;
	}
}

void PartGenerator::genLead(const PhrasePlan& p, const PartSettings& ps, int attempt, std::vector<GenNote>& out)
{
	const bool ambient = p.style == Style::Ambient;
	const int oct = naturalOctave(Role::Lead) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	const int len = p.bars * 16;   // 16ths
	const float dens = clampv(ps.density, 0.0f, 1.0f);
	Rng rng(hash32(p.seed ^ 0x1eadu ^ static_cast<uint32_t>(ps.channelSeed) * 131u ^ static_cast<uint32_t>(attempt) * 0x9e37u));
	auto chordAt = [&](int step) -> const BarInfo& { return p.bar[static_cast<size_t>(clampv(step / 16, 0, p.bars - 1))]; };
	auto chordToneNear = [&](int step, int degree)
	{
		const auto& bi = chordAt(step);
		int best = degree, bestDist = 1000;
		for (int o = -3; o <= 3; ++o)
		{
			for (int k = 0; k < std::min(bi.chordSize, 4); ++k)
			{
				const int c = bi.chordDegs[static_cast<size_t>(k)] + o * n;
				if (std::abs(c - degree) < bestDist) { bestDist = std::abs(c - degree); best = c; }
			}
		}
		return best;
	};
	auto makeRhythm = [&](std::vector<MotifNote>& m, int salt)
	{
		m.clear();
		Rng r2(hash32(p.seed ^ static_cast<uint32_t>(salt)));
		// the rhythm cell repeats every 2 bars (4 bars in ambient) inside the phrase
		const int cell = std::min(len, ambient ? 64 : 32);
		int onsets = ambient ? 2 + static_cast<int>(dens * 3.0f) : 3 + static_cast<int>(dens * 7.0f + p.complexity * 2.0f);
		onsets = clampv(onsets + attempt % 2, 2, cell / 2);
		const Features& f = p.feat();
		const int rot = (f.period > 0 ? f.period : static_cast<int>(f.escape * 5.0)) % 3 + (attempt / 2);
		std::vector<bool> on(static_cast<size_t>(cell), false);
		for (int s = 0; s < cell; ++s) { on[static_cast<size_t>(s)] = euclid(onsets, cell, s - rot); }
		on[0] = true;
		if (!ambient) { for (int s = cell - 3; s < cell; ++s) { on[static_cast<size_t>(s)] = false; } } // breathe
		// a little fractal syncopation: move one onset by a 16th where the orbit is rough
		if (f.roughness > 0.4 && r2.uni() < p.fractalControl)
		{
			const int s = 1 + r2.below(cell - 2);
			if (on[static_cast<size_t>(s)] && !on[static_cast<size_t>(s + 1)]) { on[static_cast<size_t>(s)] = false; on[static_cast<size_t>(s + 1)] = true; }
		}
		for (int s = 0; s < cell; ++s)
		{
			if (!on[static_cast<size_t>(s)]) { continue; }
			int d = 1;
			while (s + d < cell && !on[static_cast<size_t>(s + d)]) { ++d; }
			MotifNote mn;
			mn.step = s;
			mn.len = clampv(d, 1, ambient ? 24 : 6);
			mn.vel = s % 16 == 0 ? 100 : (s % 4 == 0 ? 90 : 78);
			m.push_back(mn);
		}
	};
	auto makeContour = [&](std::vector<MotifNote>& m, int startDeg, int salt)
	{
		const Features& f = p.feat();
		int deg = startDeg, lastLeap = 0, j = static_cast<int>(p.serial * 13 + salt + attempt * 7);
		for (size_t i = 0; i < m.size(); ++i)
		{
			float d = orbitA(f, j + 1) - orbitA(f, j);
			++j;
			if (d > 0.5f) { d -= 1.0f; } else if (d < -0.5f) { d += 1.0f; }
			const float mag = std::abs(d);
			int move = mag < 0.08f ? 0 : (mag < 0.22f ? 1 : (mag < 0.38f ? 2 : 4));
			if (ambient) { move = std::min(move, 2); }
			if (std::abs(lastLeap) >= 3) { move = 1; d = lastLeap > 0 ? -1.0f : 1.0f; } // after a leap, step back
			const int step = d >= 0 ? move : -move;
			if (i != 0) { deg += step; }
			lastLeap = step;
			if (m[i].step % 8 == 0) { deg = chordToneNear(m[i].step, deg); } // chord tones on strong beats
			deg = clampv(deg, -2, n + 5);                                    // singable range
			m[i].degree = deg;
			m[i].vel += static_cast<int>(orbitR(f, j) * 18.0f);
		}
	};
	std::vector<MotifNote> phrase;
	const int startDeg = chordToneNear(0, p.bar[0].chordRoot + 2);
	switch (p.form)
	{
	case FormA:
	case FormC:
		makeRhythm(m_motif, static_cast<int>(p.serial) * 7 + attempt);
		makeContour(m_motif, p.form == FormC ? chordToneNear(0, p.bar[0].chordRoot + 4) : startDeg, 0);
		m_haveMotif = true;
		phrase = m_motif;
		break;
	case FormB:
	case FormB1:
		// contrast: new rhythm and a higher contour (the motif is kept for the return)
		makeRhythm(phrase, static_cast<int>(p.serial) * 11 + 3 + attempt);
		makeContour(phrase, chordToneNear(0, p.bar[0].chordRoot + 4), 41);
		break;
	default: // A' / A'': the motif's rhythm, the second half answers differently
	{
		if (!m_haveMotif) { makeRhythm(m_motif, static_cast<int>(p.serial) * 7); makeContour(m_motif, startDeg, 0); m_haveMotif = true; }
		phrase = m_motif;
		std::vector<MotifNote> alt = m_motif;
		const float var = clampv(ps.variation, 0.0f, 1.0f);
		makeContour(alt, m_motif.empty() ? startDeg : m_motif[0].degree + (p.form == FormA1 ? 1 : -1), 17 + p.form + attempt);
		const size_t from = static_cast<size_t>(static_cast<float>(phrase.size()) * (1.0f - 0.5f * (0.5f + var)));
		bool changed = false;
		for (size_t i = from; i < phrase.size(); ++i) { changed |= phrase[i].degree != alt[i].degree; phrase[i].degree = alt[i].degree; }
		if (!changed && !phrase.empty()) { phrase.back().degree += (p.form == FormA1 ? 2 : -2); } // a variation always varies
		break;
	}
	}
	// the cell repeats through the phrase; later repeats develop (transposed by the chord)
	const int cell = std::min(len, ambient ? 64 : 32);
	for (int rep = 0; rep * cell < len; ++rep)
	{
		for (size_t i = 0; i < phrase.size(); ++i)
		{
			const int step = rep * cell + phrase[i].step;
			if (step >= len) { break; }
			int deg = phrase[i].degree;
			if (rep > 0)
			{
				deg += (chordAt(step).chordRoot - chordAt(phrase[i].step).chordRoot); // follow the harmony
				if (rep % 2 == 1 && i + 1 == phrase.size()) { deg += 1; }
			}
			if (phrase[i].step % 8 == 0) { deg = chordToneNear(step, deg); }
			deg = clampv(deg, -2, n + 6);
			const bool lastOfPhrase = rep * cell + cell >= len && i + 1 == phrase.size();
			int l = phrase[i].len * TicksPer16th;
			if (lastOfPhrase && (p.form == FormA2 || p.sectionPhrase == 3))
			{
				deg = chordToneNear(step, chordAt(step).chordRoot); // resolve home and ring
				l = std::max(l, ambient ? 16 * TicksPer16th : 4 * TicksPer16th);
			}
			l = static_cast<int>(l * (ambient ? 1.0f : 0.92f));
			const int vel = clampv(phrase[i].vel - (ambient ? 25 : 0) + static_cast<int>(p.energy * 10.0f) - 5, 30, 127);
			out.push_back({step * TicksPer16th, l, p.key.pitch(deg, oct), vel});
		}
	}
	(void) rng;
}

void PartGenerator::genArp(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	const int oct = naturalOctave(Role::Arp) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	const bool ambient = p.style == Style::Ambient;
	const Features& f0 = p.feat();
	// rate: 16ths; odd periods give triplets (the set decides)
	const bool triplets = p.grid == 2 || (p.grid == 0 && !ambient && f0.period >= 3 && (f0.period % 2) == 1 && p.fractalControl > 0.3f);
	const int stepTicks = ambient ? 24 : (triplets ? 16 : TicksPer16th);
	const int total = p.bars * TicksPerBar;
	for (int t = 0, k = 0; t < total; t += stepTicks, ++k)
	{
		const auto& bi = p.barAt(t);
		if (rng.uni() > (ambient ? 0.25f + 0.5f * ps.density : 0.45f + 0.55f * ps.density)) { continue; }
		const int L = bi.chordSize * 2;
		const int mode = static_cast<int>((t / TicksPerBar + f0.period) % 3);
		int idx;
		if (mode == 0) { idx = m_arpIndex % L; }
		else if (mode == 1) { idx = L - 1 - (m_arpIndex % L); }
		else { idx = clampv(static_cast<int>(orbitA(bi.feat, k) * L), 0, L - 1); }
		++m_arpIndex;
		const int deg = bi.chordDegs[static_cast<size_t>(idx % bi.chordSize)] + n * (idx / bi.chordSize);
		out.push_back({t, static_cast<int>(stepTicks * (ambient ? 1.6f : 0.7f)), p.key.pitch(deg, oct),
			(ambient ? 40 : 56) + static_cast<int>(orbitR(bi.feat, k) * 34.0f)});
	}
}

void PartGenerator::genBells(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	const int oct = naturalOctave(Role::Bells) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	for (int t = 0; t < p.bars * TicksPerBar; t += 24)
	{
		const auto& bi = p.barAt(t);
		if (rng.uni() > 0.04f + 0.18f * ps.density) { continue; }
		const int k = rng.below(std::min(bi.chordSize, 4));
		const int deg = bi.chordDegs[static_cast<size_t>(k)] + (rng.uni() < 0.3f ? n : 0);
		out.push_back({t, 96 + rng.below(96), p.key.pitch(deg, oct), 36 + rng.below(40)});
	}
}

void PartGenerator::genTexture(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	// stochastic grain cloud (after cecilia5 Synthesis/StochGrains): sparse soft notes from the chord,
	// scattered over three octaves, random length and loudness, density follows the set's entropy
	const int oct = naturalOctave(Role::Texture) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	for (int t = 0; t < p.bars * TicksPerBar; t += 6)
	{
		const auto& bi = p.barAt(t);
		const float chance = 0.02f + 0.16f * ps.density * static_cast<float>(0.5 + bi.feat.entropy);
		if (rng.uni() > chance) { continue; }
		const int k = rng.below(bi.chordSize);
		const int octave = static_cast<int>(orbitA(bi.feat, t / 6) * 3.0f);
		const int deg = bi.chordDegs[static_cast<size_t>(k)] + n * octave + (rng.uni() < 0.15f ? 1 : 0);
		out.push_back({t, 12 + rng.below(84), p.key.pitch(deg, oct - 1), 22 + rng.below(40)});
	}
}

void PartGenerator::genDrone(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	const int oct = naturalOctave(Role::Drone) + ps.octave;
	const int n = std::max(1, p.key.numDegs);
	for (int b = 0; b < p.bars; ++b)
	{
		const auto& bi = p.bar[static_cast<size_t>(b)];
		if (!bi.chordChange && b != 0) { continue; }
		int len = 0;
		for (int k = b; k < p.bars && (k == b || !p.bar[static_cast<size_t>(k)].chordChange); ++k) { len += TicksPerBar; }
		// the drone keeps the key's root under everything; the upper voice moves with the chord and
		// the set: fifth, fourth, ninth or octave, entering late or swelling in again
		const int upper[] = {bi.chordRoot + 4, bi.chordRoot + 3, bi.chordRoot + 8, n, bi.chordRoot + 2};
		const int pick = clampv(static_cast<int>(orbitA(bi.feat, b + static_cast<int>(p.serial)) * 5.0f + rng.uni() * 2.0f) % 5, 0, 4);
		const int enter = rng.below(4) * 24;
		out.push_back({b * TicksPerBar, len + 48, p.key.pitch(0, oct), 64 + rng.below(12)});
		out.push_back({b * TicksPerBar + 8 + enter, std::max(48, len + 40 - enter), p.key.pitch(upper[pick], oct), 50 + rng.below(16)});
		if (ps.density > 0.6f || rng.uni() < 0.3f)
		{
			const int t2 = b * TicksPerBar + 96 + rng.below(4) * 24;
			out.push_back({t2, std::max(48, len - (t2 - b * TicksPerBar)), p.key.pitch(upper[(pick + 2) % 5] + n, oct), 40 + rng.below(14)});
		}
		(void) rng;
	}
}

void PartGenerator::genBreak(const PhrasePlan& p, const PartSettings& ps, Rng& rng, std::vector<GenNote>& out)
{
	// re-chop the break's own slices (SlicerT: base key = whole break, base+1+i = slice i)
	std::vector<float> slices = ps.sliceBeats;
	float breakBeats = std::max(0.5f, ps.breakBeats);
	if (slices.empty()) { for (int i = 0; i < 16; ++i) { slices.push_back(i * 0.5f); } breakBeats = 8.0f; }
	const int numSlices = static_cast<int>(slices.size());
	const float dens = clampv(ps.density, 0.0f, 1.0f);
	int last = 0;
	for (int t = 0; t < p.bars * TicksPerBar; t += 6) // 32nds
	{
		const auto& bi = p.barAt(t);
		const Features& f = bi.feat;
		const int bar = t / TicksPerBar;
		const int s32 = (t % TicksPerBar) / 6;
		const float beat = t / 48.0f;
		const float posInBreak = std::fmod(beat, breakBeats);
		int orig = -1;
		for (int i = 0; i < numSlices; ++i) { if (std::abs(slices[static_cast<size_t>(i)] - posInBreak) < 0.0625f) { orig = i; break; } }
		const float chop = clampv(0.15f + static_cast<float>(f.chaos) * 0.35f + p.complexity * 0.35f + (dens - 0.5f) * 0.4f
			+ (p.form == 2 ? 0.15f : 0.0f), 0.0f, 1.0f);
		const float ang = orbitA(f, t / 12), rad = orbitR(f, t / 12);
		const int vel = 96 + static_cast<int>(rad * 30.0f);
		auto play = [&](int slice, int v, int len) { last = slice; out.push_back({t, len, ps.baseKey + 1 + slice, clampv(v, 1, 127)}); };
		const bool fillBar = bar == p.bars - 1 && s32 >= 24;
		if (fillBar && rng.uni() < 0.35f + chop * 0.6f)
		{
			const int src = static_cast<int>(orbitA(f, bar * 3) * numSlices) % numSlices;
			if (s32 % 2 == 0 || chop > 0.55f) { play(src, 70 + (s32 % 8) * 6, 6); }
			continue;
		}
		if (s32 % 2 == 1) { continue; }
		if (orig >= 0)
		{
			if (rng.uni() < chop * 0.75f)
			{
				int alt = clampv(static_cast<int>(ang * numSlices), 0, numSlices - 1);
				if (rng.uni() < 0.25f) { alt = last; }
				play(alt, vel, 24);
			}
			else if (rng.uni() > 0.1f * (1.0f - dens)) { play(orig, vel, 24); }
			continue;
		}
		if (rng.uni() < (dens - 0.45f) * 0.5f * chop) { play(static_cast<int>(ang * numSlices) % numSlices, 60 + static_cast<int>(rad * 25.0f), 12); }
	}
}

} // namespace lmms::fractal
