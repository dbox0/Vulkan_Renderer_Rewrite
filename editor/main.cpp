#include <SDL3/SDL_main.h>

#include "Application.h"
#include "Editor.h"

int main(int argc, char *argv[])
{
    Application app;
    app.init();

    Editor editor;
    editor.attach(app);
    if (argc > 1) {
        editor.openModel(argv[1]);
    }

    app.run();

    editor.detach();
    app.shutdown();
    return 0;
}
