/*
#    Copyright (C) 2026 Veia <h27ck@proton.me>
*/

#include "core/application.h"

// Global pointers for legacy code/externs
DummyGlobal* gloParent = new DummyGlobal();
DummyGLView* glView = new DummyGLView();
Viewport* gViewport = nullptr;
Application* gApplication = nullptr;

int main(int, char**) {
    Application app;
    gApplication = &app;
    if (!app.Initialize()) {
        return 1;
    }

    app.Run();
    app.Shutdown();

    delete glView;
    delete gloParent;

    return 0;
}
