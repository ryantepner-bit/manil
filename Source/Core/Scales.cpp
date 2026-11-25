#include "Scales.h"

namespace Scales
{
    static int maskFromList (std::initializer_list<int> l)
    {
        int m = 0;
        for (auto i : l) m |= (1 << i);
        return m;
    }

    juce::StringArray names()
    {
        return { "Major", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
                 "Harmonic Minor", "Melodic Minor", "Whole Tone", "Pentatonic Minor", "Hirajoshi",
                 "Chromatic", "Custom" };
    }

    juce::String name (int kind) { return names()[juce::jlimit (0, NumKinds - 1, kind)]; }

    int mask (int kind, int customMask)
    {
        switch (kind)
        {
            case Major:           return maskFromList ({ 0, 2, 4, 5, 7, 9, 11 });
            case Dorian:          return maskFromList ({ 0, 2, 3, 5, 7, 9, 10 });
            case Phrygian:        return maskFromList ({ 0, 1, 3, 5, 7, 8, 10 });
            case Lydian:          return maskFromList ({ 0, 2, 4, 6, 7, 9, 11 });
            case Mixolydian:      return maskFromList ({ 0, 2, 4, 5, 7, 9, 10 });
            case Aeolian:         return maskFromList ({ 0, 2, 3, 5, 7, 8, 10 });
            case Locrian:         return maskFromList ({ 0, 1, 3, 5, 6, 8, 10 });
            case HarmonicMinor:   return maskFromList ({ 0, 2, 3, 5, 7, 8, 11 });
            case MelodicMinor:    return maskFromList ({ 0, 2, 3, 5, 7, 9, 11 });
            case WholeTone:       return maskFromList ({ 0, 2, 4, 6, 8, 10 });
            case PentatonicMinor: return maskFromList ({ 0, 3, 5, 7, 10 });
            case Hirajoshi:       return maskFromList ({ 0, 2, 3, 7, 8 });
            case Chromatic:       return 0xfff;
            case Custom:          return (customMask & 0xfff) != 0 ? ((customMask & 0xfff) | 1) : 0xfff;
            default:              return 0xfff;
        }
    }

    std::vector<int> degrees (int kind, int customMask)
    {
        std::vector<int> d;
        const int m = mask (kind, customMask);
        for (int i = 0; i < 12; ++i)
            if (m & (1 << i)) d.push_back (i);
        if (d.empty()) d.push_back (0);
        return d;
    }

    bool contains (int pitch, int root, int kind, int customMask)
    {
        const int pc = ((pitch - root) % 12 + 12) % 12;
        return (mask (kind, customMask) >> pc) & 1;
    }

    int degreeToPitch (int degree, int root, int baseOctave, const std::vector<int>& degs)
    {
        const int n = (int) degs.size();
        int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        int idx = degree - oct * n;
        return juce::jlimit (0, 127, 12 * (baseOctave + 1) + root + oct * 12 + degs[(size_t) idx]);
    }

    int snap (int pitch, int root, int kind, int customMask)
    {
        for (int d = 0; d < 12; ++d)
        {
            if (contains (pitch - d, root, kind, customMask)) return juce::jlimit (0, 127, pitch - d);
            if (contains (pitch + d, root, kind, customMask)) return juce::jlimit (0, 127, pitch + d);
        }
        return pitch;
    }

    juce::StringArray rootNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }

    juce::String noteName (int n)
    {
        return rootNames()[((n % 12) + 12) % 12] + juce::String (n / 12 - 1);
    }
}
