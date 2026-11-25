// Stand-alone test of the MANDELBROT fractal engine (no Qt / LMMS needed).
// g++ -std=c++17 -O2 -I../../../../include engine_test.cpp ../*.cpp -o engine_test && ./engine_test
#include "fractal/Conductor.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

using namespace lmms::fractal;

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct Track { std::string name; PartSettings ps; PartGenerator gen; std::vector<GenNote> played; };

static void writeMidi(const char* path, const std::vector<Track>& tracks, int bpm)
{
	FILE* f = std::fopen(path, "wb");
	if (!f) { return; }
	auto be32 = [&](uint32_t v) { for (int i = 3; i >= 0; --i) { std::fputc((v >> (i * 8)) & 0xff, f); } };
	auto be16 = [&](uint16_t v) { std::fputc(v >> 8, f); std::fputc(v & 0xff, f); };
	std::fwrite("MThd", 1, 4, f); be32(6); be16(1); be16(static_cast<uint16_t>(tracks.size() + 1)); be16(48);
	auto vlq = [](std::vector<uint8_t>& d, uint32_t v) { uint8_t b[5]; int n = 0; do { b[n++] = v & 0x7f; v >>= 7; } while (v); while (n--) { d.push_back(b[n] | (n ? 0x80 : 0)); } };
	{
		std::vector<uint8_t> d; vlq(d, 0); const uint32_t us = 60000000 / bpm; d.insert(d.end(), {0xff, 0x51, 3, uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us)});
		vlq(d, 0); d.insert(d.end(), {0xff, 0x2f, 0}); std::fwrite("MTrk", 1, 4, f); be32(static_cast<uint32_t>(d.size())); std::fwrite(d.data(), 1, d.size(), f);
	}
	int ch = 0;
	for (const auto& t : tracks)
	{
		struct Ev { int tick; int on; int pitch; int vel; };
		std::vector<Ev> ev;
		const int c = isDrumRole(t.ps.role) ? 9 : (ch++ % 9);
		for (const auto& n : t.played) { ev.push_back({n.tick, 1, n.pitch, n.velocity}); ev.push_back({n.tick + n.length, 0, n.pitch, 0}); }
		std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.on < b.on; });
		std::vector<uint8_t> d; int last = 0;
		for (const auto& e : ev) { vlq(d, static_cast<uint32_t>(e.tick - last)); last = e.tick; d.push_back(uint8_t((e.on ? 0x90 : 0x80) | c)); d.push_back(uint8_t(e.pitch)); d.push_back(uint8_t(e.vel)); }
		vlq(d, 0); d.insert(d.end(), {0xff, 0x2f, 0});
		std::fwrite("MTrk", 1, 4, f); be32(static_cast<uint32_t>(d.size())); std::fwrite(d.data(), 1, d.size(), f);
	}
	std::fclose(f);
}

