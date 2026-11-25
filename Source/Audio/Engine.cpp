#include "Engine.h"
#include "../Fractal/MandelMath.h"

// ============================================================================ ChannelState
void ChannelState::readFrom (const juce::ValueTree& v)
{
    volDb.store ((float) v.getProperty (IDs::volume, 0.0f));
    pan.store ((float) v.getProperty (IDs::pan, 0.0f));
    mute.store ((bool) v.getProperty (IDs::mute, false));
    solo.store ((bool) v.getProperty (IDs::solo, false));
    for (int i = 0; i < MixP::numSends; ++i)
        sends[i].store ((float) v.getProperty (juce::Identifier ("send" + juce::String (i)), 0.0f));
}

void ChannelState::meter (const float* L, const float* R, int n)
{
    float a = 0, b = 0;
    for (int i = 0; i < n; ++i) { a = std::max (a, std::abs (L[i])); b = std::max (b, std::abs (R[i])); }
    if (a > peakL.load (std::memory_order_relaxed)) peakL.store (a, std::memory_order_relaxed);
    if (b > peakR.load (std::memory_order_relaxed)) peakR.store (b, std::memory_order_relaxed);
}

static void syncInserts (InsertChain& chain, const juce::ValueTree& insertsNode)
{
    for (int i = 0; i < MixP::numInserts && i < insertsNode.getNumChildren(); ++i)
    {
        auto s = insertsNode.getChild (i);
        chain.slots[i].type.store ((int) s.getProperty (IDs::fx, 0));
        chain.slots[i].bypass.store ((bool) s.getProperty (IDs::bypass, false));
        for (int k = 0; k < 4; ++k)
            chain.slots[i].params[k].store ((float) s.getProperty (juce::Identifier ("p" + juce::String (k)), 0.5f));
    }
}

// ============================================================================ synth sources
std::shared_ptr<WavetableData> wavetableFromSample (const SampleData& d)
{
    constexpr int size = 2048;
    auto wt = std::make_shared<WavetableData>();
    wt->size = size;
    const int n = d.numFrames();
    wt->frames = juce::jlimit (1, 64, n / size);
    wt->data.assign ((size_t) (wt->frames * size), 0.0f);
    const int hop = wt->frames > 1 ? (n - size) / (wt->frames - 1) : 0;
    for (int f = 0; f < wt->frames; ++f)
    {
        float* dst = wt->data.data() + (size_t) (f * size);
        float peak = 0.0f, mean = 0.0f;
        for (int i = 0; i < size; ++i)
        {
            const double src = n >= size ? (double) (f * hop + i) : (double) i * n / size;
            dst[i] = d.get (0, juce::jlimit (0, n - 1, (int) src));
            mean += dst[i];
        }
        mean /= size;
        for (int i = 0; i < size; ++i)
        {
            // remove DC and cross-fade the frame edges so the cycle loops seamlessly
            dst[i] -= mean;
            const float edge = juce::jmin (1.0f, juce::jmin ((float) i, (float) (size - 1 - i)) / 32.0f);
            dst[i] *= edge;
            peak = juce::jmax (peak, std::abs (dst[i]));
        }
        if (peak > 0) for (int i = 0; i < size; ++i) dst[i] /= peak;
    }
    return wt;
}

void applySynthSources (SynthEngine& synth, const juce::ValueTree& s)
{
    const auto wtFile = s.getProperty ("wtFile", "").toString();
    std::shared_ptr<WavetableData> wt;
    if (wtFile.isNotEmpty())
        if (auto d = AudioPool::get().load (juce::File (wtFile))) wt = wavetableFromSample (*d);
    if (wt == nullptr && s.hasProperty ("wtCx"))
    {
        wt = std::make_shared<WavetableData>();
        wt->frames = 16; wt->size = 2048;
        wt->data = MandelMath::buildWavetable ((double) s["wtCx"], (double) s["wtCy"], wt->frames, wt->size);
    }
    if (wt != nullptr) synth.setWavetable (wt);
    const auto gFile = s.getProperty ("grainFile", "").toString();
    synth.setGrainSource (gFile.isNotEmpty() ? AudioPool::get().load (juce::File (gFile)) : nullptr);
}

// ============================================================================ TrackProcessor
TrackProcessor::TrackProcessor (int trackId, const juce::String& t) : id (trackId), type (t)
{
    if (type == "synth") synth = std::make_unique<SynthEngine>();
    if (type == "drums") drums = std::make_unique<DrumEngine>();
    scratch.reserve (2048);
}

void TrackProcessor::prepare (double sr, int block)
{
    buf.setSize (2, block);
    buf.clear();
    inserts.prepare (sr, block);
    if (synth) synth->prepare (sr, block);
    if (drums) drums->prepare (sr, block);
    ch.gL.setTime (0.02f, (float) sr); ch.gR.setTime (0.02f, (float) sr);
    held.fill (0); suppressed.fill (0);
}

void TrackProcessor::syncFrom (const juce::ValueTree& track)
{
    ch.readFrom (track);
    armed.store ((bool) track.getProperty (IDs::arm, false));
    livePart.store ((int) track.getProperty (IDs::livePart, 0));
    syncInserts (inserts, track.getChildWithName (IDs::INSERTS));
    if (synth)
    {
        auto s = track.getChildWithName (IDs::SYNTH);
        for (int i = 0; i < SynthP::NumParams; ++i)
            synth->params[i].store ((float) s.getProperty (SynthP::defs()[(size_t) i].id, SynthP::defs()[(size_t) i].defV));
        if (s.hasProperty ("wtFile") || s.hasProperty ("wtCx") || s.hasProperty ("grainFile")) applySynthSources (*synth, s);
    }
}

