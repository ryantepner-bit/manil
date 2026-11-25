#pragma once
#include "../Core/ProjectModel.h"
#include "../Core/Scales.h"
#include "../Fractal/MandelMath.h"

namespace Gen
{
    enum Role { Melody = 0, Bass, Chords, Arp, Texture, Breaks, NumRoles };
    juce::StringArray roleNames();

    struct Settings
    {
        int role = Melody;
        int scale = Scales::Dorian;
        int root = 9;          // A
        int customMask = 0xfff;
        juce::int64 seed = 1;
        int bars = 4;
        int beatsPerBar = 4;
        float density = 0.5f;
        float complexity = 0.5f;
        float mutation = 0.25f;
        int octave = 4;
    };

    struct MotifNote { int degree = 0; double onset = 0.0, duration = 0.25; int velocity = 100; };

    struct Motif
    {
        std::vector<MotifNote> notes;
        double length = 4.0;   // beats
        bool empty() const { return notes.empty(); }
        juce::String serialise() const;
        static Motif parse (const juce::String&);
    };

    struct PatternLaneOut { int pad = 0, steps = 16, rate = 4; std::vector<StepData> data; };

    struct Result
    {
        std::vector<NoteData> notes;
        std::vector<PatternLaneOut> lanes;   // only for Breaks
        float swing = 0.0f;
        Motif motif;
        juce::String summary;                // human readable description of the musical decisions
    };

    /** The core mapping from fractal characteristics to musical parameters. */
    struct MusicalTraits
    {
        int subdivision = 4;           // grid steps per beat
        bool irregular = false;        // per-beat varying subdivisions
        float syncopation = 0.0f;
        float densityScale = 1.0f;
        int polyCycle = 5;             // length of polyrhythmic cycles
        int chordsPerBar = 1;
        float consonance = 0.5f;
        float legato = 0.5f;
        float accent = 0.5f;
        static MusicalTraits from (const FractalFeatures& f, const Settings& s);
        juce::String describe() const;
    };

    /**
     * Generate musical material. Deterministic: same settings + same features +
     * same previous motif + same generation index => identical output.
     * If 'previous' is non-empty, it is mutated (motif preservation) instead of
     * being replaced, with the amount controlled by settings.mutation and
     * 'evolveAmount' (how far the fractal location moved).
     */
    Result generate (const Settings& s, const FractalFeatures& f, const Motif& previous, int generationIndex, float evolveAmount = 0.0f);

    Motif deriveMotif (const Settings& s, const FractalFeatures& f, juce::Random& rng);
    Motif mutate (const Motif& m, const Settings& s, const FractalFeatures& f, float amount, juce::Random& rng);

    std::vector<int> progression (const Settings& s, const FractalFeatures& f, const MusicalTraits& tr, juce::Random& rng);

    Settings settingsFromTree (const juce::ValueTree& gen, int beatsPerBar);
    juce::ValueTree createGeneratorNode (juce::ValueTree project, int trackId, int role);

    /** Applies a generation result to a clip (notes or pattern), undoable. */
    void applyToClip (juce::ValueTree clip, const Result& r, juce::UndoManager* um);
}
