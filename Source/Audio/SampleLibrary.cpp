#include "SampleLibrary.h"
#include <juce_events/juce_events.h>

namespace SampleLib
{
    juce::StringArray catNames() { return { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Ride", "Crash", "Low Tom", "High Tom", "Rim", "Perc", "Shaker", "FX", "Sub", "Break" }; }

    // ------------------------------------------------------------------ classification
    static juce::StringArray tokens (const juce::String& name)
    {
        juce::StringArray out;
        juce::String cur;
        auto kind = [] (juce::juce_wchar c) { return juce::CharacterFunctions::isLetter (c) ? 1 : (juce::CharacterFunctions::isDigit (c) ? 2 : 0); };
        int last = 0;
        for (auto c : name.toLowerCase())
        {
            const int k = kind (c);
            if (k == 0 || (k != last && cur.isNotEmpty())) { if (cur.isNotEmpty()) out.add (cur); cur.clear(); }
            if (k != 0) cur << juce::String::charToString (c);
            last = k;
        }
        if (cur.isNotEmpty()) out.add (cur);
        return out;
    }

    int classify (const juce::String& fileName)
    {
        const auto t = tokens (juce::File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension());
        auto has = [&] (std::initializer_list<const char*> ws) { for (auto* w : ws) if (t.contains (w)) return true; return false; };
        auto firstIndex = [&] (std::initializer_list<const char*> ws) { int best = 1 << 20; for (auto* w : ws) { const int i = t.indexOf (w); if (i >= 0) best = juce::jmin (best, i); } return best; };

        if (has ({ "break", "breaks", "breakbeat", "amen" }) || (has ({ "loop" }) && has ({ "drum", "drums", "beat", "perc" }))) return Break;
        if (! t.isEmpty() && (t[0] == "bass" || t[0] == "sub" || (has ({ "808" }) && has ({ "bass", "sub" }) && ! has ({ "kick", "bd" })))) return Sub;

        struct Rule { int cat; std::initializer_list<const char*> words; };
        const Rule rules[] = {
            { Kick, { "kick", "kik", "kck", "bd", "bassdrum" } },
            { Snare, { "snare", "snr", "sd", "snares" } },
            { Clap, { "clap", "claps", "snap" } },
            { HatClosed, { "hat", "hats", "hh", "hihat", "chh", "ch", "ohh", "oh" } },
            { Ride, { "ride" } },
            { Crash, { "crash", "cymbal", "cym", "china", "splash" } },
            { TomLow, { "tom", "toms" } },
            { Rim, { "rim", "rimshot", "sidestick", "crossstick", "stick" } },
            { Shaker, { "shaker", "shake", "maracas", "maraca", "tambourine", "tamb", "cabasa" } },
            { Perc, { "perc", "conga", "bongo", "cowbell", "claves", "clave", "woodblock", "wood", "block", "darbuka", "frame", "log", "tabla", "agogo", "timbale", "cajon" } },
            { Fx, { "fx", "zap", "riser", "sweep", "noise", "glitch", "drop", "impact", "laser" } },
        };
        int bestCat = -1, bestIdx = 1 << 20;
        for (auto& r : rules)
        {
            const int i = firstIndex (r.words);
            if (i < bestIdx) { bestIdx = i; bestCat = r.cat; }
        }
        if (bestCat == HatClosed && has ({ "open", "oh", "ohh" })) return HatOpen;
        if (bestCat == TomLow && has ({ "high", "hi", "bright", "mid" }) && ! has ({ "low", "floor", "deep", "lo" })) return TomHigh;
        if (bestCat == Snare && has ({ "roll" })) return Fx;
        return bestCat;
    }

    // ------------------------------------------------------------------ folder
    static juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MANDELBROT").getChildFile ("sample_folder.txt");
    }

    static juce::File& savedFolder()
    {
        static juce::File f = [] { auto s = settingsFile(); return s.existsAsFile() ? juce::File (s.loadFileAsString().trim()) : juce::File(); }();
        return f;
    }

    juce::File defaultFolder()
    {
        if (savedFolder().isDirectory()) return savedFolder();
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        for (auto d : { exeDir.getChildFile ("Samples"), exeDir.getParentDirectory().getChildFile ("Samples"), exeDir.getParentDirectory().getParentDirectory().getChildFile ("Samples") })
            if (d.isDirectory()) return d;
        return exeDir.getChildFile ("Samples");
    }

    static Library& lib()
    {
        static Library l;
        static bool scanned = false;
        if (! scanned) { scanned = true; l.folder = defaultFolder(); rescan(); }
        return l;
    }

    void rescan()
    {
        static bool inRescan = false;
        if (inRescan) return;
        inRescan = true;
        auto& l = lib();
        inRescan = false;
        for (auto& a : l.files) a.clear();
        if (! l.folder.isDirectory()) return;
        for (const auto& e : juce::RangedDirectoryIterator (l.folder, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", juce::File::findFiles))
        {
            const int c = classify (e.getFile().getFileName());
            if (c >= 0) l.files[(size_t) c].add (e.getFile());
        }
        for (auto& a : l.files)
            std::sort (a.begin(), a.end(), [] (const juce::File& x, const juce::File& y) { return x.getFileName().compareNatural (y.getFileName()) < 0; });
    }

    void setFolder (const juce::File& f)
    {
        savedFolder() = f;
        settingsFile().getParentDirectory().createDirectory();
        settingsFile().replaceWithText (f.getFullPathName());
        lib().folder = f;
        rescan();
    }

    const Library& get() { return lib(); }

    // ------------------------------------------------------------------ kits
    juce::StringArray kitNames() { return { "Synth", "Studio", "808", "Trap", "TR-8", "Lo-fi Mix", "Random" }; }

    int padCategory (int pad)
    {
        static const int map[12] = { Kick, Snare, HatClosed, HatOpen, Clap, TomLow, TomHigh, Rim, Perc, Crash, Shaker, Sub };
        return map[juce::jlimit (0, 11, pad)];
    }

    static juce::File pick (int cat, const juce::String& kit, juce::uint32 seed, int pad)
    {
        const auto& all = lib().files[(size_t) cat];
        if (all.isEmpty()) return {};
        juce::StringArray want;
        if (kit == "Studio") want = { "studio", "acoustic", "live", "zildjian", "field", "rope" };
        else if (kit == "808") want = { "808", "x0x" };
        else if (kit == "Trap") want = { "trap" };
        else if (kit == "TR-8") want = { "tr8", "808" };
        juce::Array<juce::File> cands;
        for (auto& f : all)
        {
            const auto t = tokens (f.getFileNameWithoutExtension());
            bool ok = want.isEmpty();
            for (auto& w : want) ok = ok || t.contains (w);
            if (ok) cands.add (f);
        }
        if (cands.isEmpty()) cands = all;
        juce::Random r ((juce::int64) seed * 7919 + pad * 104729 + cat);
        if (kit == "Random" || kit == "Lo-fi Mix") return cands[r.nextInt (cands.size())];
        // stable choice: the plainest name first ("kick-808" before "kick-808-boom"), then the seed varies it
        std::sort (cands.begin(), cands.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().length() < b.getFileName().length(); });
        return cands[(int) (seed % (juce::uint32) juce::jmin (3, cands.size()))];
    }

    std::array<juce::File, 12> buildKit (const juce::String& kit, juce::uint32 seed)
    {
        std::array<juce::File, 12> out;
        if (kit == "Synth") return out;
        for (int p = 0; p < 12; ++p)
        {
            int cat = padCategory (p);
            auto f = pick (cat, kit, seed, p);
            if (f == juce::File() && cat == TomHigh) f = pick (TomLow, kit, seed + 1, p);
            if (f == juce::File() && cat == Crash) f = pick (Ride, kit, seed, p);
            if (f == juce::File() && cat == Shaker) f = pick (Perc, kit, seed + 3, p);
            out[(size_t) p] = f;
        }
        return out;
    }

    juce::File stepSample (int pad, const juce::File& current, int dir)
    {
        int cat = classify (current.getFileName());
        if (cat < 0) cat = padCategory (pad);
        const auto& all = lib().files[(size_t) cat];
        if (all.isEmpty()) return current;
        const int i = all.indexOf (current);
        return all[((i < 0 ? 0 : i + dir) % all.size() + all.size()) % all.size()];
    }

    // ------------------------------------------------------------------ free CC0 library
    juce::String freeLibraryBaseUrl()
    {
        // pinned revision of github.com/miskibin/synkopa (CC0 standard library)
        return "https://raw.githubusercontent.com/miskibin/synkopa/20e8a9a36cd5a5c8d40dedd2ebb3502870afacc4/crates/engine/library/";
    }

    juce::StringArray freeLibraryFiles()
    {
        return juce::StringArray::fromTokens (
            "bass-808-sub bass-808 bongo-high bongo-low clap-808 clap-group-2 clap-group clap-hand-soft clap-hand clap-snare clap-tr8 "
            "clap-trap-bright clap-trap-clean clap-trap-click clap-trap-crisp clap-trap-gritty clap-trap-hard clap-trap-layered clap-trap-punch "
            "clap-trap-sharp clap-trap-short clap-trap-thick clap-trap-tight clap-trap-warm claves-808 conga-808-high conga-808-low conga-808-mid "
            "cowbell-808 crash-choke crash-studio crossstick-studio cymbal-808-bright cymbal-808-short cymbal-808 darbuka-dum darbuka-tek egg-shaker "
            "finger-snap frame-drum-muted frame-drum hat-808-closed hat-808-open-long hat-808-open-short hat-808-open hat-808-pedal hat-open-choked "
            "hat-open-zildjian hat-studio-closed hat-studio-loose hat-studio-open hat-studio-openclose hat-studio-pedal hat-studio-soft hat-tr8-closed "
            "hat-tr8-open hat-trap-bright hat-trap-classic hat-trap-crisp hat-trap-cut hat-trap-metal hat-trap-open-bright hat-trap-open-short "
            "hat-trap-open-smooth hat-trap-punch hat-trap-sharp hat-trap-snap hat-trap-tick hat-trap-tight hat-x0x kick-808-boom kick-808-bright "
            "kick-808-click kick-808-deep kick-808-long kick-808-punch kick-808-short kick-808-tight kick-808 kick-cajon kick-studio-soft kick-studio "
            "kick-trap-boom kick-trap-bounce kick-trap-brick kick-trap-clean kick-trap-clip kick-trap-dark kick-trap-hard kick-trap-heavy kick-trap-knock-2 "
            "kick-trap-knock kick-trap-phonk kick-trap-punch kick-trap-rack kick-trap-stomp kick-trap-tight kick-trap-top kick-trap log-drum-high "
            "log-drum-low maracas-808 ride-bell-studio ride-studio rimshot-808 rimshot-studio rimshot-tight shaker-small shaker sidestick-studio "
            "snare-808-bright snare-808-dark snare-808-low snare-808-snappy snare-808-soft snare-808-tight snare-808 snare-field snare-roll snare-rope "
            "snare-studio-2 snare-studio-3 snare-studio-off snare-studio snare-tr8 snare-trap-clap snare-trap-classic snare-trap-clean snare-trap-crisp "
            "snare-trap-dirty snare-trap-hard snare-trap-layered snare-trap-low snare-trap-punch snare-trap-rage snare-trap-round snare-trap-sharp "
            "snare-trap-smack snare-trap-snap snare-trap-tight snare-trap sub-drop tambourine-hit tom-808-bright tom-808-deep tom-808-high tom-808-low "
            "tom-808-mid tom-studio-floor tom-studio-high tom-studio-low tom-studio-rim woodblock zap", " ", "");
    }

    void downloadFreeLibrary (const juce::File& folder, std::function<void (float)> progress, std::function<void (bool, juce::String)> done)
    {
        juce::Thread::launch ([folder, progress, done]
        {
            const auto dest = folder.getChildFile ("Free Drums (CC0)");
            dest.createDirectory();
            const auto names = freeLibraryFiles();
            int ok = 0, failed = 0;
            for (int i = 0; i < names.size(); ++i)
            {
                const auto out = dest.getChildFile (names[i] + ".flac");
                if (out.existsAsFile() && out.getSize() > 0) { ++ok; continue; }
                juce::URL url (freeLibraryBaseUrl() + names[i] + ".flac");
                auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (15000));
                juce::MemoryBlock mb;
                if (stream != nullptr && stream->readIntoMemoryBlock (mb) > 100 && out.replaceWithData (mb.getData(), mb.getSize())) ++ok;
                else ++failed;
                if (progress) juce::MessageManager::callAsync ([progress, p = (float) (i + 1) / (float) names.size()] { progress (p); });
                if (failed > 8 && ok == 0) break;
            }
            dest.getChildFile ("CREDITS.txt").replaceWithText (
                "Free drum samples (CC0 / public domain) from the Synkopa standard library: https://github.com/miskibin/synkopa\n"
                "Sources: Versilian Community Sample Library, TidalCycles TR-808 (Michael Fischer), MckAudio TR-8 recordings, Freesound CC0 uploads.\n");
            juce::MessageManager::callAsync ([done, ok, failed, dest]
            {
                if (done) done (ok > 0, ok > 0 ? "Downloaded " + juce::String (ok) + " samples to " + dest.getFullPathName() + (failed > 0 ? " (" + juce::String (failed) + " failed)" : juce::String())
                                               : "Download failed - check your internet connection");
            });
        });
    }
}