// ============================================================================ Engine
Engine::Engine()
{
    writerThread.startThread();
    midiSender.startThread();
    recordingFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MANDELBROT").getChildFile ("Recordings");
    masterBuf.setSize (2, chunk);
    inputBuf.setSize (2, chunk);
    prepareAll (sampleRate);
    startTimerHz (30);
}

Engine::~Engine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    for (auto& d : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback (d.identifier, this);
    deviceManager.closeAudioDevice();
    if (project.isValid()) project.removeListener (this);
    recordWriter.store (nullptr);
    recordOwner.reset();
    writerThread.stopThread (2000);
    midiSender.stopThread (1000);
    DeferredDeleter::get().flushAll();
}

void Engine::initialiseAudio (const juce::String& savedXml)
{
    std::unique_ptr<juce::XmlElement> xml;
    if (savedXml.isNotEmpty()) xml = juce::parseXML (savedXml);
    auto err = deviceManager.initialise (2, 2, xml.get(), true);
    if (err.isNotEmpty()) DBG ("Audio init: " << err);
    deviceManager.addAudioCallback (this);
    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
        deviceManager.addMidiInputDeviceCallback (d.identifier, this);
    }
}

void Engine::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    // drop old processors so everything is resynced
    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        for (auto& [id, tp] : trackProcs) DeferredDeleter::get().retire (tp);
        trackProcs.clear();
        for (auto& b : busProcs) DeferredDeleter::get().retire (b);
        busProcs.clear();
        auto old = currentOwner;
        currentOwner.reset();
        current.store (nullptr);
        DeferredDeleter::get().retire (old);
    }
    auto m = Model::master (project);
    masterCh.readFrom (m);
    syncInserts (masterInserts, m.getChildWithName (IDs::INSERTS));
    metronomeOn.store ((bool) project.getProperty (IDs::metronome, false));
    patClipId = -1;
    liveOn.store ((bool) project.getChildWithName (IDs::LIVE).getProperty (IDs::liveOn, false));
    rebuildNow();
}

void Engine::updateLoop()
{
    if (! project.isValid()) return;
    const bool songMode = (bool) project.getProperty (IDs::songMode, true);
    const double selA = project.getProperty (IDs::loopStart, 0.0), selB = project.getProperty (IDs::loopEnd, 0.0);
    const bool selection = (bool) project.getProperty (IDs::loopOn, false) && selB > selA + 0.01;
    bool active = false; double a = 0.0, b = 0.0;
    juce::ValueTree patClip;
    if (! songMode && patClipId >= 0)
        for (auto t : Model::tracks (project))
            for (auto c : t.getChildWithName (IDs::CLIPS))
                if ((int) c[IDs::id] == patClipId) patClip = c;
    if (patClip.isValid())                                                                        // PAT: loop the selected clip
    { active = true; a = patClip[IDs::start]; b = a + juce::jmax (0.25, (double) patClip[IDs::length]); }
    else if (selection) { active = true; a = selA; b = selB; }                                     // timeline selection
    else if (songMode && ! liveOn.load())
    {
        // SONG: play the arrangement and loop back to bar 1 at the end of the last clip
        const double bpb = juce::jmax (1, (int) project.getProperty (IDs::tsNum, 4)) * 4.0 / juce::jmax (1, (int) project.getProperty (IDs::tsDen, 4));
        const double end = std::ceil (Model::projectEndBeat (project) / bpb - 1e-9) * bpb;
        if (end > 0.0) { active = true; a = 0.0; b = end; }
    }
    loopA.store (a); loopB.store (b); loopActive.store (active);
}

// ---------------------------------------------------------------------------- MIDI out
void Engine::MidiOutSender::push (int status, int d1, int d2)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 + n2 == 0) return;
    data[(size_t) (n1 > 0 ? s1 : s2)] = (juce::uint32) ((status & 0xff) | ((d1 & 0x7f) << 8) | ((d2 & 0x7f) << 16));
    fifo.finishedWrite (1);
}

void Engine::MidiOutSender::run()
{
    while (! threadShouldExit())
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        if (n1 + n2 > 0)
        {
            const juce::ScopedLock sl (lock);
            auto send = [this] (juce::uint32 m) { if (out != nullptr) out->sendMessageNow (juce::MidiMessage ((int) (m & 0xff), (int) ((m >> 8) & 0x7f), (int) ((m >> 16) & 0x7f))); };
            for (int i = 0; i < n1; ++i) send (data[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) send (data[(size_t) (s2 + i)]);
        }
        fifo.finishedRead (n1 + n2);
        wait (1);
    }
}

bool Engine::setMidiOutput (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiOutput> dev;
    if (identifier.isNotEmpty()) dev = juce::MidiOutput::openDevice (identifier);
    const juce::ScopedLock sl (midiSender.lock);
    if (midiSender.out != nullptr)
        for (int ch = 1; ch <= 16; ++ch) midiSender.out->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
    midiSender.out = std::move (dev);
    return identifier.isEmpty() || midiSender.out != nullptr;
}

juce::String Engine::getMidiOutputId() const
{
    const juce::ScopedLock sl (midiSender.lock);
    return midiSender.out != nullptr ? midiSender.out->getIdentifier() : juce::String();
}

void Engine::prepareAll (double sr)
{
    sampleRate = sr;
    for (auto& [id, tp] : trackProcs) tp->prepare (sr, chunk);
    for (auto& b : busProcs) b->prepare (sr, chunk);
    masterInserts.prepare (sr, chunk);
    masterCh.gL.setTime (0.02f, (float) sr); masterCh.gR.setTime (0.02f, (float) sr);
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::SpinLock::ScopedLockType sl (renderLock);
    prepareAll (device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 44100.0);
}

