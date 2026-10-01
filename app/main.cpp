#include <SDL3/SDL_main.h>

#include <filesystem>

#include "Application.h"
#include "core/Log.h"

int main(int argc, char *argv[])
{
    Application app;
    app.init();

    if (argc > 1) {
        app.loadData(std::filesystem::path(argv[1]));
    } else {
        core::log("No model given; usage: vulkanapp path/to/scene.gltf");
    }

    app.run();
    app.shutdown();
    return 0;
}
