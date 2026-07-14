#include "App.hpp"

#include "rlImGui.h"
#include "imgui.h"
#include "raymath.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <stdexcept>

// Directory (relative to the working directory) that holds the event JSON.
// The same relative path resolves on native disk and in the Emscripten
// virtual filesystem (assets/ is preloaded there via --preload-file).
static const char* kEventsDir = "assets/events";

// ─── Web (Emscripten) interop ──────────────────────────────────────────────────
// The HTML shell owns a #canvas-container that flexes to fill the page; the
// <canvas> framebuffer is sized in physical pixels for crisp HiDPI rendering,
// while its CSS display size stays in logical pixels.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EM_JS(int,    canvas_width,       (), { return document.getElementById('canvas-container').clientWidth;  });
EM_JS(int,    canvas_height,      (), { return document.getElementById('canvas-container').clientHeight; });
EM_JS(int,    canvas_buf_width,   (), { return document.getElementById('canvas').width;  });
EM_JS(int,    canvas_buf_height,  (), { return document.getElementById('canvas').height; });
EM_JS(double, device_pixel_ratio, (), { return window.devicePixelRatio || 1.0; });
EM_JS(void,   set_canvas_css_size, (int w, int h), {
    Module.canvas.style.width  = w + 'px';
    Module.canvas.style.height = h + 'px';
});
EM_JS(void, download_file, (const char* namePtr, const unsigned char* dataPtr, int dataSize, const char* mimePtr), {
    const name = UTF8ToString(namePtr);
    const mime = UTF8ToString(mimePtr);
    const bytes = HEAPU8.slice(dataPtr, dataPtr + dataSize);
    const blob = new Blob([bytes], { type: mime });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = name;
    document.body.appendChild(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
});
#endif

// Image formats supported by this raylib build's ExportImage() (see config.h:
// SUPPORT_FILEFORMAT_*). Keep in sync if that configuration changes.
struct ScreenshotFormat { const char* ext; const char* label; const char* mime; };
static const ScreenshotFormat kScreenshotFormats[] = {
    { ".png", "PNG", "image/png" },
    { ".bmp", "BMP", "image/bmp" },
    { ".qoi", "QOI", "image/qoi" },
};
static constexpr int kScreenshotFormatCount = (int)(sizeof(kScreenshotFormats) / sizeof(kScreenshotFormats[0]));

static std::string ScreenshotName(int serial, const char* ext)
{
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif

    char name[96];
    std::snprintf(name, sizeof(name), "VELOvis3_%04d%02d%02d_%02d%02d%02d_%03d%s",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, serial, ext);
    return name;
}

// Screen-size helpers: on the web, GetRenderWidth/Height track the physical
// framebuffer and are updated by SetWindowSize; GetScreenWidth is stale after a
// resize. On native, the two are equivalent.
#ifdef __EMSCRIPTEN__
static inline int SW() { return GetRenderWidth();  }
static inline int SH() { return GetRenderHeight(); }
#else
static inline int SW() { return GetScreenWidth();  }
static inline int SH() { return GetScreenHeight(); }
#endif

// ─── Lifecycle ─────────────────────────────────────────────────────────────────

App::App()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);

#ifdef __EMSCRIPTEN__
    double dpr = device_pixel_ratio();
    dpiScale   = (float)dpr;
    int cssW   = canvas_width();
    int cssH   = canvas_height();
    int w      = (int)(cssW * dpr + 0.5);
    int h      = (int)(cssH * dpr + 0.5);
#else
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);
    int w = 1280;
    int h = 720;
#endif
    SetTraceLogLevel(LOG_WARNING); // quiet raylib's INFO spam
    InitWindow(w, h, "VELOvis");
    SetExitKey(KEY_NULL);          // don't let ESC close the window
    SetTargetFPS(60);

#ifdef __EMSCRIPTEN__
    // Pin the CSS display size to logical pixels; the framebuffer is already physical.
    set_canvas_css_size(cssW, cssH);
#endif

    rlImGuiSetup(true);            // true = apply the Dear ImGui dark theme

#ifdef __EMSCRIPTEN__
    if (dpr > 1.0) {
        ImGui::GetStyle().ScaleAllSizes((float)dpr);
        ImGui::GetIO().FontGlobalScale = (float)dpr;
    }
