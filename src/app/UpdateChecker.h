#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace merge
{

/** Asks GitHub Releases for the newest version once at startup (in the background). */
class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker() : juce::Thread ("Merge update check") {}
    ~UpdateChecker() override { stopThread (3000); }

    /** Calls back on the message thread with (version, url) if a newer release exists. */
    void check (const juce::String& repo, std::function<void (juce::String, juce::String)> onNewer)
    {
        repository = repo;
        callback = std::move (onNewer);
        startThread (juce::Thread::Priority::low);
    }

    static bool isNewer (const juce::String& candidate, const juce::String& current)
    {
        auto parts = [] (juce::String v)
        {
            v = v.trimCharactersAtStart ("vV");
            juce::StringArray a;
            a.addTokens (v, ".", {});
            std::array<int, 3> n {};
            for (int i = 0; i < 3 && i < a.size(); ++i) n[(size_t) i] = a[i].getIntValue();
            return n;
        };
        return parts (candidate) > parts (current);
    }

private:
    void run() override
    {
        juce::URL url ("https://api.github.com/repos/" + repository + "/releases/latest");
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (5000)
                                                 .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: Merge"));
        if (stream == nullptr || threadShouldExit()) return;
        auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json["tag_name"].toString();
        const auto page = json["html_url"].toString();
        if (tag.isNotEmpty() && isNewer (tag, MERGE_VERSION_STRING) && callback)
        {
            auto cb = callback;
            juce::MessageManager::callAsync ([cb, tag, page] { cb (tag, page); });
        }
    }

    juce::String repository;
    std::function<void (juce::String, juce::String)> callback;
};

} // namespace merge
