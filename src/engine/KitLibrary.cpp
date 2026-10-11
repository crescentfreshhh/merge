#include "KitLibrary.h"
#include "Instruments.h"
#include "core/Paths.h"

namespace merge
{

static bool isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

std::optional<KitInfo> KitLibrary::readKit (const juce::File& kitJson, const juce::String& source)
{
    auto v = juce::JSON::parse (kitJson);
    if (! v.isObject())
        return std::nullopt;
    KitInfo k;
    k.dir = kitJson.getParentDirectory();
    k.name = v.getProperty ("name", k.dir.getFileName()).toString();
    k.genre = v["genre"].toString();
    k.keyName = v["key"].toString();
    k.bpm = (double) v.getProperty ("bpm", 0.0);
    k.swing = (double) v.getProperty ("swing", 50.0);
    k.license = v["license"].toString();
    k.order = (int) v.getProperty ("order", 1000);
    k.source = source;
    if (auto* pads = v["pads"].getArray())
        for (auto& p : *pads)
        {
            KitPad pad;
            pad.pad = (int) p.getProperty ("pad", (int) k.pads.size() + 1);
            pad.name = p["name"].toString();
            pad.file = k.dir.getChildFile (p["file"].toString());
            pad.choke = (int) p["choke"];
            pad.root = (int) p.getProperty ("root", 60);
            if (pad.name.isEmpty()) pad.name = pad.file.getFileNameWithoutExtension();
            k.pads.push_back (pad);
        }
    auto pattern = v["pattern"];
    k.patternLength = (int) pattern.getProperty ("length", 16);
    if (auto* steps = pattern["steps"].getDynamicObject())
        for (auto& prop : steps->getProperties())
            k.steps[prop.name.toString().getIntValue()] = prop.value.toString();
    return k;
}

KitInfo KitLibrary::kitFromFolder (const juce::File& dir)
{
    KitInfo k;
    k.dir = dir;
    k.name = dir.getFileName();
    k.source = "Folder";
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    files.sort();
    int pad = 1;
    for (auto& f : files)
    {
        if (! isAudioFile (f)) continue;
        k.pads.push_back ({ pad, f.getFileNameWithoutExtension(), f, 0, 60 });
        if (++pad > kNumPads) break;
    }
    return k;
}

void KitLibrary::scan()
{
    kits.clear();
    instruments.clear();

    auto scanRoot = [this] (const juce::File& root, const juce::String& source)
    {
        if (! root.isDirectory()) return;
        for (auto& f : root.findChildFiles (juce::File::findFiles, true, "kit.json"))
        {
            auto src = source;
            if (source != "Factory")
            {
                auto rel = f.getRelativePathFrom (root);
                src = rel.upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("\\", false, false);
            }
            if (auto k = readKit (f, src))
                kits.push_back (*k);
        }
    };

    scanRoot (paths::factorySamplesDir(), "Factory");
    scanRoot (paths::bundledPacksDir(), "Pack");
    scanRoot (paths::userSamplesDir(), "User");

    std::stable_sort (kits.begin(), kits.end(), [] (const KitInfo& a, const KitInfo& b)
    {
        const bool fa = a.source == "Factory", fb = b.source == "Factory";
        if (fa != fb) return fa;
        if (a.order != b.order) return a.order < b.order;
        return a.name.compareNatural (b.name) < 0;
    });

    auto instJson = paths::factorySamplesDir().getChildFile ("Instruments").getChildFile ("instruments.json");
    auto iv = juce::JSON::parse (instJson);
    if (auto* arr = iv["instruments"].getArray())
        for (auto& e : *arr)
            instruments.push_back ({ e["name"].toString(), instJson.getParentDirectory().getChildFile (e["file"].toString()),
                                     (int) e.getProperty ("root", 60) });
}

const KitInfo* KitLibrary::find (const juce::String& name) const
{
    for (auto& k : kits)
        if (k.name.equalsIgnoreCase (name))
            return &k;
    return nullptr;
}

void KitLibrary::applyKit (Project& project, const KitInfo& kit, bool includePattern, int patternIndex)
{
    auto& channels = project.getChannels();
    for (int i = 0; i < kNumPads && i < (int) channels.size(); ++i)
    {
        auto& ch = channels[(size_t) i];
        if (ch.kind != ChannelKind::Sampler)
            continue;
        const KitPad* pad = nullptr;
        for (auto& p : kit.pads)
            if (p.pad == i + 1) { pad = &p; break; }

        if (pad != nullptr && pad->file.existsAsFile())
        {
            auto sample = SampleCache::get().load (pad->file);
            project.setChannelSample (ch.id, sample, paths::toRef (pad->file), pad->root);
            ch.name = pad->name;
            if (ch.strip != nullptr)
                ch.strip->chokeGroup.store (pad->choke);
        }
        else
        {
            project.setChannelSample (ch.id, nullptr, {}, 60);
            ch.name = "Pad " + juce::String (i + 1);
        }
    }

    project.kitName = kit.name;
    project.genre = kit.genre;
    if (kit.bpm > 0)
        project.bpm = kit.bpm;
    project.swing = juce::jlimit (50.0, 75.0, kit.swing);
    if (kit.keyName.isNotEmpty())
        project.key = MusicalKey::fromString (kit.keyName);

    if (includePattern && ! kit.steps.empty())
    {
        project.clearPattern (patternIndex);
        project.setPatternLength (patternIndex, kit.patternLength);
        for (auto& [pad, steps] : kit.steps)
            if (pad >= 1 && pad <= kNumPads && pad <= (int) channels.size())
                project.setStepsFromString (patternIndex, channels[(size_t) (pad - 1)].id, steps);
    }
}

//==============================================================================
std::map<int, juce::String> generatePattern (const juce::String& genreIn, juce::Random& rng)
{
    const auto genre = genreIn.toLowerCase();
    std::map<int, juce::String> out;

    auto fromProb = [&rng] (const float* prob, float accentChance = 0.0f)
    {
        juce::String s;
        for (int i = 0; i < 16; ++i)
        {
            const float r = rng.nextFloat();
            if (r < prob[i])
                s << (rng.nextFloat() < accentChance ? "X" : (prob[i] < 0.35f ? "o" : "x"));
            else
                s << ".";
        }
        return s;
    };

    if (genre.contains ("house"))
    {
        out[1] = "x...x...x...x...";
        const float clap[16] { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0.15f };
        const float hat[16] { 0.1f, 0, 1, 0, 0.1f, 0, 1, 0, 0.1f, 0, 1, 0, 0.1f, 0, 1, 0.3f };
        const float shk[16] { 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f };
        const float perc[16] { 0, 0, 0, 0.4f, 0, 0, 0.3f, 0, 0, 0.4f, 0, 0, 0, 0, 0.3f, 0 };
        const float bass[16] { 0, 0, 0.8f, 0, 0, 0, 0.7f, 0.2f, 0, 0, 0.8f, 0, 0, 0.3f, 0.6f, 0 };
        out[4] = fromProb (clap);
        out[6] = fromProb (hat);
        out[11] = fromProb (shk);
        out[8] = fromProb (perc);
        out[13] = fromProb (bass);
    }
    else if (genre.contains ("trap") || genre.contains ("drill"))
    {
        const bool drill = genre.contains ("drill");
        const float kick[16] { 1, 0, 0.1f, 0.25f, 0, 0, 0.4f, 0.3f, 0, 0.2f, drill ? 0.1f : 0.6f, drill ? 0.8f : 0.2f, 0, 0.2f, 0.1f, 0.1f };
        juce::String k = fromProb (kick);
        out[1] = k;
        out[13] = k;
        juce::String snare = drill ? "......x.......x." : "........x.......";
        if (! drill && rng.nextFloat() < 0.3f) snare = "........x......o";
        out[3] = snare;
        out[4] = drill ? juce::String ("................") : snare;
        juce::String hats;
        for (int i = 0; i < 16; ++i)
        {
            if (drill) hats << ((i % 3 == 0 || rng.nextFloat() < 0.15f) ? "x" : ".");
            else hats << ((i % 2 == 0) ? (rng.nextFloat() < 0.2f ? "X" : "x") : (rng.nextFloat() < 0.35f ? "o" : "."));
        }
        out[5] = hats;
        out[6] = rng.nextFloat() < 0.5f ? "...............o" : "................";
        const float perc[16] { 0, 0, 0, 0, 0, 0, 0, 0.2f, 0, 0, 0, 0.2f, 0, 0, 0, 0.3f };
        out[8] = fromProb (perc);
    }
    else if (genre.contains ("afro") || genre.contains ("amapiano"))
    {
        out[1] = rng.nextBool() ? "x.....x.x......." : "x...x...x...x...";
        out[4] = "....x.......x...";
        out[8] = "oooooooooooooooo";
        const float rim[16] { 1, 0, 0, 0.9f, 0, 0, 0.9f, 0, 0, 0, 0.8f, 0, 0.7f, 0, 0, 0 };
        const float conga[16] { 0, 0, 0.6f, 0, 0, 0.3f, 0, 0, 0.6f, 0, 0.5f, 0, 0, 0, 0.6f, 0 };
        const float log[16] { 1, 0, 0, 0, 0, 0, 0.8f, 0, 0, 0, 0.3f, 0, 0.7f, 0, 0.2f, 0 };
        out[7] = fromProb (rim);
        out[9] = fromProb (conga);
        out[13] = fromProb (log);
    }
    else // boom bap, lo-fi, hip hop and anything else
    {
        const float kick[16] { 1, 0, 0.15f, 0, 0, 0, 0.2f, 0.5f, 0, 0.4f, 0.6f, 0, 0, 0.15f, 0, 0.1f };
        out[1] = fromProb (kick);
        out[3] = rng.nextFloat() < 0.2f ? "....x.......x..o" : "....x.......x...";
        juce::String hats;
        for (int i = 0; i < 16; ++i)
            hats << ((i % 2 == 0) ? "x" : (rng.nextFloat() < 0.25f ? "o" : "."));
        out[5] = hats;
        const float rim[16] { 0, 0, 0, 0, 0, 0, 0, 0.2f, 0, 0, 0, 0, 0, 0, 0, 0.4f };
        out[7] = fromProb (rim);
        const float shk[16] { 0, 0, 0.4f, 0, 0, 0, 0.4f, 0, 0, 0, 0.4f, 0, 0, 0, 0.4f, 0 };
        out[11] = fromProb (shk);
    }
    return out;
}

} // namespace merge