void Engine::audioDeviceStopped() {}

TrackProcessor* Engine::getTrackProcessor (int trackId)
{
    auto it = trackProcs.find (trackId);
    return it == trackProcs.end() ? nullptr : it->second.get();
}

BusProcessor* Engine::getBusProcessor (int index)
{
    return juce::isPositiveAndBelow (index, (int) busProcs.size()) ? busProcs[(size_t) index].get() : nullptr;
}

// ---------------------------------------------------------------------------- param mirroring
void Engine::applyPadProperty (TrackProcessor& tp, const juce::ValueTree& pad, const juce::Identifier& prop)
{
    if (! tp.drums) return;
    const int idx = (int) pad[IDs::id];
    if (prop == IDs::sample || prop == IDs::synthKind)
    {
        const auto path = pad[IDs::sample].toString();
        std::shared_ptr<const SampleData> data;
        if (path.isNotEmpty()) data = AudioPool::get().load (juce::File (path));
        if (data == nullptr) data = DrumEngine::synthesiseDrum ((int) pad.getProperty (IDs::synthKind, idx));
        tp.drums->setPadSample (idx, data);
        return;
    }
    const int pi = PadP::indexOf (prop.toString());
    if (pi >= 0) tp.drums->pad (idx).params[pi].store ((float) pad[prop]);
}

void Engine::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    const auto type = tree.getType();
    if (type == IDs::SYNTH)
    {
        if (auto* tp = getTrackProcessor ((int) tree.getParent()[IDs::id]))
            if (tp->synth)
            {
                const int idx = SynthP::indexOf (prop.toString());
                if (idx >= 0) tp->synth->params[idx].store ((float) tree[prop]);
                else if (prop.toString() == "wtFile" || prop.toString() == "wtCy" || prop.toString() == "grainFile") applySynthSources (*tp->synth, tree);
            }
        return;
    }
    if (type == IDs::PAD)
    {
        if (auto* tp = getTrackProcessor ((int) tree.getParent().getParent()[IDs::id]))
            applyPadProperty (*tp, tree, prop);
        return;
    }
    if (type == IDs::SLOT)
    {
        auto ins = tree.getParent();
        auto owner = ins.getParent();
        const int slot = ins.indexOf (tree);
        InsertChain* chain = nullptr;
        if (owner.hasType (IDs::TRACK)) { if (auto* tp = getTrackProcessor ((int) owner[IDs::id])) chain = &tp->inserts; }
        else if (owner.hasType (IDs::BUS)) { if (auto* bp = getBusProcessor (owner.getParent().indexOf (owner))) chain = &bp->inserts; }
        else if (owner.hasType (IDs::MASTER)) chain = &masterInserts;
        if (chain != nullptr && juce::isPositiveAndBelow (slot, MixP::numInserts))
        {
            auto& s = chain->slots[slot];
            if (prop == IDs::fx) s.type.store ((int) tree[prop]);
            else if (prop == IDs::bypass) s.bypass.store ((bool) tree[prop]);
            else
            {
                const auto name = prop.toString();
                if (name.length() == 2 && name[0] == 'p') s.params[juce::jlimit (0, 3, name.getTrailingIntValue())].store ((float) tree[prop]);
            }
        }
        return;
    }
    const bool channelProp = prop == IDs::volume || prop == IDs::pan || prop == IDs::mute || prop == IDs::solo
                          || prop.toString().startsWith ("send");
    if (type == IDs::TRACK)
    {
        if (auto* tp = getTrackProcessor ((int) tree[IDs::id]))
        {
            if (channelProp) { tp->ch.readFrom (tree); return; }
            if (prop == IDs::arm) { tp->armed.store ((bool) tree[prop]); return; }
            if (prop == IDs::livePart) { tp->livePart.store ((int) tree[prop]); return; }
        }
        if (prop == IDs::name || prop == IDs::colour || prop == IDs::height || prop == IDs::showAuto) return;
    }
    if (type == IDs::BUS && channelProp)
    {
        if (auto* bp = getBusProcessor (tree.getParent().indexOf (tree))) bp->ch.readFrom (tree);
        return;
    }
    if (type == IDs::MASTER) { masterCh.readFrom (tree); return; }
    if (type == IDs::PROJECT && prop == IDs::metronome) { metronomeOn.store ((bool) tree[prop]); return; }
    if (type == IDs::PROJECT && (prop == IDs::songMode || prop == IDs::loopOn || prop == IDs::loopStart || prop == IDs::loopEnd)) { updateLoop(); return; }
    if (type == IDs::LIVE) { if (prop == IDs::liveOn) { liveOn.store ((bool) tree[prop]); updateLoop(); } return; }
    if (type == IDs::LPART) return;
    if (type == IDs::FRACTAL || type == IDs::GEN || type == IDs::BOOKMARK || type == IDs::PATH || type == IDs::KEY) return;
    if (type == IDs::PROJECT && prop == IDs::name) return;
    if (type == IDs::CLIP && prop == IDs::name) return;
    dirty = true;
}

void Engine::timerCallback()
{
    if (dirty.exchange (false)) rebuildNow();
    for (auto& [id, tp] : trackProcs) tp->ch.publish();
    for (auto& b : busProcs) b->ch.publish();
    masterCh.publish();
}

