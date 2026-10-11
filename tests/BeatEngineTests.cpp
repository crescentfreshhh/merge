#include "TestHelpers.h"
#include "engine/KitLibrary.h"
#include "engine/Instruments.h"
#include "core/Paths.h"

using namespace merge;
using namespace merge::test;

class SequencerTimingTests : public juce::UnitTest
{
public:
    SequencerTimingTests() : juce::UnitTest ("Sequencer timing", "beat") {}

    void runTest() override
    {
        beginTest ("16th notes are sample-accurate at 120 BPM / 44.1 kHz");
        {
            auto p = makeClickProject (120.0);
            const int id = p->getChannels()[0].id;
            for (int s = 0; s < 16; ++s)
                p->setStep (0, id, s, 127);
            RenderOptions o;
            o.loops = 2;
            o.blockSize = 480; // deliberately not a divisor of the step length
            auto audio = renderProject (*p, o);
            auto onsets = findOnsets (audio);
            expectEquals ((int) onsets.size(), 32);
            if (onsets.size() != 32)
                for (auto o2 : onsets) logMessage ("onset " + juce::String (o2));
            int worst = 0;
            for (size_t k = 0; k < onsets.size(); ++k)
            {
                const double expected = k * 5512.5;
                worst = juce::jmax (worst, (int) std::abs (onsets[k] - expected));
            }
            expect (worst <= 1, "worst step timing error " + juce::String (worst) + " samples");
        }

        beginTest ("MPC swing delays only the odd 16ths");
        {
            auto p = makeClickProject (120.0);
            p->swing = 60.0;
            const int id = p->getChannels()[0].id;
            for (int s = 0; s < 16; ++s)
                p->setStep (0, id, s, 127);
            auto audio = renderProject (*p, {});
            auto onsets = findOnsets (audio);
            expectEquals ((int) onsets.size(), 16);
            // 60% swing: offset = (0.60 - 0.50) * 0.5 beat = 0.05 beat = 1102.5 samples
            for (size_t k = 0; k < onsets.size(); ++k)
            {
                const double expected = k * 5512.5 + ((k % 2) ? 1102.5 : 0.0);
                expect (std::abs (onsets[k] - expected) <= 1.0,
                        "step " + juce::String ((int) k) + " at " + juce::String (onsets[k]) + " expected " + juce::String (expected));
            }
        }

        beginTest ("Offline render length = musical length + tail");
        {
            auto p = makeClickProject (90.0);
            RenderOptions o;
            o.loops = 3;
            o.tailSeconds = 0.5;
            auto audio = renderProject (*p, o);
            const int expected = (int) std::llround (3 * 4.0 * 60.0 / 90.0 * 44100.0) + (int) (0.5 * 44100.0);
            expectEquals (audio.getNumSamples(), expected);
        }

        beginTest ("Song mode plays pattern clips at their position");
        {
            auto p = makeClickProject (120.0);
            const int id = p->getChannels()[0].id;
            p->setStep (1, id, 0, 127); // pattern B, first step only
            p->getClips().clear();
            p->getClips().push_back ({ 1, 0, 4.0, 4.0, 0.0 });  // bar 2
            p->getClips().push_back ({ 1, 1, 12.0, 8.0, 0.0 }); // bar 4, two loops of B
            RenderOptions o;
            o.songMode = true;
            auto audio = renderProject (*p, o);
            auto onsets = findOnsets (audio);
            expectEquals ((int) onsets.size(), 3);
            if (onsets.size() == 3)
            {
                expect (std::abs (onsets[0] - 4 * 22050) <= 1);
                expect (std::abs (onsets[1] - 12 * 22050) <= 1);
                expect (std::abs (onsets[2] - 16 * 22050) <= 1);
            }
        }

        beginTest ("Muted song lanes are silent");
        {
            auto p = makeClickProject (120.0);
            const int id = p->getChannels()[0].id;
            p->setStep (0, id, 0, 127);
            p->getClips() = { { 0, 2, 0.0, 4.0, 0.0 } };
            p->getLanes()[2].muted = true;
            RenderOptions o;
            o.songMode = true;
            expect (findOnsets (renderProject (*p, o)).empty());
        }
    }
};

class SamplerTests : public juce::UnitTest
{
public:
    SamplerTests() : juce::UnitTest ("Sampler & mixer", "beat") {}

