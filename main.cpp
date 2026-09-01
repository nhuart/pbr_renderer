#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "renderer/renderer.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: pbr_renderer <scene.json>\n";
        return EXIT_FAILURE;
    }
    try {
        Renderer app(argv[1]);
        app.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