#endif

    lastW = SW();
    lastH = SH();

    // Phone-sized screens (any orientation) get a compact layout: the control
    // panel starts collapsed and shows touch instead of keyboard hints.
    mobileLayout = fminf((float)SW(), (float)SH()) / dpiScale < 500.0f;

    // Default camera; overwritten by FrameEvent() once an event is loaded.
    state.camera.position   = {0.0f, 0.0f, 400.0f};
    state.camera.target     = {0.0f, 0.0f, 0.0f};
    state.camera.up         = {0.0f, 1.0f, 0.0f};
    state.camera.fovy       = 45.0f;
    state.camera.projection = CAMERA_PERSPECTIVE;

    // Populate the dropdown and open the first event so there's something to see.
    RefreshEventList();
    if (!state.eventFiles.empty())
        LoadEvent(0);

    // Anything printed to stdout/stderr shows up in the web console pane.
    printf("VELOvis started: %d x %d (dpi scale %.2f), %zu event files\n",
           SW(), SH(), dpiScale, state.eventFiles.size());
    fflush(stdout);
}

App::~App()
{
    rlImGuiShutdown();
    CloseWindow();
}

bool App::ShouldClose() const
{
    return WindowShouldClose();
}

// ─── Event loading ─────────────────────────────────────────────────────────────

void App::RefreshEventList()
{
    state.eventFiles.clear();
    try {
        for (const auto& entry : std::filesystem::directory_iterator(kEventsDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                state.eventFiles.push_back(entry.path().filename().string());
        }
    } catch (const std::exception& e) {
        printf("could not list %s: %s\n", kEventsDir, e.what());
    }
    std::sort(state.eventFiles.begin(), state.eventFiles.end());
}

void App::LoadEvent(int index)
{
    if (index < 0 || index >= (int)state.eventFiles.size())
        return;

    const std::string path = std::string(kEventsDir) + "/" + state.eventFiles[index];
    try {
        state.event        = velo::EventReader::LoadFromFile(path);
        state.selectedEvent = index;
        state.hasEvent      = true;
        scene.SetEvent(state.event);
        // Frame the first event; keep the user's camera on later switches, but
        // still refresh the movement-speed scale for the new event's extent.
        if (!framedOnce) { FrameEvent(); framedOnce = true; }
        else             { UpdateViewScale(); }
        printf("loaded %s: %zu hits, %zu particles\n",
               path.c_str(), state.event.x.size(), state.event.montecarlo.particles.size());
    } catch (const std::exception& e) {
        printf("failed to load %s: %s\n", path.c_str(), e.what());
        state.hasEvent = false;
    }
    fflush(stdout);
}

// Look direction from yaw/pitch. Shared by FrameEvent and UpdateFlyCamera.
static Vector3 ForwardDir(float yaw, float pitch)
{
    const float cp = cosf(pitch);
    return Vector3{cp * sinf(yaw), sinf(pitch), cp * cosf(yaw)};
}

void App::UpdateViewScale()
{
    if (scene.Empty()) return;
    const BoundingBox bb = scene.Bounds();
    const float ext = fmaxf(bb.max.x - bb.min.x, fmaxf(bb.max.y - bb.min.y, bb.max.z - bb.min.z));
    state.viewScale = fmaxf(ext, 1.0f);
}

void App::FrameEvent()
{
    if (scene.Empty()) return;
    UpdateViewScale();

    const BoundingBox bb = scene.Bounds();
    const Vector3 center = {
        (bb.min.x + bb.max.x) * 0.5f,
        (bb.min.y + bb.max.y) * 0.5f,
        (bb.min.z + bb.max.z) * 0.5f,
    };

    // Keep the current look direction; back the eye off so the whole event fits.
    const float dist = state.viewScale * 1.2f + 1.0f;
    state.camPos = Vector3Subtract(center, Vector3Scale(ForwardDir(state.camYaw, state.camPitch), dist));
}

// ─── Input ───────────────────────────────────────────────────────────────────

void App::HandleEvents()
{
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Q))
        CloseWindow();
    if (IsKeyPressed(KEY_F12))
        RequestScreenshot();

#ifdef __EMSCRIPTEN__
    // The shell resizes the canvas framebuffer (physical pixels) whenever the
    // page layout or orientation changes; mirror any change into raylib.
    // Without GLFW_SCALE_TO_MONITOR, emscripten's glfwSetWindowSize takes raw
    // framebuffer pixels and clears the canvas CSS size — re-pin the CSS size
    // to logical pixels afterwards to keep HiDPI rendering crisp.
    int cw = canvas_buf_width(), ch = canvas_buf_height();
    if (cw != lastW || ch != lastH)
    {
        lastW = cw; lastH = ch;
        double dpr = device_pixel_ratio();
        SetWindowSize(cw, ch);
        set_canvas_css_size((int)(cw / dpr + 0.5), (int)(ch / dpr + 0.5));
    }
