#pragma once
#include <juce_data_structures/juce_data_structures.h>

// All ValueTree type names and property identifiers used by the project model.
namespace IDs
{
#define MANDEL_ID(name) inline const juce::Identifier name { #name };
    // node types
    MANDEL_ID (PROJECT)
    MANDEL_ID (TRACKS)
    MANDEL_ID (TRACK)
    MANDEL_ID (CLIPS)
    MANDEL_ID (CLIP)
    MANDEL_ID (NOTE)
    MANDEL_ID (PATTERN)
    MANDEL_ID (LANE)
    MANDEL_ID (SYNTH)
    MANDEL_ID (DRUMKIT)
    MANDEL_ID (PAD)
    MANDEL_ID (INSERTS)
    MANDEL_ID (SLOT)
    MANDEL_ID (AUTOMATION)
    MANDEL_ID (ALANE)
    MANDEL_ID (POINT)
    MANDEL_ID (BUSES)
    MANDEL_ID (BUS)
    MANDEL_ID (MASTER)
    MANDEL_ID (GENERATORS)
    MANDEL_ID (GEN)
    MANDEL_ID (FRACTAL)
    MANDEL_ID (BOOKMARKS)
    MANDEL_ID (BOOKMARK)
    MANDEL_ID (PATHS)
    MANDEL_ID (PATH)
    MANDEL_ID (KEY)
    MANDEL_ID (MODMATRIX)
    MANDEL_ID (MOD)

    // generic props
    MANDEL_ID (id)
    MANDEL_ID (name)
    MANDEL_ID (type)
    MANDEL_ID (colour)
    MANDEL_ID (version)

    // project
    MANDEL_ID (tempo)
    MANDEL_ID (tsNum)
    MANDEL_ID (tsDen)
    MANDEL_ID (loopOn)
    MANDEL_ID (loopStart)
    MANDEL_ID (loopEnd)
    MANDEL_ID (nextId)
    MANDEL_ID (metronome)

    // track / channel
    MANDEL_ID (mute)
    MANDEL_ID (solo)
    MANDEL_ID (arm)
    MANDEL_ID (volume)
    MANDEL_ID (pan)
    MANDEL_ID (output)
    MANDEL_ID (send0)
    MANDEL_ID (send1)
    MANDEL_ID (send2)
    MANDEL_ID (send3)
    MANDEL_ID (height)
    MANDEL_ID (showAuto)

    // clip
    MANDEL_ID (start)
    MANDEL_ID (length)
    MANDEL_ID (file)
    MANDEL_ID (offset)
    MANDEL_ID (gain)
    MANDEL_ID (fadeIn)
    MANDEL_ID (fadeOut)
    MANDEL_ID (swing)

    // note
    MANDEL_ID (p)   // pitch
    MANDEL_ID (s)   // start beat (relative to clip)
    MANDEL_ID (l)   // length beats
    MANDEL_ID (v)   // velocity 1..127
    MANDEL_ID (pr)  // probability 0..1

    // pattern lane
    MANDEL_ID (pad)
    MANDEL_ID (steps)
    MANDEL_ID (rate)
    MANDEL_ID (data)

    // pad
    MANDEL_ID (sample)
    MANDEL_ID (synthKind)

    // insert slot
    MANDEL_ID (fx)
    MANDEL_ID (p0)
    MANDEL_ID (p1)
    MANDEL_ID (p2)
    MANDEL_ID (p3)
    MANDEL_ID (bypass)

    // automation
    MANDEL_ID (param)
    MANDEL_ID (t)
    MANDEL_ID (val)

    // modulation
    MANDEL_ID (src)
    MANDEL_ID (dst)
    MANDEL_ID (amt)

    // generator
    MANDEL_ID (track)
    MANDEL_ID (role)
    MANDEL_ID (scale)
    MANDEL_ID (root)
    MANDEL_ID (customMask)
    MANDEL_ID (seed)
    MANDEL_ID (bars)
    MANDEL_ID (density)
    MANDEL_ID (complexity)
    MANDEL_ID (mutation)
    MANDEL_ID (octave)
    MANDEL_ID (live)
    MANDEL_ID (motif)
    MANDEL_ID (cx)
    MANDEL_ID (cy)
    MANDEL_ID (lockPoint)
    MANDEL_ID (generation)

    // fractal
    MANDEL_ID (scaleF)  // view width in complex units
    MANDEL_ID (iters)
    MANDEL_ID (palette)
    MANDEL_ID (probeX)
    MANDEL_ID (probeY)
    MANDEL_ID (beat)

    // live generative mode
    MANDEL_ID (LIVE)
    MANDEL_ID (LPART)
    MANDEL_ID (livePart)
    MANDEL_ID (liveOn)
    MANDEL_ID (songMode)
    MANDEL_ID (drift)
    MANDEL_ID (midiCh)
    MANDEL_ID (variation)
#undef MANDEL_ID
}
