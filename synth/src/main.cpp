#include "ofApp.h"

#include "ofMain.h"

int main()
{
    ofGLWindowSettings settings;
    settings.setSize(1280, 800);
    settings.windowMode = OF_WINDOW;
    settings.title      = "cc2-synth";

    auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>());
    return ofRunMainLoop();
}
