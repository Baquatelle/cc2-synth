#include "SelfTest.h"
#include "ofApp.h"

#include "ofMain.h"

#include <cstring>

int main(int aArgc, char** aArgv)
{
    // --selftest must be handled BEFORE any window or GL context is created:
    // CI runners and remote shells have no display, and ofCreateWindow would fail
    // there. Because the audio core has no openFrameworks dependency, the tests can
    // run in this bare state -- no window, no GL, no audio device.
    for (int i = 1; i < aArgc; ++i)
    {
        if (std::strcmp(aArgv[i], "--selftest") == 0)
        {
            return selftest::run();
        }
    }

    ofGLWindowSettings settings;
    settings.setSize(1280, 800);
    settings.windowMode = OF_WINDOW;
    settings.title      = "cc2-synth";

    auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>());
    return ofRunMainLoop();
}
