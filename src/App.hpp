#pragma once
#include "raylib.h"

// All mutable state for the demo. Replace these fields with your own.
struct AppState {
    Color  clearColor   = {18, 18, 22, 255};
    Color  shapeColor   = {80, 160, 255, 255};
    float  speed        = 120.0f;   // px / second
    float  radius       = 60.0f;
    bool   paused       = false;
    bool   showImGuiDemo = false;

    // Bouncing-shape simulation.
    Vector2 pos = {200.0f, 200.0f};
    Vector2 vel = {1.0f, 1.0f};      // unit direction, scaled by speed
};

class App {
public:
    App();
    ~App();

    void Frame();             // one full frame: input -> update -> draw scene -> draw GUI
    bool ShouldClose() const;

private:
    void HandleEvents();      // platform input + web resize detection
    void Update();            // advance the simulation
    void DrawScene();         // raylib drawing
    void DrawGui();           // Dear ImGui windows

    AppState state;
    int   lastW = 0, lastH = 0; // last known canvas size; used to detect resize on web
    float dpiScale = 1.0f;      // device pixel ratio; > 1 on HiDPI web builds
};
