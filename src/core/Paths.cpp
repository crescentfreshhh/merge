#include "Paths.h"

namespace merge::paths
{

static juce::File& overrideRoot()
{
    static juce::File f;
    return f;
}

void setOverrideRoot (const juce::File& root) { overrideRoot() = root; }

static juce::File ensure (const juce::File& f)
{
    f.createDirectory();
    return f;
}

juce::File userDir()
{
    if (overrideRoot() != juce::File())
        return ensure (overrideRoot().getChildFile ("User"));
    return ensure (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Merge"));
}

juce::File appDataDir()
{
    if (overrideRoot() != juce::File())
        return ensure (overrideRoot().getChildFile ("AppData"));
#if JUCE_MAC
    return ensure (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Application Support").getChildFile ("Merge"));
#else
    return ensure (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Merge"));
#endif
}

juce::File cacheDir() { return ensure (appDataDir().getChildFile ("Cache")); }

static juce::Array<juce::File> sampleSearchCandidates()
{
    juce::Array<juce::File> c;
    auto env = juce::SystemStats::getEnvironmentVariable ("MERGE_SAMPLES_DIR", {});
    if (env.isNotEmpty())
        c.add (juce::File (env));

    auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    c.add (exeDir.getChildFile ("Samples"));
    c.add (exeDir.getParentDirectory().getChildFile ("Resources").getChildFile ("Samples")); // macOS bundle
    c.add (exeDir.getParentDirectory().getChildFile ("Samples"));
   #ifdef MERGE_DEV_SAMPLES_DIR
    c.add (juce::File (MERGE_DEV_SAMPLES_DIR));
   #endif
    return c;
}

static juce::File findSamplesRoot()
{
    for (auto& f : sampleSearchCandidates())
        if (f.getChildFile ("Factory").isDirectory())
            return f;
    return {};
}

juce::File factorySamplesDir()
{
    auto root = findSamplesRoot();
    return root == juce::File() ? juce::File() : root.getChildFile ("Factory");
}

juce::File bundledPacksDir()
{
    auto root = findSamplesRoot();
    return root == juce::File() ? juce::File() : root.getChildFile ("Packs");
}

juce::File userSamplesDir() { return ensure (userDir().getChildFile ("Samples")); }
juce::File projectsDir() { return ensure (userDir().getChildFile ("Projects")); }
juce::File beatsDir() { return ensure (userDir().getChildFile ("Beats")); }
juce::File recordingsDir() { return ensure (userDir().getChildFile ("Recordings")); }
juce::File mappingsDir() { return ensure (userDir().getChildFile ("Mappings")); }
juce::File autosaveDir() { return ensure (appDataDir().getChildFile ("Autosave")); }
juce::File libraryDatabase() { return appDataDir().getChildFile ("library.db"); }
juce::File modelsDir() { return ensure (appDataDir().getChildFile ("Models")); }

juce::Array<juce::File> sampleRoots()
{
    juce::Array<juce::File> roots;
    if (auto f = factorySamplesDir(); f.isDirectory()) roots.add (f);
    if (auto p = bundledPacksDir(); p.isDirectory()) roots.add (p);
    roots.add (userSamplesDir());
    return roots;
}

namespace
{
    struct Root { const char* prefix; juce::File dir; };
    std::vector<Root> refRoots()
    {
        return { { "factory:", factorySamplesDir() },
                 { "packs:", bundledPacksDir() },
                 { "user:", userDir() } };
    }
}

juce::String toRef (const juce::File& file)
{
    for (auto& r : refRoots())
        if (r.dir != juce::File() && file.isAChildOf (r.dir))
            return juce::String (r.prefix) + file.getRelativePathFrom (r.dir).replaceCharacter ('\\', '/');
    return file.getFullPathName();
}

juce::File fromRef (const juce::String& ref)
{
    for (auto& r : refRoots())
        if (ref.startsWith (r.prefix))
            return r.dir == juce::File() ? juce::File() : r.dir.getChildFile (ref.substring ((int) strlen (r.prefix)));
    return juce::File::isAbsolutePath (ref) ? juce::File (ref) : juce::File();
}

} // namespace merge::paths
