#pragma once
#include <array>
#include <vector>
#include <juce_core/juce_core.h>

namespace Scales
{
    enum Kind
    {
        Major = 0, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian,
        HarmonicMinor, MelodicMinor, WholeTone, PentatonicMinor, Hirajoshi,
        Chromatic, Custom, NumKinds
    };

    juce::StringArray names();
    juce::String name (int kind);

    /** 12-bit mask; bit i set => pitch class (root + i) is in scale. */
    int mask (int kind, int customMask);

    /** Ascending semitone offsets (0..11) of the scale. */
    std::vector<int> degrees (int kind, int customMask);

    bool contains (int pitch, int root, int kind, int customMask);

    /** Map a (possibly negative / >size) scale-degree index to a MIDI note. */
    int degreeToPitch (int degree, int root, int baseOctave, const std::vector<int>& degs);

    /** Snap any pitch to the nearest scale pitch. */
    int snap (int pitch, int root, int kind, int customMask);

    juce::String noteName (int midiNote);
    juce::StringArray rootNames();
}