// ---------------------------------------------------------------------------- snapshot build
static void parseAutomation (const juce::ValueTree& track, std::vector<Snap::AutoLane>& out)
{
    for (auto lane : track.getChildWithName (IDs::AUTOMATION))
    {
        if (lane.getNumChildren() == 0) continue;
        Snap::AutoLane a;
        const auto id = lane[IDs::param].toString();
        if (id == "vol") a.kind = Snap::AutoVol;
        else if (id == "pan") a.kind = Snap::AutoPan;
        else if (id.startsWith ("send")) { a.kind = Snap::AutoSend; a.index = juce::jlimit (0, MixP::numSends - 1, id.getTrailingIntValue()); }
        else if (id.startsWith ("fx")) { a.kind = Snap::AutoFx; a.index = juce::jlimit (0, MixP::numInserts - 1, id.substring (2, 3).getIntValue()) * 4 + juce::jlimit (0, 3, id.getTrailingIntValue()); }
        else if (id.startsWith ("synth.")) { a.kind = Snap::AutoSynth; a.index = SynthP::indexOf (id.fromFirstOccurrenceOf (".", false, false)); if (a.index < 0) continue; }
        else continue;
        for (auto pt : lane) a.points.push_back ({ (double) pt[IDs::t], (float) pt[IDs::val] });
        std::sort (a.points.begin(), a.points.end(), [] (auto& x, auto& y) { return x.first < y.first; });
        out.push_back (std::move (a));
    }
}

void Engine::rebuildNow()
{
    if (! project.isValid()) return;
    auto snap = std::make_shared<Snap::Snapshot>();
    snap->tempo = juce::jlimit (20.0, 400.0, (double) project.getProperty (IDs::tempo, 120.0));
    snap->tsNum = juce::jmax (1, (int) project.getProperty (IDs::tsNum, 4));
    snap->loopOn = (bool) project.getProperty (IDs::loopOn, false);
    snap->loopStart = project.getProperty (IDs::loopStart, 0.0);
    snap->loopEnd = project.getProperty (IDs::loopEnd, 16.0);
    snap->endBeat = Model::projectEndBeat (project);

    // ---- buses
    auto busesNode = Model::buses (project);
    while ((int) busProcs.size() > busesNode.getNumChildren())
    {
        DeferredDeleter::get().retire (busProcs.back());
        busProcs.pop_back();
    }
    while ((int) busProcs.size() < busesNode.getNumChildren())
    {
        auto bp = std::make_shared<BusProcessor> ((int) busProcs.size());
        bp->prepare (sampleRate, chunk);
        auto node = busesNode.getChild ((int) busProcs.size());
        bp->ch.readFrom (node);
        syncInserts (bp->inserts, node.getChildWithName (IDs::INSERTS));
        busProcs.push_back (bp);
    }
    for (auto& b : busProcs) snap->buses.push_back (b.get());

    // ---- tracks
    std::map<int, std::shared_ptr<TrackProcessor>> keep;
    for (auto track : Model::tracks (project))
    {
        const int id = track[IDs::id];
        const auto type = track[IDs::type].toString();
        std::shared_ptr<TrackProcessor> tp;
        if (auto it = trackProcs.find (id); it != trackProcs.end() && it->second->type == type) tp = it->second;
        else
        {
            tp = std::make_shared<TrackProcessor> (id, type);
            tp->prepare (sampleRate, chunk);
            tp->syncFrom (track);
            if (tp->drums)
                for (auto pad : track.getChildWithName (IDs::DRUMKIT))
                {
                    applyPadProperty (*tp, pad, IDs::sample);
                    for (auto& d : PadP::defs()) applyPadProperty (*tp, pad, juce::Identifier (d.id));
                }
        }
        keep[id] = tp;

        Snap::Track st;
        st.id = id;
        st.proc = tp.get();
        st.output = (int) track.getProperty (IDs::output, -1);
        if (st.output >= (int) busProcs.size()) st.output = -1;
        st.events = Snap::eventsForTrack (track);
        for (auto clip : track.getChildWithName (IDs::CLIPS))
        {
            if (clip[IDs::type].toString() != "audio") continue;
            auto data = AudioPool::get().load (juce::File (clip[IDs::file].toString()));
            if (data == nullptr) continue;
            snap->keepAlive.push_back (data);
            Snap::AudioRegion r;
            r.startBeat = clip[IDs::start];
            r.endBeat = r.startBeat + (double) clip[IDs::length];
            r.sourceOffsetSec = clip.getProperty (IDs::offset, 0.0);
            r.data = data.get();
            r.gain = juce::Decibels::decibelsToGain ((float) clip.getProperty (IDs::gain, 0.0f));
            r.fadeInBeats = clip.getProperty (IDs::fadeIn, 0.0);
            r.fadeOutBeats = clip.getProperty (IDs::fadeOut, 0.0);
            st.regions.push_back (r);
        }
        parseAutomation (track, st.automation);
        snap->tracks.push_back (std::move (st));
    }
    for (auto& [id, tp] : trackProcs)
        if (keep.find (id) == keep.end() || keep[id] != tp)
            DeferredDeleter::get().retire (tp);
    trackProcs = std::move (keep);

    auto old = currentOwner;
    currentOwner = snap;
    current.store (snap.get(), std::memory_order_release);
    DeferredDeleter::get().retire (old);
    updateLoop();
}

// ---------------------------------------------------------------------------- transport
void Engine::play()
{
    if (playing.load()) return;
    // FL style: in a looped range, start inside it
    const double pos = positionBeat.load();
    if (loopActive.load() && (pos < loopA.load() || pos >= loopB.load())) setPosition (loopA.load());
    playStartBeat.store (positionBeat.load());
    if (recordEnabled.load()) startRecordingWriter();
    playing.store (true);
}