    void runTest() override
    {
        beginTest ("Choke group: closed hat cuts the open hat");
        {
            auto p = makeClickProject();
            auto& open = p->getChannels()[0];
            auto& closed = p->getChannels()[1];
            p->setChannelSample (open.id, makeConstant (0.5f, 44100), {}, 60);
            p->setChannelSample (closed.id, makeClick(), {}, 60);
            open.strip->chokeGroup.store (1);
            closed.strip->chokeGroup.store (1);
            closed.strip->volume.store (0.8f);
            p->setStep (0, open.id, 0, 127);
            p->setStep (0, closed.id, 4, 127);
            auto audio = renderProject (*p, {});
            const int chokeAt = 4 * 5512;
            expect (rms (audio, 0, 1000, 2000) > 0.3, "open hat sounds before the choke");
            expect (rms (audio, 0, chokeAt + 600, 4000) < 1.0e-3, "open hat silent after the choke");
        }

        beginTest ("Pan hard left silences the right channel");
        {
            auto p = makeClickProject();
            auto& ch = p->getChannels()[0];
            p->setChannelSample (ch.id, makeConstant (0.5f, 4410), {}, 60);
            ch.strip->pan.store (-1.0f);
            p->setStep (0, ch.id, 0, 127);
            auto audio = renderProject (*p, {});
            expect (rms (audio, 0, 2000, 1000) > 0.3);
            expect (rms (audio, 1, 2000, 1000) < 1.0e-3);
        }

        beginTest ("Mute and solo");
        {
            auto p = makeClickProject();
            auto& a = p->getChannels()[0];
            auto& b = p->getChannels()[1];
            p->setChannelSample (b.id, makeClick(), {}, 60);
            p->setStep (0, a.id, 0, 127);
            p->setStep (0, b.id, 8, 127);
            b.strip->solo.store (true);
            auto onsets = findOnsets (renderProject (*p, {}));
            expectEquals ((int) onsets.size(), 1);
            if (! onsets.empty()) expect (std::abs (onsets[0] - 8 * 5512.5) <= 1);
        }

        beginTest ("Sampler pitch follows the note");
        {
            auto p = makeClickProject();
            auto& ch = p->getChannels()[0];
            p->setChannelSample (ch.id, makeSine (440.0, 1.0), {}, 60);
            ch.stepNote = 72; // an octave up
            p->setStep (0, ch.id, 0, 127);
            auto audio = renderProject (*p, {});
            // count zero crossings over 0.25 s -> ~880 Hz
            int crossings = 0;
            for (int i = 1001; i < 1001 + 11025; ++i)
                if ((audio.getSample (0, i - 1) < 0) != (audio.getSample (0, i) < 0)) ++crossings;
            const double freq = crossings / 2.0 / 0.25;
            expect (std::abs (freq - 880.0) < 10.0, "measured " + juce::String (freq));
        }
    }
};

class ProjectTests : public juce::UnitTest
{
public:
    ProjectTests() : juce::UnitTest ("Project model", "beat") {}

    void runTest() override
    {
        beginTest ("Save / load round trip");
        {
            Project p;
            p.bpm = 93.5;
            p.swing = 57.0;
            p.key = MusicalKey::fromString ("F# minor");
            auto& c = p.addChannel (ChannelKind::Synth, "Lead");
            p.setStep (0, p.getChannels()[0].id, 3, 100);
            p.setPatternLength (2, 32);
            p.setNotes (2, c.id, { { 0.0, 1.0, 64, 0.9f }, { 1.5, 0.5, 67, 0.5f } });
            p.setInsert (c.id, 1, createBuiltInEffect ("delay"));
            p.getClips().push_back ({ 2, 3, 8.0, 8.0, 0.0 });
            auto file = juce::File::createTempFile (".merge");
            expect (p.save (file));
            const auto json = juce::JSON::toString (p.toVar());
            Project q;
            expect (q.load (file));
            const auto json2 = juce::JSON::toString (q.toVar());
            if (json2 != json)
            {
                file.getSiblingFile ("a.json").replaceWithText (json);
                file.getSiblingFile ("b.json").replaceWithText (json2);
            }
            expect (json2 == json, "round trip differs (see a.json / b.json next to " + file.getFullPathName() + ")");
            expectEquals (q.key.toCamelot(), juce::String ("11A"));
            file.deleteFile();
        }

        beginTest ("Undo / redo of pattern edits");
        {
            Project p;
            const int id = p.getChannels()[0].id;
            p.checkpoint();
            p.setStep (0, id, 0, 127);
            expectEquals (p.getStep (0, id, 0), 127);
            expect (p.undo());
            expectEquals (p.getStep (0, id, 0), 0);
            expect (p.redo());
            expectEquals (p.getStep (0, id, 0), 127);
        }

        beginTest ("Legato steps on instrument channels hold until the next step");
        {
            Project p;
            auto& bass = p.addChannel (ChannelKind::Bass808, "808");
            p.setStep (0, bass.id, 0, 100);
            p.setStep (0, bass.id, 6, 100);
            auto cp = p.compilePattern (p.getPatterns()[0]);
            expectEquals ((int) cp.events.size(), 2);
            expect (std::abs (cp.events[0].length - 6 * 0.25 * 0.97) < 1.0e-9);
            expect (std::abs (cp.events[1].length - 10 * 0.25 * 0.97) < 1.0e-9);
        }

        beginTest ("Camelot and key parsing");
        {
            expectEquals (MusicalKey::fromString ("A minor").toCamelot(), juce::String ("8A"));
            expectEquals (MusicalKey::fromString ("C major").toCamelot(), juce::String ("8B"));
            expectEquals (MusicalKey::fromString ("Fm").toCamelot(), juce::String ("4A"));
            expectEquals (MusicalKey::fromString ("B").toCamelot(), juce::String ("1B"));
            expectEquals (MusicalKey::fromString ("8A").toString(), juce::String ("A minor"));
            expect (MusicalKey::fromString ("A minor").isInScale (60));
            expect (! MusicalKey::fromString ("A minor").isInScale (61));
        }

        beginTest ("Recorded pad hits are quantised to the nearest step");
        {
            Project p;
            const int id = p.getChannels()[2].id;
            p.applyRecorded ({ 2, 0, 60, 1.0f, 1.13, 0.25 }); // 1.13 beats -> step 5 (1.25)
            p.applyRecorded ({ 2, 0, 60, 1.0f, 3.95, 0.25 }); // wraps to step 0
            expectEquals (p.getStep (0, id, 5), 127);
            expectEquals (p.getStep (0, id, 0), 127);
        }
    }
};

