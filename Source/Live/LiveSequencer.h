#pragma once
#include <atomic>
#include <array>
#include <memory>
#include <juce_data_structures/juce_data_structures.h>
#include "../Audio/Synth.h"

/**
 * Live generative mode.
 *
 * The message thread turns the fractal probe into an immutable Live::Frame
 * (the orbit of z -> z^2 + c, the region's characteristics, the key/scale and
 * the per-part settings) and publishes it atomically. The audio thread runs a
 * step sequencer that maps that data onto the scale in real time: every note
 * is in key, nothing is pre-rendered, and because the probe keeps drifting
 * the music never repeats exactly.
 *
 *   pitch     <- orbit angle   (arg z_n), quantised to the scale
 *   velocity  <- orbit radius  (|z_n|)
 *   riff len  <- attracting cycle period (inside the set: a p-note riff)
 *   rhythm    <- escape speed / period / boundary distance (euclidean pulses)
 *   harmony   <- orbit sampled once per bar (chord degree in the scale)
 *   lead      <- 2-bar phrases built from the orbit (A A' B A'' form, chord tones on strong beats)
 *   break     <- the break's own slices, re-ordered / stuttered by the orbit
 *
 * Styles: IDM (default), Ambient (drones, slow pads, sparse bells, a
 * stochastic grain cloud after Cecilia's StochGrains), Breakbeat (chopped break).
 */
namespace Live
{
    enum Part { None = 0, Drums, Bass, Chords, Lead, Arp, Break, NumParts };
    enum Style { IDM = 0, Ambient, Breakbeat };
    juce::StringArray partNames();

    struct PartSettings
    {
        bool on = true;
        float density = 0.6f;     // 0..1
        int octave = 4;
        int midiChannel = 1;      // 1..16 for MIDI out
    };

    struct Frame
    {
        static constexpr int maxOrbit = 128;
        static constexpr int maxSlices = 64;
        float angle[maxOrbit] {};   // 0..1
        float radius[maxOrbit] {};  // 0..1
        int orbitCount = 1;
        float escape = 0, boundary = 0, chaos = 0, entropy = 0, insideFraction = 0, roughness = 0;
        int period = 0;
        bool inside = false;
        int root = 9;
        int degs[12] {}; int numDegs = 7;
        float complexity = 0.5f;
        int style = IDM;
        int chordSpeed = 0;         // 0 = follow the fractal, else bars per chord
        float swing = 0.0f;         // 0..0.6
        float repeat = 0.6f;        // how much the lead repeats itself
        // the break of the Break track: slice start positions in beats, and its length
        float sliceBeat[maxSlices] {};
        int numSlices = 0;
        float breakBeats = 8.0f;
        PartSettings parts[NumParts];
        juce::uint32 seed = 1;      // changes as the probe moves
    };

    /** A generated note event, for monitoring, capture and MIDI out. */
    struct MonitorEvent { int part = 0; int note = 60; int velocity = 0; double beat = 0.0; };

    /** Audio-thread step sequencer. All public calls except the ones marked are audio-thread only. */
    class Sequencer
    {
    public:
        static constexpr int maxEventsPerPart = 96;

        /** Message thread: publish a new frame (the old one is retired safely). */
        void setFrame (std::shared_ptr<Frame> f);
        std::atomic<bool> enabled { true };
        std::atomic<double> liveBeat { 0.0 };          // read by the UI
        std::atomic<int> currentChord { 0 };            // scale degree of the chord root (UI)

        /** Generate events for one block. Sets eventCount[part] and events[part][...]. */
        void process (int numSamples, double beatsPerSample, int beatsPerBar);
        /** Note-offs for everything that is sounding (stop / disable). */
        void flush();
        /** Re-align the live clock (transport start). */
        void alignTo (double beat) { clock = beat; liveBeat.store (beat); }
        /** MIDI channel (1..16) of a part in the current frame. */
        int channelFor (int part) const { auto* f = frame.load (std::memory_order_acquire); return f != nullptr ? f->parts[part].midiChannel : part; }
        bool hasFrame() const { return frame.load() != nullptr; }

        NoteEvent events[NumParts][maxEventsPerPart];
        int eventCount[NumParts] {};

        /** Lock-free monitor queue (single consumer: message thread). */
        juce::AbstractFifo monitorFifo { 4096 };
        std::array<MonitorEvent, 4096> monitorData;

    private:
        struct Pending { double offBeat = 0; int note = 0; };
        struct PhraseNote { bool on = false; int degree = 0; int len = 1; int vel = 90; };
        static constexpr int phraseSteps = 64;          // up to 4 bars of 16ths

        void stepPart (const Frame& f, int part, long long step, double stepBeat, int offset, int beatsPerBar, double stepLen);
        void stepDrums (const Frame& f, int s, long long bar, double stepBeat, int offset, DSP::Rng& rng, float cx);
        void stepBreak (const Frame& f, long long step, double stepBeat, int offset, int bpb, double stepLen, DSP::Rng& rng);
        void stepLead (const Frame& f, int s, long long bar, double stepBeat, int offset, int bpb, double stepLen);
        void buildPhrase (const Frame& f, long long phrase, int stepsPerBar);
        void noteOn (int part, int note, int vel, int offset, double beat, double lengthBeats);
        void emit (int part, int offset, int note, float vel, double beat);
        void chordFor (const Frame& f, long long bar);
        int chordToneNear (const Frame& f, int degree) const;

        std::atomic<Frame*> frame { nullptr };
        std::shared_ptr<Frame> owner;
        double clock = 0.0;
        std::array<std::array<Pending, 32>, NumParts> pending {};
        std::array<int, NumParts> numPending {};
        std::array<int, NumParts> lastDegree {};
        std::array<int, NumParts> arpIndex {};
        long long chordBar = -1, lastChordChange = -1;
        int chordRoot = 0, chordSize = 3, prevChordRoot = -1, chordLenBars = 1;
        std::array<int, 4> chordDegs {};
        // lead phrase memory: the motif (A) and the phrase being played
        std::array<PhraseNote, phraseSteps> motif {}, phrase {};
        long long phraseIndex = -1;
        int phraseBars = 2;
        bool haveMotif = false;
        int lastBreakSlice = 0;
        double blockStart = 0.0, bps = 0.0;
        int blockLen = 0;
    };

    /** Break of the Break track, for the frame (message thread). */
    struct BreakInfo { std::vector<float> slicePoints; double lengthBeats = 8.0; };

    /** Message-thread helper: fill a frame from a probe point and the project's LIVE node. */
    void buildFrame (Frame& f, double cx, double cy, double scale, const juce::ValueTree& liveNode, const BreakInfo* breakInfo = nullptr);

    /** Default LIVE node for new projects. */
    juce::ValueTree createLiveNode();
}