#endif
}

// ─── Update ────────────────────────────────────────────────────────────────────

void App::Update()
{
    UpdateFlyCamera();
    scene.Update();   // rebuild any layer batches marked dirty by the UI
}

// ─── Screenshot ───────────────────────────────────────────────────────────────

void App::RequestScreenshot()
{
    screenshotPending = true;
}

void App::CaptureScreenshot()
{
    if (!screenshotPending) return;
    screenshotPending = false;

    const ScreenshotFormat& fmt = kScreenshotFormats[state.screenshotFormat];
    const std::string fileName = ScreenshotName(++screenshotSerial, fmt.ext);
    Image image = LoadImageFromScreen();

#ifdef __EMSCRIPTEN__
    // No in-memory export exists for non-PNG formats in this raylib build, so
    // write through the (in-memory) virtual filesystem and read the bytes back.
    if (ExportImage(image, fileName.c_str())) {
        int dataSize = 0;
        unsigned char* data = LoadFileData(fileName.c_str(), &dataSize);
        if (data && dataSize > 0) {
            download_file(fileName.c_str(), data, dataSize, fmt.mime);
            printf("Screenshot downloaded: %s\n", fileName.c_str());
        } else {
            printf("Screenshot failed\n");
        }
        UnloadFileData(data);
        remove(fileName.c_str());
    } else {
        printf("Screenshot failed: %s\n", fileName.c_str());
    }
    fflush(stdout);
#else
    if (ExportImage(image, fileName.c_str()))
        printf("Screenshot saved: %s\n", fileName.c_str());
    else
        printf("Screenshot failed: %s\n", fileName.c_str());
    fflush(stdout);
#endif

    UnloadImage(image);
}