class RecordingTests : public juce::UnitTest
{
public:
    RecordingTests() : juce::UnitTest ("Live recording", "beat") {}

    void runTest() override
    {
        beginTest ("Pad hits while recording come back with their beat position");
        auto p = makeClickProject (120.0);
        BeatCore core;
        core.prepare (44100.0, 441);
        core.publish (p->buildSnapshot());
        core.transport.recording.store (true);
        core.play();
        juce::AudioBuffer<float> buf (2, 441);
        for (int i = 0; i < 110; ++i) // 110 * 10 ms = 1.1 s = 2.2 beats
            core.process (buf, 441);
        core.noteOn (0, 60, 0.9f);
        core.process (buf, 441);
        RecordedNote r;
        expect (core.popRecorded (r));
        expectEquals (r.channel, 0);
        expect (std::abs (r.startBeat - 2.2) < 0.02, "beat " + juce::String (r.startBeat));
    }
};

class KitTests : public juce::UnitTest
{
public:
    KitTests() : juce::UnitTest ("Factory kits", "beat") {}

    void runTest() override
    {
        beginTest ("Factory kits are found and load onto the pads");
        KitLibrary lib;
        lib.scan();
        const auto factory = paths::factorySamplesDir();
        if (! factory.isDirectory())
        {
            logMessage ("Factory samples not generated - skipping (set MERGE_SAMPLES_DIR)");
            return;
        }
        int factoryKits = 0;
        for (auto& k : lib.getKits())
            if (k.source == "Factory")
            {
                ++factoryKits;
                expectEquals ((int) k.pads.size(), 16, k.name);
                for (auto& pad : k.pads)
                    expect (pad.file.existsAsFile(), pad.file.getFullPathName());
            }
        expectEquals (factoryKits, 6);
        expect (lib.getInstrumentSamples().size() >= 5);

        auto* trap = lib.find ("Trap");
        expect (trap != nullptr);
        if (trap == nullptr) return;
        Project p;
        p.setAudioConfig (44100.0, 512);
        KitLibrary::applyKit (p, *trap, true, 0);
        expectEquals (p.bpm, 140.0);
        expectEquals (p.getChannels()[0].name, juce::String ("Kick"));
        expect (p.getChannels()[0].strip->instrument->getSample() != nullptr);
        expect (! p.getPatterns()[0].isEmpty());
        auto audio = renderProject (p, {});
        expect (audio.getMagnitude (0, 0, audio.getNumSamples()) > 0.1f, "kit pattern makes sound");

        beginTest ("Pattern generator produces drums for every genre");
        juce::Random rng (42);
        for (auto genre : { "Trap", "Drill", "House", "Boom Bap", "Lo-Fi", "Afrobeats" })
        {
            auto pat = generatePattern (genre, rng);
            expect (pat.count (1) == 1 && pat[1].containsAnyOf ("xXo"), juce::String (genre) + " has a kick");
        }
    }
};

static SequencerTimingTests sequencerTimingTests;
static SamplerTests samplerTests;
static ProjectTests projectTests;
static RecordingTests recordingTests;
static KitTests kitTests;
