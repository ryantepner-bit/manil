/*
 * PhraseHistory.h - everything the fractal engine has played, and the anti-repetition check.
 *
 * Every phrase a generator produces is fingerprinted (rhythm, pitch and interval shape) and
 * stored. Before a new phrase is used it is compared with the recent ones (or, if the user
 * wants, with the whole saved history); a phrase that is too close to old material is
 * regenerated with a different variation. The history is kept on disk so it survives restarts,
 * and every phrase can be recalled into a MIDI clip.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#ifndef LMMS_FRACTAL_PHRASE_HISTORY_H
#define LMMS_FRACTAL_PHRASE_HISTORY_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace lmms::fractal
{

//! A generated note, relative to the start of its phrase. Pitch is MIDI (0..127).
struct GenNote
{
	int tick = 0;      //!< 192 ticks per 4/4 bar
	int length = 12;
	int pitch = 60;
	int velocity = 100; //!< 1..127
};

enum class NoveltyMode { Off = 0, Recent, AllHistory };

struct PhraseRecord
{
	uint64_t id = 0;
	int64_t time = 0;          //!< unix seconds
	int role = 0;
	int style = 0;
	int root = 0;              //!< key root pitch class (pitches are compared relative to it)
	int bars = 1;
	double cx = 0, cy = 0;     //!< where in the set it came from
	bool favourite = false;
	std::string track;         //!< track name at the time
	std::vector<GenNote> notes;
	// fingerprint (computed by PhraseHistory::fingerprint)
	uint64_t rhythmHash = 0, pitchHash = 0, shapeHash = 0;
	std::vector<uint32_t> rhythmSet, pitchSet; //!< sorted
	std::vector<int> intervals;
};

class PhraseHistory
{
public:
	explicit PhraseHistory(size_t capacity = 20000) : m_capacity(capacity) {}

	static void fingerprint(PhraseRecord& r);
	//! 0 = nothing in common, 1 = identical (same rhythm, pitches and melodic shape)
	static float similarity(const PhraseRecord& a, const PhraseRecord& b);

	/** Highest similarity between `candidate` and remembered phrases of the same role
	 *  (Recent: the last `recentCount` of that role; AllHistory: everything; Off: 0).
	 *  Phrases with id in [excludeFrom, ...) (the current section's own motifs) are skipped. */
	float maxSimilarity(const PhraseRecord& candidate, NoveltyMode mode, size_t recentCount = 256,
		uint64_t excludeFromId = UINT64_MAX) const;

	//! True if exactly this phrase (same rhythm and pitches) is anywhere in memory
	//! (Recent: only this session's phrases; AllHistory: everything remembered; Off: never)
	bool containsExact(const PhraseRecord& fingerprinted, NoveltyMode mode) const;
	//! Phrases with smaller ids come from earlier sessions (loaded from disk)
	void markSessionStart() { std::lock_guard<std::mutex> g(m_lock); m_sessionStart = m_nextId; }

	//! Stores a phrase (fingerprints it). Returns its id.
	uint64_t add(PhraseRecord r);

	//! Copy of the newest `max` records (newest first), optionally only favourites.
	std::vector<PhraseRecord> recent(size_t max, bool favouritesOnly = false) const;
	bool get(uint64_t id, PhraseRecord& out) const;
	void setFavourite(uint64_t id, bool fav);
	void clear();
	size_t size() const;
	uint64_t nextId() const;
	bool dirty() const { return m_dirty; }

	bool save(const std::string& path) const;
	bool load(const std::string& path);

private:
	mutable std::mutex m_lock;
	std::deque<PhraseRecord> m_records;
	size_t m_capacity;
	uint64_t m_nextId = 1;
	uint64_t m_sessionStart = 1;
	mutable bool m_dirty = false;
	std::unordered_map<uint64_t, std::vector<uint64_t>> m_byRhythm; //!< rhythm hash -> ids (fast lookup for AllHistory)
};

} // namespace lmms::fractal

#endif // LMMS_FRACTAL_PHRASE_HISTORY_H
