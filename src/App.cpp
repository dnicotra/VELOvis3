#include "App.hpp"

#include "rlImGui.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

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
#endif

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
#ifdef __EMSCRIPTEN__
    double dpr = device_pixel_ratio();
    dpiScale   = (float)dpr;
    int cssW   = canvas_width();
    int cssH   = canvas_height();
    int w      = (int)(cssW * dpr + 0.5);
    int h      = (int)(cssH * dpr + 0.5);
#else
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    int w = 1280;
    int h = 720;
#endif
    SetTraceLogLevel(LOG_WARNING); // quiet raylib's INFO spam
    InitWindow(w, h, "dear_raylib");
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
    state.pos = { SW() * 0.5f, SH() * 0.5f };

    // Anything printed to stdout/stderr shows up in the web console pane.
    printf("dear_raylib started: %d x %d (dpi scale %.2f)\n", SW(), SH(), dpiScale);
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

// ─── Input ───────────────────────────────────────────────────────────────────

void App::HandleEvents()
{
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Q))
        CloseWindow();

    if (IsKeyPressed(KEY_SPACE))
        state.paused = !state.paused;

#ifdef __EMSCRIPTEN__
    // Detect canvas buffer resizes (e.g. browser window resize). canvas_buf_*
    // are physical pixels; divide by DPR before SetWindowSize, which (via GLFW)
    // treats its arguments as CSS pixels.
    int cw = canvas_buf_width(), ch = canvas_buf_height();
    if (cw != lastW || ch != lastH)
    {
        lastW = cw; lastH = ch;
        double dpr = device_pixel_ratio();
        SetWindowSize((int)(cw / dpr + 0.5), (int)(ch / dpr + 0.5));
    }
#endif
}

// ─── Update ────────────────────────────────────────────────────────────────────

void App::Update()
{
    if (state.paused) return;

    float dt = GetFrameTime();
    state.pos.x += state.vel.x * state.speed * dt;
    state.pos.y += state.vel.y * state.speed * dt;

    // Bounce off the window edges.
    if (state.pos.x - state.radius < 0)        { state.pos.x = state.radius;            state.vel.x =  fabsf(state.vel.x); }
    if (state.pos.x + state.radius > SW())      { state.pos.x = SW() - state.radius;      state.vel.x = -fabsf(state.vel.x); }
    if (state.pos.y - state.radius < 0)        { state.pos.y = state.radius;            state.vel.y =  fabsf(state.vel.y); }
    if (state.pos.y + state.radius > SH())      { state.pos.y = SH() - state.radius;      state.vel.y = -fabsf(state.vel.y); }
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

void App::DrawScene()
{
    DrawCircleV(state.pos, state.radius, state.shapeColor);

    const char* msg = "raylib + Dear ImGui template";
    int fontSize = 20;
    int tw = MeasureText(msg, fontSize);
    DrawText(msg, (SW() - tw) / 2, SH() - 40, fontSize, Fade(WHITE, 0.6f));
}

void App::DrawGui()
{
    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Once);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

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

    ImGui::Separator();

    ImGui::Checkbox("Paused (space)", &state.paused);
    ImGui::SliderFloat("Speed", &state.speed, 0.0f, 600.0f, "%.0f px/s");
    ImGui::SliderFloat("Radius", &state.radius, 5.0f, 200.0f, "%.0f px");

    float shape[4] = { state.shapeColor.r / 255.0f, state.shapeColor.g / 255.0f,
                       state.shapeColor.b / 255.0f, state.shapeColor.a / 255.0f };
    if (ImGui::ColorEdit4("Shape", shape)) {
        state.shapeColor = { (unsigned char)(shape[0] * 255), (unsigned char)(shape[1] * 255),
                             (unsigned char)(shape[2] * 255), (unsigned char)(shape[3] * 255) };
    }

    float bg[3] = { state.clearColor.r / 255.0f, state.clearColor.g / 255.0f, state.clearColor.b / 255.0f };
    if (ImGui::ColorEdit3("Background", bg)) {
        state.clearColor = { (unsigned char)(bg[0] * 255), (unsigned char)(bg[1] * 255),
                             (unsigned char)(bg[2] * 255), 255 };
    }

    ImGui::Separator();
    ImGui::Checkbox("Show ImGui demo window", &state.showImGuiDemo);
    ImGui::TextDisabled("Ctrl+Q to quit");

    ImGui::End();

    if (state.showImGuiDemo)
        ImGui::ShowDemoWindow(&state.showImGuiDemo);
}

// ─── Frame ───────────────────────────────────────────────────────────────────

void App::Frame()
{
    HandleEvents();
    Update();

    BeginDrawing();
    ClearBackground(state.clearColor);

    DrawScene();

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
