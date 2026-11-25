#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <map>
#include <array>

/**
 * Drum sample library.
 *
 * Scans a folder (recursively) for audio files and sorts them into drum
 * categories from their file names, so it works with any pack (Splice, FL
 * Studio packs, the free library below) without tagging anything.
 * Kits are built from those categories: one sample per pad.
 *
 * The free library is the CC0 standard library of Synkopa
 * (github.com/miskibin/synkopa, crates/engine/library; recordings released to
 * the public domain: Versilian Community Sample Library, TidalCycles TR-808,
 * MckAudio TR-8, Freesound CC0 uploads). It can be downloaded from inside the
 * app into the sample folder.
 */
namespace SampleLib
{
    enum Cat { Kick, Snare, Clap, HatClosed, HatOpen, Ride, Crash, TomLow, TomHigh, Rim, Perc, Shaker, Fx, Sub, Break, NumCats };
    juce::StringArray catNames();

    /** Category from a file name ("Kick_Hard_01.wav" -> Kick), or -1. */
    int classify (const juce::String& fileName);

    struct Library
    {
        juce::File folder;
        std::array<juce::Array<juce::File>, NumCats> files;
        int total() const { int n = 0; for (auto& a : files) n += a.size(); return n; }
    };

    /** Where samples live: the saved folder, else "Samples" next to the exe (or one level up). */
    juce::File defaultFolder();
    void setFolder (const juce::File& f);          // remembered across runs
    const Library& get();                          // scanned lazily, message thread
    void rescan();

    /** Kit styles ("Synth" = the built-in synthesised drums). */
    juce::StringArray kitNames();
    /** One file per pad (12 pads) for a kit style; empty file = keep the synthesised sound. */
    std::array<juce::File, 12> buildKit (const juce::String& kit, juce::uint32 seed);
    /** The next/previous sample of the same category as 'current' for a pad. */
    juce::File stepSample (int pad, const juce::File& current, int direction);
    /** Pad -> category used by kits. */
    int padCategory (int pad);

    /** Free CC0 library: file names and download source. */
    juce::StringArray freeLibraryFiles();
    juce::String freeLibraryBaseUrl();
    /** Download the free library into folder/Free Drums (background thread). progress 0..1, done(ok, message). */
    void downloadFreeLibrary (const juce::File& folder, std::function<void (float)> progress, std::function<void (bool, juce::String)> done);
}