static void runStyle(Style style, int phrases, const char* midiPath)
{
	Conductor c;
	GlobalSettings gs; gs.style = style; gs.novelty = 0.8f; gs.noveltyMode = NoveltyMode::Recent;
	c.setSettings(gs);
	std::vector<Track> tracks;
	auto addT = [&](const char* name, Role r, int base = 69) { Track t; t.name = name; t.ps.role = r; t.ps.baseKey = base; t.ps.channelSeed = static_cast<int>(tracks.size()); tracks.push_back(std::move(t)); };
	const int drumKeys[] = {36, 38, 39, 42, 46, 37, 47, 41, 48, 49, 70};
	addT("Kick", Role::Kick, drumKeys[0]); addT("Snare", Role::Snare, drumKeys[1]); addT("Hat", Role::HatClosed, drumKeys[3]);
	addT("Open hat", Role::HatOpen, drumKeys[4]); addT("Shaker", Role::Shaker, drumKeys[10]); addT("Perc", Role::Perc, drumKeys[6]);
	addT("Bass", Role::Bass); addT("Chords", Role::Chords); addT("Lead", Role::Lead); addT("Arp", Role::Arp);
	if (style == Style::Ambient) { addT("Pad", Role::Pad); addT("Drone", Role::Drone); addT("Texture", Role::Texture); addT("Bells", Role::Bells); }
	if (style == Style::Breakbeat) { addT("Break", Role::Break, 60); }

	c.start(0);
	int64_t songTick = 0;
	int newPhrases = 1;
	std::map<int, std::set<uint64_t>> phraseHashes; // role -> pitch hashes of whole phrases
	int repeats = 0, phraseCount = 0;
	float maxSimSeen = 0.0f; double simSum = 0; int simN = 0;
	bool inKey = true;
	bool publishOk = true;
	const int totalTicks = phrases * c.plan().bars * TicksPerBar;
	for (int i = 0; i < totalTicks; ++i)
	{
		if (i > 0 && c.tick()) { ++newPhrases; }
		c.publishCurrentBar();
		{
			// what the effects see: a non-empty chord inside the key, a bass note of that chord
			const int cm = c.currentChordMask.load(), sm = c.currentScaleMask.load(), bass = c.currentChordBass.load();
			if (cm == 0 || (cm & ~sm) != 0 || !((cm >> (bass % 12)) & 1)) { publishOk = false; }
		}
		const auto& plan = c.plan();
		const int tip = c.tickInPhrase();
		for (auto& t : tracks)
		{
			if (t.gen.builtFor() != plan.serial)
			{
				const auto& notes = t.gen.build(plan, t.ps, c.history(), t.name);
				if (!isDrumRole(t.ps.role) && !notes.empty())
				{
					PhraseRecord r; r.role = static_cast<int>(t.ps.role); r.root = plan.key.root; r.bars = plan.bars; r.notes = notes;
					PhraseHistory::fingerprint(r);
					++phraseCount;
					if (!phraseHashes[r.role].insert(r.pitchHash ^ r.rhythmHash).second) { ++repeats; std::printf("  repeat: %s phrase %llu\n", t.name.c_str(), (unsigned long long) plan.serial); }
					maxSimSeen = std::max(maxSimSeen, t.gen.lastSimilarity());
					simSum += t.gen.lastSimilarity(); ++simN;
				}
			}
			for (const auto& n : t.gen.notes())
			{
				if (n.tick != tip) { continue; }
				t.played.push_back({static_cast<int>(songTick) / 4, n.length / 4, n.pitch, n.velocity}); // 48 ppq
				if (!isDrumRole(t.ps.role) && t.ps.role != Role::Break && !scales::contains(n.pitch, plan.key.root, plan.key.kind)) { inKey = false; }
			}
		}
		++songTick;
	}
	std::printf("\n== %s: %d phrases of %d bars, history %zu records\n", styleName(static_cast<int>(style)), newPhrases, c.plan().bars, c.history().size());
	for (const auto& t : tracks) { std::printf("  %-9s %5zu notes\n", t.name.c_str(), t.played.size()); }
	std::printf("  exact phrase repeats: %d of %d, mean similarity to old material %.2f, max %.2f\n", repeats, phraseCount, simN ? simSum / simN : 0.0, maxSimSeen);
	std::printf("  probe ended at %.5f %+.5fi\n", c.probeX.load(), c.probeY.load());
	CHECK(newPhrases == phrases);
	CHECK(inKey);
	CHECK(publishOk);
	CHECK(repeats == 0);
	for (const auto& t : tracks)
	{
		if (t.ps.role == Role::Bells || t.ps.role == Role::Texture || t.ps.role == Role::HatOpen || t.ps.role == Role::Perc
			|| t.ps.role == Role::Shaker || t.ps.role == Role::Snare || t.ps.role == Role::Arp) { continue; }
		if (style != Style::IDM && isDrumRole(t.ps.role)) { continue; } // sparse in some styles
		CHECK(!t.played.empty());
	}
	writeMidi(midiPath, tracks, style == Style::Ambient ? 76 : (style == Style::Breakbeat ? 170 : 128));
}

static void testHistory()
{
	PhraseHistory h;
	PhraseRecord a; a.role = 15; a.root = 9; a.bars = 2;
	for (int i = 0; i < 8; ++i) { a.notes.push_back({i * 24, 12, 69 + (i % 3) * 2, 100}); }
	PhraseRecord b = a; b.notes[7].pitch += 5;
	PhraseHistory::fingerprint(a); PhraseHistory::fingerprint(b);
	CHECK(PhraseHistory::similarity(a, a) > 0.999f);
	CHECK(PhraseHistory::similarity(a, b) > 0.7f && PhraseHistory::similarity(a, b) < 1.0f);
	PhraseRecord c; c.role = 15; c.root = 9; c.bars = 2;
	for (int i = 0; i < 5; ++i) { c.notes.push_back({i * 30 + 6, 12, 60 + i * 3, 90}); }
	PhraseHistory::fingerprint(c);
	CHECK(PhraseHistory::similarity(a, c) < 0.3f);
	h.add(a);
	CHECK(h.maxSimilarity(b, NoveltyMode::Recent) > 0.7f);
	CHECK(h.maxSimilarity(c, NoveltyMode::Recent) < 0.3f);
	CHECK(h.maxSimilarity(b, NoveltyMode::Off) == 0.0f);
	h.add(c);
	h.setFavourite(1, true);
	CHECK(h.save("/tmp/mbh_test.txt"));
	PhraseHistory h2;
	CHECK(h2.load("/tmp/mbh_test.txt"));
	CHECK(h2.size() == 2);
	PhraseRecord got;
	CHECK(h2.get(1, got) && got.favourite && got.notes.size() == 8);
	CHECK(h2.maxSimilarity(a, NoveltyMode::AllHistory) > 0.999f);
	CHECK(h2.nextId() == 3);
}

static void testMath()
{
	auto f = math::analyse(0.0, 0.0, 0.01);
	CHECK(f.inside && f.period == 1);
	auto g = math::analyse(-0.1225, 0.7449, 0.01);
	CHECK(g.inside && g.period == 3);
	auto e = math::analyse(-0.7436, 0.1318, 0.01);
	CHECK(e.boundary > 0.3 && e.orbitCount >= 8);
	Key k; k.set(9, scales::Dorian);
	CHECK(k.pitch(0, 4) == 69 && k.pitch(7, 4) == 81 && k.pitch(-1, 4) == 67);
	CHECK(k.degreeOf(81, 4) == 7);
}

int main()
{
	testMath();
	testHistory();
	runStyle(Style::IDM, 24, "/tmp/fractal_idm.mid");
	runStyle(Style::Ambient, 12, "/tmp/fractal_ambient.mid");
	runStyle(Style::Breakbeat, 24, "/tmp/fractal_breakbeat.mid");
	std::printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
