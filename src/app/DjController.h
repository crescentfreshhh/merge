#pragma once
#include <juce_core/juce_core.h>
namespace merge { class AppContext; class DjController { public: explicit DjController (AppContext&) {} void audioConfigChanged (double, int) {} }; }