void App::UpdateFlyCamera()
{
    ImGuiIO& io = ImGui::GetIO();

    // Hold Shift for precision: slower look/pan/fly and finer dolly.
    const bool  slowMod = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const float slow    = slowMod ? 0.2f : 1.0f;

    Vector3 forward = ForwardDir(state.camYaw, state.camPitch);
    Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, Vector3{0, 1, 0}));

    // ─── Touch gestures (web/mobile): 1 finger looks, 2 fingers pan + pinch ────
    // Deltas are normalized by screen height so the feel is device-independent.
    // Native desktop never reports touch points, so this block is inert there.
    const int touches = (GetTouchPointCount() < 2) ? GetTouchPointCount() : 2;
    if (touches > 0) {
        const Vector2 t0 = GetTouchPosition(0);
        const Vector2 t1 = (touches > 1) ? GetTouchPosition(1) : t0;

        // Frame 1 of a gesture only records positions: io.WantCaptureMouse still
        // reflects the pre-touch pointer position (there is no hover on touch),
        // so whether the gesture belongs to the UI is decided one frame later.
        if (touchFrames == 1) touchOnUi = io.WantCaptureMouse;

        // Move only while the finger count is stable; a count change re-baselines.
        if (touchFrames > 0 && !touchOnUi && touches == touchPrevCount) {
            if (touches == 1) {
                // One finger: mouse-look. A screen-height swipe turns ~3 rad.
                const float lookScale = 3.0f / (float)SH();
                state.camYaw   -= (t0.x - touchPrev[0].x) * lookScale;
                state.camPitch -= (t0.y - touchPrev[0].y) * lookScale;
                const float lim = 1.55f;
                state.camPitch = fmaxf(-lim, fminf(lim, state.camPitch));
                forward = ForwardDir(state.camYaw, state.camPitch);
                right   = Vector3Normalize(Vector3CrossProduct(forward, Vector3{0, 1, 0}));
            } else {
                // Two fingers: the midpoint drags the view plane (pan), the
                // pinch distance dollies along the look direction.
                const Vector2 mid  = {(t0.x + t1.x) * 0.5f, (t0.y + t1.y) * 0.5f};
                const Vector2 pmid = {(touchPrev[0].x + touchPrev[1].x) * 0.5f,
                                      (touchPrev[0].y + touchPrev[1].y) * 0.5f};
                const Vector3 up = Vector3CrossProduct(right, forward);
                const float pan = state.viewScale * 1.8f / (float)SH();
                state.camPos = Vector3Add(state.camPos,
                    Vector3Add(Vector3Scale(right, -(mid.x - pmid.x) * pan),
                               Vector3Scale(up,     (mid.y - pmid.y) * pan)));

                const float spread = Vector2Distance(t0, t1) - Vector2Distance(touchPrev[0], touchPrev[1]);
                state.camPos = Vector3Add(state.camPos,
                    Vector3Scale(forward, spread * state.viewScale * 2.5f / (float)SH()));
            }
        }
        touchPrev[0]   = t0;
        touchPrev[1]   = t1;
        touchPrevCount = touches;
        ++touchFrames;
    } else {
        touchPrevCount = 0;
        touchFrames    = 0;
        touchOnUi      = false;
    }

    // Don't steal the mouse while the pointer is over an ImGui window, and skip
    // the mouse path during touch: the browser synthesizes mouse events from
    // touches, which would double-apply the gesture.
    if (!io.WantCaptureMouse && touches == 0) {
        const Vector2 d = GetMouseDelta();

        // Left drag: mouse-look (FPS style) about the eye. No roll — up stays up.
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            state.camYaw   -= d.x * 0.005f;
            state.camPitch -= d.y * 0.005f;
            const float lim = 1.55f; // just under pi/2 to avoid flipping over
            state.camPitch = fmaxf(-lim, fminf(lim, state.camPitch));
            forward = ForwardDir(state.camYaw, state.camPitch);
            right   = Vector3Normalize(Vector3CrossProduct(forward, Vector3{0, 1, 0}));
        }

        // Right/middle drag: strafe the eye across the view plane.
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            const Vector3 up = Vector3CrossProduct(right, forward);
            const float s = state.viewScale * 0.002f * slow;
            state.camPos = Vector3Add(state.camPos,
                Vector3Add(Vector3Scale(right, -d.x * s), Vector3Scale(up, d.y * s)));
        }

        // Wheel: dolly the eye along the look direction.
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            state.camPos = Vector3Add(state.camPos,
                Vector3Scale(forward, wheel * state.viewScale * 0.08f * slow));
    }

    // WASD/QE: fly the eye. Speed scales with the scene extent so it stays usable
    // across the very elongated detector. Ctrl is reserved for shortcuts.
    if (!io.WantCaptureKeyboard && !IsKeyDown(KEY_LEFT_CONTROL)) {
        const float spd = state.viewScale * state.moveSpeed * slow * GetFrameTime();
        Vector3 move = {0, 0, 0};
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, Vector3Scale(forward, spd));
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, Vector3Scale(forward, spd));
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, Vector3Scale(right, spd));
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, Vector3Scale(right, spd));
        if (IsKeyDown(KEY_E)) move.y += spd;
        if (IsKeyDown(KEY_Q)) move.y -= spd;
        state.camPos = Vector3Add(state.camPos, move);
    }

    // Ease the rendered pose toward the input-driven goal. The factor
    // 1 - e^(-k*dt) is framerate-independent and never overshoots; rotation eases
    // a touch faster than translation so aiming stays responsive while movement
    // and re-framing glide. Snap on the first frame to avoid a startup swoop.
    const float dt = GetFrameTime();
    if (!state.camSmoothInit) {
        state.camPosR = state.camPos;
        state.camYawR = state.camYaw;
        state.camPitchR = state.camPitch;
        state.camSmoothInit = true;
    } else {
        const float aPos = 1.0f - expf(-12.0f * dt);
        const float aRot = 1.0f - expf(-18.0f * dt);
        state.camPosR    = Vector3Lerp(state.camPosR, state.camPos, aPos);
        state.camYawR   += (state.camYaw   - state.camYawR)   * aRot;
        state.camPitchR += (state.camPitch - state.camPitchR) * aRot;
    }

    const Vector3 rForward = ForwardDir(state.camYawR, state.camPitchR);
    state.camera.position = state.camPosR;
    state.camera.target   = Vector3Add(state.camPosR, rForward);
    state.camera.up       = Vector3{0, 1, 0};
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

void App::DrawScene()
{
    if (!state.hasEvent) return;

    BeginMode3D(state.camera);

    scene.Draw(state.camera.position);

    if (state.showBoundingBox) {
        const BoundingBox bb = scene.Bounds();
        const Vector3 center = {
            (bb.min.x + bb.max.x) * 0.5f,
            (bb.min.y + bb.max.y) * 0.5f,
            (bb.min.z + bb.max.z) * 0.5f,
        };
        const Vector3 boxSize = {
            bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z,
        };
        DrawCubeWiresV(center, boxSize, Fade(GRAY, 0.35f));
    }

    EndMode3D();
}

