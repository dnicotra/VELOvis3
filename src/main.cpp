#include "App.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

// Emscripten cannot block inside main(); it calls this free function once per
// browser animation frame instead.
static App* gApp = nullptr;

static void emscripten_loop() {
    gApp->Frame();
}
#endif

int main() {
#ifdef __EMSCRIPTEN__
    gApp = new App();
    // fps=0  -> use the browser's requestAnimationFrame rate (~60 fps)
    // inf=1  -> this call never returns; the browser drives the loop
    emscripten_set_main_loop(emscripten_loop, 0, 1);
    delete gApp; // unreachable when inf=1, kept for symmetry
#else
    App app;
    while (!app.ShouldClose()) {
        app.Frame();
    }
#endif
    return 0;
}
