#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "renderer/renderer.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: pbr_renderer <scene.json> [--screenshot <output.png>] "
                     "[--exit-after-screenshot]\n";
        return EXIT_FAILURE;
    }
    std::string screenshotPath;
    bool exitAfterScreenshot = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (std::string_view(argv[i]) == "--exit-after-screenshot") {
            exitAfterScreenshot = true;
        }
    }
    try {
        Renderer app(argv[1], screenshotPath, exitAfterScreenshot);
        app.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