void Engine::stop()
{
    if (! playing.load())
    {
        // stop while stopped: back to the start (of the loop, else bar 1)
        setPosition (loopActive.load() ? loopA.load() : 0.0);
        return;
    }
    playing.store (false);
    finishRecording();
    setPosition (playStartBeat.load());   // FL style: stop returns to where playback started
}

void Engine::pause()
{
    if (! playing.load()) return;
    playing.store (false);
    finishRecording();
}

void Engine::setPosition (double beat)
{
    beat = juce::jmax (0.0, beat);
    requestedPosition.store (beat);
    positionBeat.store (beat);
}

void Engine::setRecordEnabled (bool r)
{
    recordEnabled.store (r);
    if (! r) finishRecording();
    else if (playing.load()) startRecordingWriter();
}

void Engine::startRecordingWriter()
{
    if (recordWriter.load() != nullptr || ! project.isValid()) return;
    recordTrackId = -1;
    for (auto t : Model::tracks (project))
        if ((bool) t[IDs::arm] && t[IDs::type].toString() == "audio") { recordTrackId = t[IDs::id]; break; }
    if (recordTrackId < 0) return;
    recordingFolder.createDirectory();
    recordFile = recordingFolder.getNonexistentChildFile ("rec_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"), ".wav");
    auto w = makeWavWriter (recordFile, sampleRate, 2, 24);
    if (w == nullptr) return;
    recordOwner = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 65536);
    recordStartBeat.store (positionBeat.load());
    recordedSamples.store (0);
    recordWriter.store (recordOwner.get());
}

void Engine::finishRecording()
{
    if (recordWriter.exchange (nullptr) == nullptr) return;
    juce::Thread::sleep (60); // let any in-flight callback finish using the pointer
    recordOwner.reset();      // flushes & closes the file
    const double secs = recordedSamples.load() / sampleRate;
    const double tempo = project.getProperty (IDs::tempo, 120.0);
    AudioPool::get().invalidate (recordFile);
    if (onRecordingFinished && secs > 0.05)
        onRecordingFinished (recordTrackId, recordFile, recordStartBeat.load(), secs * tempo / 60.0);
}

// ---------------------------------------------------------------------------- live input
void Engine::sendLiveNote (int trackId, int note, float velocity)
{
    static juce::SpinLock producerLock;
    const juce::SpinLock::ScopedLockType sl (producerLock);
    int s1, n1, s2, n2;
    liveFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) liveData[(size_t) s1] = { trackId, note, velocity };
    else if (n2 > 0) liveData[(size_t) s2] = { trackId, note, velocity };
    liveFifo.finishedWrite (n1 + n2);
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const int t = liveTrackId.load();
    if (t < 0) return;
    if (m.isNoteOn()) sendLiveNote (t, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) sendLiveNote (t, m.getNoteNumber(), 0.0f);
}

void Engine::previewSample (std::shared_ptr<const SampleData> d, juce::int64 start, juce::int64 end)
{
    previewData.store (nullptr);
    auto old = previewOwner;
    previewOwner = d;
    DeferredDeleter::get().retire (std::const_pointer_cast<SampleData> (old));
    if (d == nullptr) { previewPos.store (-1.0); return; }
    previewEnd.store (end < 0 ? d->numFrames() : juce::jmin<juce::int64> (end, d->numFrames()));
    previewPos.store ((double) juce::jlimit<juce::int64> (0, d->numFrames() - 1, start));
    previewData.store (d.get());
}

void Engine::stopPreview() { previewData.store (nullptr); previewPos.store (-1.0); }

// ---------------------------------------------------------------------------- audio callback
void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    const juce::SpinLock::ScopedTryLockType tl (renderLock);
    if (! tl.isLocked() || numOut < 1)
    {
        for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear (out[c], numSamples);
        return;
    }
    const double req = requestedPosition.exchange (-1.0);
    if (req >= 0.0) { rtTransport.beat = req; rtTransport.flush = true; } // forces note-offs
    rtTransport.playing = playing.load();

    float* L = out[0];
    float* R = numOut > 1 ? out[1] : nullptr;
    for (int pos = 0; pos < numSamples; pos += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - pos);
        float* l = L + pos;
        float* r = R != nullptr ? R + pos : masterBuf.getWritePointer (1); // mono device fallback
        const float* il = numIn > 0 && in[0] != nullptr ? in[0] + pos : nullptr;
        const float* ir = numIn > 1 && in[1] != nullptr ? in[1] + pos : il;
        processBlock (l, r, il, ir, n, rtTransport, -1, false);
        if (auto* w = recordWriter.load (std::memory_order_acquire); w != nullptr && rtTransport.playing && il != nullptr)
        {
            const float* chans[2] = { il, ir };
            w->write (chans, n);
            recordedSamples.fetch_add (n);
        }
        for (int i = 0; i < n; ++i)
        {
            l[i] = juce::jlimit (-1.0f, 1.0f, l[i]);
            if (R != nullptr) r[i] = juce::jlimit (-1.0f, 1.0f, r[i]);
        }
    }
    for (int c = 2; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear (out[c], numSamples);
    positionBeat.store (rtTransport.beat);
}

