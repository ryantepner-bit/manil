#include "AudioPool.h"

DeferredDeleter& DeferredDeleter::get() { static DeferredDeleter d; return d; }
DeferredDeleter::DeferredDeleter() { startTimer (1000); }
DeferredDeleter::~DeferredDeleter() { stopTimer(); }

void DeferredDeleter::retire (std::shared_ptr<void> obj)
{
    if (obj == nullptr) return;
    const juce::ScopedLock sl (lock);
    items.push_back ({ std::move (obj), juce::Time::getMillisecondCounter() });
}

void DeferredDeleter::flushAll()
{
    std::vector<Item> old;
    { const juce::ScopedLock sl (lock); old.swap (items); }
}

void DeferredDeleter::timerCallback()
{
    std::vector<Item> dead;
    {
        const juce::ScopedLock sl (lock);
        const auto now = juce::Time::getMillisecondCounter();
        for (auto it = items.begin(); it != items.end();)
        {
            if (now - it->time > 3000) { dead.push_back (std::move (*it)); it = items.erase (it); }
            else ++it;
        }
    }
    // 'dead' destroyed here, outside the lock, on the message thread
}

AudioPool& AudioPool::get() { static AudioPool p; return p; }
AudioPool::AudioPool() { formatManager.registerBasicFormats(); }

std::shared_ptr<const SampleData> AudioPool::load (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    const auto key = f.getFullPathName();
    if (auto it = cache.find (key); it != cache.end()) return it->second;
    if (! f.existsAsFile()) return nullptr;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr) return nullptr;
    auto sd = std::make_shared<SampleData>();
    const int ch = (int) juce::jlimit<unsigned int> (1u, 2u, reader->numChannels);
    const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) 48000 * 60 * 20);
    sd->buffer.setSize (ch, juce::jmax (1, len));
    sd->buffer.clear();
    reader->read (&sd->buffer, 0, len, 0, true, ch > 1);
    sd->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    sd->path = key;
    cache[key] = sd;
    return sd;
}

void AudioPool::invalidate (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    if (auto it = cache.find (f.getFullPathName()); it != cache.end())
    {
        DeferredDeleter::get().retire (std::const_pointer_cast<SampleData> (it->second));
        cache.erase (it);
    }
}

std::unique_ptr<juce::AudioFormatWriter> makeWavWriter (const juce::File& f, double sampleRate, int numChannels, int bits)
{
    f.deleteFile();
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return nullptr;
    juce::WavAudioFormat wav;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (numChannels).withBitsPerSample (bits);
    return wav.createWriterFor (os, opts);
}