void App::DrawGui()
{
    // ─── Single control panel, pinned to the left edge and spanning full height ───
    // One window with collapsible sections replaces the old free-floating
    // "Controls" + "Layers" windows, which started stacked and overlapped once the
    // top window grew — making it hard to tell there were two of them.
    // On phones the fixed-width panel would cover most of the canvas: clamp its
    // width, let it collapse to just the title bar (the arrow is an easy touch
    // target), and start collapsed so the event display is the first thing seen.
    const float panelW = fminf(320.0f * dpiScale, (float)SW() * 0.85f);
    ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelW, (float)SH()}, ImGuiCond_Always);
    if (mobileLayout)
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    const bool panelOpen =
        ImGui::Begin("VELOvis controls", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (!panelOpen) {
        ImGui::End();
        if (state.hasEvent)
            scene.DrawPidLegendWindow();
        return;
    }

    const float itemW = ImGui::GetContentRegionAvail().x;

    ImGui::Text("FPS: %d", GetFPS());
    ImGui::SameLine();
#ifdef __EMSCRIPTEN__
    {
        double dpr = device_pixel_ratio();
        ImGui::Text("  %d x %d (logical)", (int)(SW() / dpr + 0.5), (int)(SH() / dpr + 0.5));
    }
#else
    ImGui::Text("  %d x %d", SW(), SH());
#endif

    // ─── Event section ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Event", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* preview = (state.selectedEvent >= 0)
            ? state.eventFiles[state.selectedEvent].c_str()
            : "(select an event)";
        ImGui::SetNextItemWidth(itemW);
        if (ImGui::BeginCombo("##eventfile", preview)) {
            for (int i = 0; i < (int)state.eventFiles.size(); ++i) {
                bool selected = (i == state.selectedEvent);
                if (ImGui::Selectable(state.eventFiles[i].c_str(), selected))
                    LoadEvent(i);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh"))
            RefreshEventList();

        if (state.eventFiles.empty())
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "No .json files in %s", kEventsDir);

        if (state.hasEvent) {
            ImGui::TextWrapped("\"%s\"", state.event.description.c_str());
            ImGui::Text("%zu hits, %zu MC particles",
                        state.event.x.size(), state.event.montecarlo.particles.size());
        }
    }

    // ─── View section ────────────────────────────────────────────────────────────
    if (state.hasEvent && ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Bounding box", &state.showBoundingBox);
        float bg[3] = {state.background.r / 255.0f, state.background.g / 255.0f, state.background.b / 255.0f};
        if (ImGui::ColorEdit3("Background", bg))
            state.background = {(unsigned char)(bg[0] * 255), (unsigned char)(bg[1] * 255),
                                (unsigned char)(bg[2] * 255), 255};
        if (ImGui::Button("Reset view"))
            FrameEvent();
        ImGui::SameLine();
        if (ImGui::Button("Screenshot"))
            RequestScreenshot();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::BeginCombo("##screenshotformat", kScreenshotFormats[state.screenshotFormat].label)) {
            for (int i = 0; i < kScreenshotFormatCount; ++i) {
                bool selected = (i == state.screenshotFormat);
                if (ImGui::Selectable(kScreenshotFormats[i].label, selected))
                    state.screenshotFormat = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Move speed", &state.moveSpeed, 0.05f, 3.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("FOV", &state.camera.fovy, 10.0f, 120.0f, "%.0f deg");
        if (mobileLayout) {
            ImGui::TextDisabled("1 finger look  -  2 fingers pan");
            ImGui::TextDisabled("pinch to dolly");
        } else {
            ImGui::TextDisabled("LMB look  RMB/MMB pan  wheel dolly");
            ImGui::TextDisabled("WASD/QE fly  -  hold Shift = slow");
        }
    }

    // ─── Layers section: fully customizable per-layer appearance ──────────────────
    if (state.hasEvent)
        scene.DrawInspectorUI();

    if (!mobileLayout) {
        ImGui::Separator();
        ImGui::TextDisabled("F12 screenshot  -  Ctrl+Q to quit");
    }

    ImGui::End();

    if (state.hasEvent)
        scene.DrawPidLegendWindow();
}

// ─── Frame ───────────────────────────────────────────────────────────────────

void App::Frame()
{
    HandleEvents();
    Update();

    BeginDrawing();
    ClearBackground(state.background);

    DrawScene();
    CaptureScreenshot();

    rlImGuiBegin();
#ifdef __EMSCRIPTEN__
    // rlImGui sets io.DisplaySize from GetScreenWidth(), which is stale after a
    // resize; override it with the always-current render dimensions.
    ImGui::GetIO().DisplaySize = { (float)SW(), (float)SH() };
#endif
    DrawGui();
    rlImGuiEnd();

    EndDrawing();
}