void Engine::processBlock (float* L, float* R, const float* inL, const float* inR, int n, Transport& tr, int onlyTrack, bool offline)
{
    juce::FloatVectorOperations::clear (L, n);
    juce::FloatVectorOperations::clear (R, n);
    auto* s = current.load (std::memory_order_acquire);
    if (s == nullptr) return;

    const double bps = s->tempo / 60.0 / sampleRate;  // beats per sample
    const bool stopping = (tr.wasPlaying && ! tr.playing) || tr.flush;
    tr.flush = false;

    // ---- transport segments (loop wrap)
    struct Seg { int off, len; double b0; bool wrapAfter; };
    Seg segs[4]; int numSegs = 0;
    double beat = tr.beat;
    if (tr.playing)
    {
        int off = 0;
        while (off < n && numSegs < 4)
        {
            int len = n - off; bool wrap = false;
            const double la = loopA.load (std::memory_order_relaxed), lb = loopB.load (std::memory_order_relaxed);
            const bool looping = ! offline && loopActive.load (std::memory_order_relaxed) && lb > la + 0.01;
            if (looping && beat >= lb) beat = la;    // e.g. loop shortened behind the playhead
            if (looping && beat < lb && beat + len * bps >= lb)
            {
                len = juce::jlimit (1, n - off, (int) std::ceil ((lb - beat) / bps));
                wrap = true;
            }
            segs[numSegs++] = { off, len, beat, wrap };
            off += len;
            beat += len * bps;
            if (wrap) beat = la + juce::jmax (0.0, beat - lb);
        }
    }

    // ---- live notes
    LiveNote liveNotes[64]; int numLive = 0;
    if (! offline)
    {
        int s1, n1, s2, n2;
        liveFifo.prepareToRead (64, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) liveNotes[numLive++] = liveData[(size_t) (s1 + i)];
        for (int i = 0; i < n2; ++i) liveNotes[numLive++] = liveData[(size_t) (s2 + i)];
        liveFifo.finishedRead (n1 + n2);
    }

    // ---- live generative parts
    bool liveRan = false;
    if (! offline)
    {
        if (tr.playing && ! tr.wasPlaying) live.alignTo (tr.beat);
        if (tr.playing && ! stopping && liveOn.load (std::memory_order_relaxed))
        {
            const double bpb = juce::jmax (1, s->tsNum) * 1.0;
            live.process (n, bps, (int) bpb);
        }
        else live.flush();
        liveRan = true;
        for (int p = 1; p < Live::NumParts; ++p)
        {
            const int ch = juce::jlimit (1, 16, live.channelFor (p)) - 1;
            for (int i = 0; i < live.eventCount[p]; ++i)
            {
                const auto& e = live.events[p][i];
                if (e.velocity > 0.0f) midiSender.push (0x90 | ch, e.note, juce::jlimit (1, 127, (int) std::round (e.velocity * 127.0f)));
                else midiSender.push (0x80 | ch, e.note, 0);
            }
        }
    }

    bool anySolo = false;
    for (auto& t : s->tracks) if (t.proc->ch.solo.load (std::memory_order_relaxed)) { anySolo = true; break; }

    for (auto* b : s->buses) b->buf.clear (0, n);
    masterBuf.clear (0, n);
    float* mL = masterBuf.getWritePointer (0);
    float* mR = masterBuf.getWritePointer (1);

    const FxContext fxc { s->tempo, tr.beat, tr.playing };

    for (auto& t : s->tracks)
    {
        auto& tp = *t.proc;
        if (onlyTrack >= 0 && t.id != onlyTrack) continue;
        tp.buf.clear (0, n);
        float* tL = tp.buf.getWritePointer (0);
        float* tR = tp.buf.getWritePointer (1);
        tp.scratch.clear();
        auto push = [&tp] (int off, int note, float vel) { if (tp.scratch.size() < tp.scratch.capacity()) tp.scratch.push_back ({ off, note, vel }); };

        // note-offs on stop / reposition
        if (stopping)
        {
            for (int k = 0; k < 128; ++k) if (tp.held[(size_t) k]) push (0, k, 0.0f);
            tp.held.fill (0); tp.suppressed.fill (0);
        }

        for (int i = 0; i < numLive; ++i)
            if (liveNotes[i].track == t.id) push (0, liveNotes[i].note, liveNotes[i].vel);
        if (liveRan)
            if (const int lp = tp.livePart.load (std::memory_order_relaxed); lp > 0 && lp < Live::NumParts)
                for (int i = 0; i < live.eventCount[lp]; ++i)
                    push (live.events[lp][i].offset, live.events[lp][i].note, live.events[lp][i].velocity);

        // ---- clip events
        for (int si = 0; si < numSegs; ++si)
        {
            const auto& sg = segs[si];
            const double b1 = sg.b0 + sg.len * bps;
            auto it = std::lower_bound (t.events.begin(), t.events.end(), sg.b0, [] (const Snap::Event& e, double b) { return e.beat < b; });
            for (; it != t.events.end() && it->beat < b1; ++it)
            {
                const int off = sg.off + juce::jlimit (0, sg.len - 1, (int) ((it->beat - sg.b0) / bps));
                const int note = juce::jlimit (0, 127, it->note);
                if (it->velocity > 0.0f)
                {
                    if (it->probability < 0.999f)
                    {
                        const auto h = DSP::hash32 (it->key ^ (tr.loopPass * 2654435761u) ^ 0x5bd1e995u);
                        if ((float) (h >> 8) / 16777216.0f >= it->probability) { tp.suppressed[(size_t) note]++; continue; }
                    }
                    tp.held[(size_t) note]++;
                    push (off, note, it->velocity);
                }
                else
                {
                    if (tp.suppressed[(size_t) note] > 0) { tp.suppressed[(size_t) note]--; continue; }
                    if (tp.held[(size_t) note] > 0) { tp.held[(size_t) note]--; push (off, note, 0.0f); }
                }
            }
            if (sg.wrapAfter)
            {
                for (int k = 0; k < 128; ++k) if (tp.held[(size_t) k]) push (sg.off + sg.len - 1, k, 0.0f);
                tp.held.fill (0); tp.suppressed.fill (0);
            }
        }
        // events must be time ordered for the instruments
        // insertion sort: stable and allocation-free (std::stable_sort may allocate on the audio thread)
        for (size_t i = 1; i < tp.scratch.size(); ++i)
        {
            const auto ev = tp.scratch[i];
            size_t j = i;
            while (j > 0 && tp.scratch[j - 1].offset > ev.offset) { tp.scratch[j] = tp.scratch[j - 1]; --j; }
            tp.scratch[j] = ev;
        }

        // ---- automation (block rate)
        float volDb = tp.ch.volDb.load (std::memory_order_relaxed);
        float pan = tp.ch.pan.load (std::memory_order_relaxed);
        float sends[MixP::numSends];
        for (int k = 0; k < MixP::numSends; ++k) sends[k] = tp.ch.sends[k].load (std::memory_order_relaxed);
        for (auto& row : tp.fxOverrides) for (auto& v : row) v = -1.0f;
        bool synthOv = false;
        if (tr.playing && ! t.automation.empty())
        {
            for (auto& a : t.automation)
            {
                const float v = a.valueAt (tr.beat);
                switch (a.kind)
                {
                    case Snap::AutoVol: volDb = -60.0f + v * 66.0f; break;
                    case Snap::AutoPan: pan = v * 2.0f - 1.0f; break;
                    case Snap::AutoSend: sends[a.index] = v; break;
                    case Snap::AutoFx: tp.fxOverrides[a.index / 4][a.index % 4] = v; break;
                    case Snap::AutoSynth:
                        if (! synthOv) { for (auto& o : tp.synthOverrides) o = -1e30f; synthOv = true; }
                        tp.synthOverrides[a.index] = SynthP::defs()[(size_t) a.index].fromNorm (v);
                        break;
                    default: break;
                }
            }
        }

        // ---- instruments
        if (tp.synth) tp.synth->process (tL, tR, n, tp.scratch.data(), (int) tp.scratch.size(), synthOv ? tp.synthOverrides : nullptr, mods);
        if (tp.drums) tp.drums->process (tL, tR, n, tp.scratch.data(), (int) tp.scratch.size());

        // ---- audio clips
        if (tr.playing)
            for (int si = 0; si < numSegs; ++si)
            {
                const auto& sg = segs[si];
                const double b1 = sg.b0 + sg.len * bps;
                for (auto& rg : t.regions)
                {
                    if (rg.endBeat <= sg.b0 || rg.startBeat >= b1 || rg.data == nullptr) continue;
                    const auto* d = rg.data;
                    const int nf = d->numFrames();
                    const bool st = d->buffer.getNumChannels() > 1;
                    const float* c0 = d->buffer.getReadPointer (0);
                    const float* c1 = d->buffer.getReadPointer (st ? 1 : 0);
                    const double secPerBeat = 60.0 / s->tempo;
                    for (int i = 0; i < sg.len; ++i)
                    {
                        const double b = sg.b0 + i * bps;
                        if (b < rg.startBeat || b >= rg.endBeat) continue;
                        const double src = (rg.sourceOffsetSec + (b - rg.startBeat) * secPerBeat) * d->sampleRate;
                        const int i0 = (int) src;
                        if (i0 < 0 || i0 >= nf - 1) continue;
                        const float fr = (float) (src - i0);
                        float g = rg.gain;
                        if (rg.fadeInBeats > 0 && b - rg.startBeat < rg.fadeInBeats) g *= (float) ((b - rg.startBeat) / rg.fadeInBeats);
                        if (rg.fadeOutBeats > 0 && rg.endBeat - b < rg.fadeOutBeats) g *= (float) ((rg.endBeat - b) / rg.fadeOutBeats);
                        tL[sg.off + i] += (c0[i0] + (c0[i0 + 1] - c0[i0]) * fr) * g;
                        tR[sg.off + i] += (c1[i0] + (c1[i0 + 1] - c1[i0]) * fr) * g;
                    }
                }
            }

        // ---- input monitoring on armed audio tracks
        if (! offline && tp.armed.load (std::memory_order_relaxed) && tp.type == "audio" && inL != nullptr)
            for (int i = 0; i < n; ++i) { tL[i] += inL[i]; tR[i] += inR[i]; }

        // ---- inserts
        tp.inserts.process (tL, tR, n, fxc, tp.fxOverrides);

        // ---- fader / pan / mute / solo
        const bool audible = ! tp.ch.mute.load (std::memory_order_relaxed) && (! anySolo || tp.ch.solo.load (std::memory_order_relaxed) || onlyTrack >= 0);
        const float g = audible ? MixP::dbToGain (volDb) : 0.0f;
        tp.ch.gL.target = g * DSP::panL (pan) * 1.41421f;
        tp.ch.gR.target = g * DSP::panR (pan) * 1.41421f;
        for (int i = 0; i < n; ++i) { tL[i] *= tp.ch.gL.next(); tR[i] *= tp.ch.gR.next(); }
        tp.ch.meter (tL, tR, n);

        // ---- routing
        float* oL = mL; float* oR = mR;
        if (t.output >= 0 && t.output < (int) s->buses.size())
        { oL = s->buses[(size_t) t.output]->buf.getWritePointer (0); oR = s->buses[(size_t) t.output]->buf.getWritePointer (1); }
        juce::FloatVectorOperations::add (oL, tL, n);
        juce::FloatVectorOperations::add (oR, tR, n);
        for (int k = 0; k < MixP::numSends && k < (int) s->buses.size(); ++k)
            if (sends[k] > 0.0001f && audible)
            {
                juce::FloatVectorOperations::addWithMultiply (s->buses[(size_t) k]->buf.getWritePointer (0), tL, sends[k], n);
                juce::FloatVectorOperations::addWithMultiply (s->buses[(size_t) k]->buf.getWritePointer (1), tR, sends[k], n);
            }
    }

    // ---- buses
    for (auto* b : s->buses)
    {
        float* bL = b->buf.getWritePointer (0);
        float* bR = b->buf.getWritePointer (1);
        b->inserts.process (bL, bR, n, fxc, nullptr);
        const float g = b->ch.mute.load() ? 0.0f : MixP::dbToGain (b->ch.volDb.load());
        const float pan = b->ch.pan.load();
        b->ch.gL.target = g * DSP::panL (pan) * 1.41421f;
        b->ch.gR.target = g * DSP::panR (pan) * 1.41421f;
        for (int i = 0; i < n; ++i) { bL[i] *= b->ch.gL.next(); bR[i] *= b->ch.gR.next(); }
        b->ch.meter (bL, bR, n);
        juce::FloatVectorOperations::add (mL, bL, n);
        juce::FloatVectorOperations::add (mR, bR, n);
    }

    // ---- metronome
    if (! offline && tr.playing && metronomeOn.load (std::memory_order_relaxed))
    {
        for (int si = 0; si < numSegs; ++si)
        {
            const auto& sg = segs[si];
            for (int i = 0; i < sg.len; ++i)
            {
                const double b = sg.b0 + i * bps;
                const double prev = b - bps;
                if (std::floor (b) != std::floor (prev) || (i == 0 && std::abs (b - std::round (b)) < bps))
                {
                    metroEnv = 1.0f;
                    metroAccent = ((int) std::round (b) % s->tsNum) == 0 ? 1 : 0;
                }
                if (metroEnv > 0.001f)
                {
                    metroPhase += (metroAccent ? 1760.0f : 1320.0f) / (float) sampleRate;
                    if (metroPhase >= 1.0f) metroPhase -= 1.0f;
                    const float c = std::sin (DSP::twoPi * metroPhase) * metroEnv * 0.25f;
                    mL[sg.off + i] += c; mR[sg.off + i] += c;
                    metroEnv *= 0.9985f;
                }
            }
        }
    }

    // ---- master
    masterInserts.process (mL, mR, n, fxc, nullptr);
    const float mg = MixP::dbToGain (masterCh.volDb.load (std::memory_order_relaxed));
    const float mp = masterCh.pan.load (std::memory_order_relaxed);
    masterCh.gL.target = mg * DSP::panL (mp) * 1.41421f;
    masterCh.gR.target = mg * DSP::panR (mp) * 1.41421f;
    for (int i = 0; i < n; ++i) { L[i] = mL[i] * masterCh.gL.next(); R[i] = mR[i] * masterCh.gR.next(); }

    // ---- preview (post master, not exported)
    if (! offline)
        if (auto* pd = previewData.load (std::memory_order_acquire))
        {
            double pp = previewPos.load (std::memory_order_relaxed);
            const auto end = previewEnd.load (std::memory_order_relaxed);
            const double inc = pd->sampleRate / sampleRate;
            const bool st = pd->buffer.getNumChannels() > 1;
            for (int i = 0; i < n && pp < (double) end - 1; ++i)
            {
                const int i0 = (int) pp;
                L[i] += pd->get (0, i0) * 0.8f;
                R[i] += pd->get (st ? 1 : 0, i0) * 0.8f;
                pp += inc;
            }
            if (pp >= (double) end - 1) { previewData.store (nullptr); previewPos.store (-1.0); }
            else previewPos.store (pp);
        }
    masterCh.meter (L, R, n);

    if (tr.playing)
    {
        // track loop passes for deterministic probability
        for (int si = 0; si < numSegs; ++si) if (segs[si].wrapAfter) ++tr.loopPass;
        tr.beat = beat;
    }
    tr.wasPlaying = tr.playing;
}

