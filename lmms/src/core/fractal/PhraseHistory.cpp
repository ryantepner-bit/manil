/*
 * PhraseHistory.cpp - phrase memory and anti-repetition.
 *
 * This file is part of MANDELBROT, a fork of LMMS - https://lmms.io
 * Licence: AGPL-3.0 (see docs/AUDIT_AND_MIGRATION.md)
 */

#include "fractal/PhraseHistory.h"
#include "fractal/FractalMath.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace lmms::fractal
{

namespace
{

float jaccard(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
	if (a.empty() && b.empty()) { return 1.0f; }
	size_t i = 0, j = 0, inter = 0;
	while (i < a.size() && j < b.size())
	{
		if (a[i] == b[j]) { ++inter; ++i; ++j; }
		else if (a[i] < b[j]) { ++i; }
		else { ++j; }
	}
	const size_t uni = a.size() + b.size() - inter;
	return uni == 0 ? 1.0f : static_cast<float>(inter) / static_cast<float>(uni);
}

//! longest common subsequence ratio of two interval sequences
float lcsRatio(const std::vector<int>& a, const std::vector<int>& b)
{
	if (a.empty() && b.empty()) { return 1.0f; }
	if (a.empty() || b.empty()) { return 0.0f; }
	const size_t n = std::min<size_t>(a.size(), 64), m = std::min<size_t>(b.size(), 64);
	std::vector<int> prev(m + 1, 0), cur(m + 1, 0);
	for (size_t i = 1; i <= n; ++i)
	{
		for (size_t j = 1; j <= m; ++j)
		{
			cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : std::max(prev[j], cur[j - 1]);
		}
		std::swap(prev, cur);
	}
	return static_cast<float>(prev[m]) / static_cast<float>(std::max(n, m));
}

uint64_t hashVec(const std::vector<uint32_t>& v, uint64_t salt)
{
	uint64_t h = 1469598103934665603ull ^ salt;
	for (auto x : v) { h = hash64(h ^ x) * 1099511628211ull; }
	return h;
}

} // namespace

void PhraseHistory::fingerprint(PhraseRecord& r)
{
	auto notes = r.notes;
	std::sort(notes.begin(), notes.end(), [](const GenNote& a, const GenNote& b)
		{ return a.tick != b.tick ? a.tick < b.tick : a.pitch > b.pitch; });
	r.rhythmSet.clear(); r.pitchSet.clear(); r.intervals.clear();
	int lastTick = -1, lastPitch = -1;
	for (const auto& n : notes)
	{
		const auto step = static_cast<uint32_t>(std::max(0, n.tick) / 6); // 32nd grid
		const auto rel = static_cast<uint32_t>((n.pitch - r.root + 256) & 0xff);
		r.rhythmSet.push_back(step);
		r.pitchSet.push_back((step << 8) | rel);
		if (n.tick != lastTick) // top voice only for the melodic shape
		{
			if (lastPitch >= 0) { r.intervals.push_back(n.pitch - lastPitch); }
			lastPitch = n.pitch;
			lastTick = n.tick;
		}
	}
	auto uniq = [](std::vector<uint32_t>& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
	uniq(r.rhythmSet);
	uniq(r.pitchSet);
	r.rhythmHash = hashVec(r.rhythmSet, static_cast<uint64_t>(r.bars) * 31 + static_cast<uint64_t>(r.role));
	r.pitchHash = hashVec(r.pitchSet, static_cast<uint64_t>(r.role));
	std::vector<uint32_t> iv;
	for (int x : r.intervals) { iv.push_back(static_cast<uint32_t>(x + 128)); }
	r.shapeHash = hashVec(iv, 7);
}

float PhraseHistory::similarity(const PhraseRecord& a, const PhraseRecord& b)
{
	if (a.notes.empty() || b.notes.empty()) { return (a.notes.empty() && b.notes.empty()) ? 1.0f : 0.0f; }
	if (a.pitchHash == b.pitchHash && a.rhythmHash == b.rhythmHash) { return 1.0f; }
	const float jr = jaccard(a.rhythmSet, b.rhythmSet);
	const float jp = jaccard(a.pitchSet, b.pitchSet);
	const float sh = lcsRatio(a.intervals, b.intervals);
	return 0.4f * jr + 0.35f * jp + 0.25f * sh;
}

float PhraseHistory::maxSimilarity(const PhraseRecord& c, NoveltyMode mode, size_t recentCount, uint64_t excludeFrom) const
{
	if (mode == NoveltyMode::Off) { return 0.0f; }
	std::lock_guard<std::mutex> g(m_lock);
	const uint64_t from = mode == NoveltyMode::Recent ? m_sessionStart : 0;
	float best = 0.0f;
	size_t seen = 0;
	for (auto it = m_records.rbegin(); it != m_records.rend() && seen < recentCount; ++it)
	{
		if (it->id < from) { break; }
		if (it->role != c.role) { continue; }
		++seen;
		if (it->id >= excludeFrom) { continue; }
		best = std::max(best, similarity(c, *it));
	}
	// exact / near-exact matches anywhere in memory are always avoided (rhythm index); AllHistory
	// additionally keeps the whole saved history in memory (see FractalEngine)
	{
		const auto f = m_byRhythm.find(c.rhythmHash);
		if (f != m_byRhythm.end())
		{
			for (auto id : f->second)
			{
				if (id >= excludeFrom || id < from) { continue; }
				// records are sorted by id: binary search
				auto pos = std::lower_bound(m_records.begin(), m_records.end(), id,
					[](const PhraseRecord& r, uint64_t v) { return r.id < v; });
				if (pos != m_records.end() && pos->id == id && pos->role == c.role)
				{
					best = std::max(best, similarity(c, *pos));
				}
			}
		}
	}
	return best;
}

bool PhraseHistory::containsExact(const PhraseRecord& r, NoveltyMode mode) const
{
	if (mode == NoveltyMode::Off) { return false; }
	std::lock_guard<std::mutex> g(m_lock);
	const uint64_t from = mode == NoveltyMode::Recent ? m_sessionStart : 0;
	const auto f = m_byRhythm.find(r.rhythmHash);
	if (f == m_byRhythm.end()) { return false; }
	for (auto id : f->second)
	{
		auto pos = std::lower_bound(m_records.begin(), m_records.end(), id,
			[](const PhraseRecord& x, uint64_t v) { return x.id < v; });
		if (pos != m_records.end() && pos->id == id && pos->id >= from && pos->role == r.role && pos->pitchHash == r.pitchHash) { return true; }
	}
	return false;
}

uint64_t PhraseHistory::add(PhraseRecord r)
{
	fingerprint(r);
	std::lock_guard<std::mutex> g(m_lock);
	r.id = m_nextId++;
	m_byRhythm[r.rhythmHash].push_back(r.id);
	m_records.push_back(std::move(r));
	while (m_records.size() > m_capacity)
	{
		const auto& old = m_records.front();
		auto f = m_byRhythm.find(old.rhythmHash);
		if (f != m_byRhythm.end())
		{
			auto& v = f->second;
			v.erase(std::remove(v.begin(), v.end(), old.id), v.end());
			if (v.empty()) { m_byRhythm.erase(f); }
		}
		m_records.pop_front();
	}
	m_dirty = true;
	return m_nextId - 1;
}

std::vector<PhraseRecord> PhraseHistory::recent(size_t max, bool favouritesOnly) const
{
	std::lock_guard<std::mutex> g(m_lock);
	std::vector<PhraseRecord> out;
	for (auto it = m_records.rbegin(); it != m_records.rend() && out.size() < max; ++it)
	{
		if (!favouritesOnly || it->favourite) { out.push_back(*it); }
	}
	return out;
}

bool PhraseHistory::get(uint64_t id, PhraseRecord& out) const
{
	std::lock_guard<std::mutex> g(m_lock);
	auto pos = std::lower_bound(m_records.begin(), m_records.end(), id,
		[](const PhraseRecord& r, uint64_t v) { return r.id < v; });
	if (pos == m_records.end() || pos->id != id) { return false; }
	out = *pos;
	return true;
}

void PhraseHistory::setFavourite(uint64_t id, bool fav)
{
	std::lock_guard<std::mutex> g(m_lock);
	auto pos = std::lower_bound(m_records.begin(), m_records.end(), id,
		[](const PhraseRecord& r, uint64_t v) { return r.id < v; });
	if (pos != m_records.end() && pos->id == id) { pos->favourite = fav; m_dirty = true; }
}

void PhraseHistory::clear()
{
	std::lock_guard<std::mutex> g(m_lock);
	m_records.clear();
	m_byRhythm.clear();
	m_dirty = true;
}

size_t PhraseHistory::size() const
{
	std::lock_guard<std::mutex> g(m_lock);
	return m_records.size();
}

uint64_t PhraseHistory::nextId() const
{
	std::lock_guard<std::mutex> g(m_lock);
	return m_nextId;
}

// file format (text, one phrase per line):
// MBH1 <id> <time> <role> <style> <root> <bars> <cx> <cy> <fav> <track name, escaped> | t,l,p,v;t,l,p,v;...
bool PhraseHistory::save(const std::string& path) const
{
	std::deque<PhraseRecord> copy;
	{
		std::lock_guard<std::mutex> g(m_lock);
		copy = m_records;
		m_dirty = false;
	}
	const std::string tmp = path + ".tmp";
	{
		std::ofstream os(tmp, std::ios::trunc);
		if (!os) { return false; }
		for (const auto& r : copy)
		{
			std::string name = r.track;
			std::replace(name.begin(), name.end(), ' ', '_');
			std::replace(name.begin(), name.end(), '|', '_');
			if (name.empty()) { name = "-"; }
			char head[200];
			std::snprintf(head, sizeof(head), "MBH1 %llu %lld %d %d %d %d %.12g %.12g %d ",
				static_cast<unsigned long long>(r.id), static_cast<long long>(r.time), r.role, r.style, r.root, r.bars,
				r.cx, r.cy, r.favourite ? 1 : 0);
			os << head << name << " |";
			for (const auto& n : r.notes) { os << n.tick << ',' << n.length << ',' << n.pitch << ',' << n.velocity << ';'; }
			os << '\n';
		}
		if (!os) { return false; }
	}
	std::remove(path.c_str());
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool PhraseHistory::load(const std::string& path)
{
	std::ifstream is(path);
	if (!is) { return false; }
	std::deque<PhraseRecord> recs;
	std::string line;
	while (std::getline(is, line))
	{
		const auto bar = line.find('|');
		if (line.rfind("MBH1 ", 0) != 0 || bar == std::string::npos) { continue; }
		std::istringstream hs(line.substr(5, bar - 5));
		PhraseRecord r;
		unsigned long long id = 0; long long t = 0; int fav = 0;
		if (!(hs >> id >> t >> r.role >> r.style >> r.root >> r.bars >> r.cx >> r.cy >> fav >> r.track)) { continue; }
		r.id = id; r.time = t; r.favourite = fav != 0;
		std::istringstream ns(line.substr(bar + 1));
		std::string item;
		while (std::getline(ns, item, ';'))
		{
			GenNote n;
			if (std::sscanf(item.c_str(), "%d,%d,%d,%d", &n.tick, &n.length, &n.pitch, &n.velocity) == 4) { r.notes.push_back(n); }
		}
		fingerprint(r);
		recs.push_back(std::move(r));
	}
	std::sort(recs.begin(), recs.end(), [](const PhraseRecord& a, const PhraseRecord& b) { return a.id < b.id; });
	std::lock_guard<std::mutex> g(m_lock);
	m_records = std::move(recs);
	while (m_records.size() > m_capacity) { m_records.pop_front(); }
	m_byRhythm.clear();
	m_nextId = 1;
	for (const auto& r : m_records)
	{
		m_byRhythm[r.rhythmHash].push_back(r.id);
		m_nextId = std::max(m_nextId, r.id + 1);
	}
	m_dirty = false;
	return true;
}

} // namespace lmms::fractal
