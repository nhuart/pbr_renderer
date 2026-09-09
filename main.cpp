#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "renderer/renderer.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: pbr_renderer <scene.json> [--screenshot <output.png>]\n";
        return EXIT_FAILURE;
    }
    std::string screenshotPath;
    for (int i = 2; i < argc - 1; ++i) {
        if (std::string_view(argv[i]) == "--screenshot") {
            screenshotPath = argv[i + 1];
        }
    }
    try {
        Renderer app(argv[1], screenshotPath);
        app.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