// ---------------------------------------------------------------------------- offline render
bool Engine::renderOffline (const RenderOptions& o, std::function<bool (float)> progress, juce::String& error)
{
    auto* s = current.load();
    if (s == nullptr) { error = "Nothing to render"; return false; }
    auto writer = makeWavWriter (o.file, o.sampleRate, 2, o.bitDepth);
    if (writer == nullptr) { error = "Could not create " + o.file.getFullPathName(); return false; }

    const juce::SpinLock::ScopedLockType sl (renderLock);
    const double oldSr = sampleRate;
    prepareAll (o.sampleRate);

    Transport tr;
    tr.playing = true; tr.beat = o.startBeat; tr.wasPlaying = false;
    const double secPerBeat = 60.0 / s->tempo;
    const auto mainSamples = (juce::int64) std::ceil ((o.endBeat - o.startBeat) * secPerBeat * o.sampleRate);
    const auto total = mainSamples + (juce::int64) (o.tailSeconds * o.sampleRate);
    juce::AudioBuffer<float> out (2, chunk);
    bool ok = true;
    juce::int64 done = 0;
    while (done < total)
    {
        const int n = (int) juce::jmin<juce::int64> (chunk, total - done);
        if (done >= mainSamples) tr.playing = false;
        processBlock (out.getWritePointer (0), out.getWritePointer (1), nullptr, nullptr, n, tr, o.onlyTrackId, true);
        if (! writer->writeFromAudioSampleBuffer (out, 0, n)) { ok = false; error = "Disk write failed"; break; }
        done += n;
        if (progress && (done % (chunk * 64)) < chunk)
            if (! progress ((float) done / (float) total)) { ok = false; error = "Cancelled"; break; }
    }
    writer.reset();
    prepareAll (oldSr);
    return ok;
}
