#pragma once
#include "raylib.h"
#include "EventReader.hpp"
#include "EventScene.hpp"
#include <string>
#include <vector>

// All mutable state for the app.
struct AppState {
    // ─── Event selection ──────────────────────────────────────────────────────
    std::vector<std::string> eventFiles;   // *.json names found in assets/events
    int   selectedEvent = -1;              // index into eventFiles, -1 = none
    bool  hasEvent      = false;
    velo::VeloEvent event;

    // ─── 3D view (free-fly camera; world-up locked, no roll) ────────────────────
    Camera3D camera{};
    // Goal pose: what input drives directly.
    Vector3  camPos{0, 0, 400};             // eye position
    float    camYaw   = 0.6f;               // radians; look direction around world Y
    float    camPitch = -0.3f;              // radians; clamped near +/- pi/2
    // Rendered pose: eased toward the goal each frame for smooth motion.
    Vector3  camPosR{0, 0, 400};
    float    camYawR   = 0.6f;
    float    camPitchR = -0.3f;
    bool     camSmoothInit = false;         // snap on the first frame, ease after
    float    viewScale = 100.0f;            // scene extent, drives move/dolly speed

    float moveSpeed = 0.6f;                 // WASD/QE fly speed, as a fraction of view scale / s
    bool  showBoundingBox = true;
    Color background      = {12, 12, 16, 255};
    int   screenshotFormat = 0;             // index into kScreenshotFormats (App.cpp)
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
    void RequestScreenshot(); // queue a capture of the completed frame
    void CaptureScreenshot(); // write/download the current framebuffer

    void RefreshEventList();  // rescan assets/events for *.json files
    void LoadEvent(int index);// load eventFiles[index] into state.event + scene
    void UpdateViewScale();   // refresh the movement-speed scale from event bounds
    void FrameEvent();        // reposition the camera to see the loaded event
    void UpdateFlyCamera();   // mouse-look + WASD fly -> camera pose

    AppState   state;
    EventScene scene;
    bool  framedOnce = false;   // camera auto-framed on first load only
    int   lastW = 0, lastH = 0; // last known canvas size; used to detect resize on web
    float dpiScale = 1.0f;      // device pixel ratio; > 1 on HiDPI web builds
    bool  mobileLayout = false; // phone-sized screen: compact panel, touch hints
    bool  screenshotPending = false;
    int   screenshotSerial = 0;

    // Touch gesture state (web/mobile): one finger looks, two pan + pinch-dolly.
    int     touchPrevCount = 0; // touch points seen last frame
    int     touchFrames    = 0; // frames since the current gesture started
    bool    touchOnUi      = false; // gesture began over the ImGui panel
    Vector2 touchPrev[2]   = {};    // last positions of the tracked points
};
