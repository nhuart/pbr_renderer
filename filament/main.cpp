#include "camera.h"
#include "mesh_loader.h"
#include "scenes/bunny_ibl_scene.h"
#include "screenshot.h"

#include <backend/DriverEnums.h>
#include <filamentapp/AssetLoader.h>
#include <filamentapp/DesktopAssetLoader.h>
#include <filamentapp/FilamentApp2.h>
#include <filamentapp/SDLDisplayManager.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/NodeManager.h>

#include <filament/Camera.h>
#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/ToneMapper.h>
#include <filament/View.h>

#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>
#include <utils/Path.h>

#include <materials/uberarchive.h>

#include <filesystem>
#include <memory>

using namespace filament;
using namespace filament::gltfio;
using namespace utils;

struct App {
    Engine* engine = nullptr;
    filament::app::AssetLoader* appLoader = nullptr;
    gltfio::AssetLoader* gltfLoader = nullptr;
    MaterialProvider* materials = nullptr;
    NameComponentManager* names = nullptr;
    ColorGrading* colorGrading = nullptr;
    SceneDesc scene;
    bool screenshotCaptured = false;
    int warmupFramesAfterLoad = 0;
};


static void setupApp(App& app, Engine* engine, View* view) {
    app.engine = engine;
    app.names = new NameComponentManager(EntityManager::get());
    app.materials =
            createUbershaderProvider(engine, UBERARCHIVE_DEFAULT_DATA, UBERARCHIVE_DEFAULT_SIZE);
    app.gltfLoader = gltfio::AssetLoader::create({ engine, app.materials, app.names });

    ReinhardToneMapper reinhard;
    app.colorGrading = ColorGrading::Builder().toneMapper(&reinhard).build(*engine);
    view->setColorGrading(app.colorGrading);

    view->setAntiAliasing(View::AntiAliasing::NONE);
    view->setDithering(View::Dithering::NONE);

    double aspect = double(app.scene.width) / double(app.scene.height);
    applyCamera(view->getCamera(), app.scene.camera, aspect);

    MeshLoaderContext ctx{ engine, app.appLoader, app.gltfLoader };
    for (auto& entry: app.scene.meshes) {
        loadMesh(ctx, entry);
    }
}

static void cleanupApp(App& app, Engine* engine) {
    for (auto& entry: app.scene.meshes) {
        destroyMesh(*app.gltfLoader, entry);
    }
    app.materials->destroyMaterials();
    delete app.materials;
    delete app.names;
    gltfio::AssetLoader::destroy(&app.gltfLoader);
    if (app.colorGrading) {
        engine->destroy(app.colorGrading);
    }
}

static void animateApp(App& app, View* view) {
    for (auto& entry: app.scene.meshes) {
        if (!entry.asset || entry.loaded) {
            continue;
        }
        entry.resourceLoader->asyncUpdateLoad();
        if (entry.resourceLoader->asyncGetLoadProgress() < 1.0f) {
            continue;
        }

        entry.loaded = true;
        applyMaterial(*app.engine, entry);

        auto* scene = view->getScene();
        if (scene) {
            addMeshToScene(*scene, entry);
        }
    }
}

int main() {
    auto app = std::make_shared<App>();
    app->scene = buildBunnyIblScene();

    auto desktopLoader =
            std::make_unique<filament::app::DesktopAssetLoader>(utils::Path::getCurrentDirectory());
    app->appLoader = desktopLoader.get();

    auto setup = [app](Engine* engine, View* view, Scene*) { setupApp(*app, engine, view); };

    auto cleanup = [app](Engine* engine, View*, Scene*) { cleanupApp(*app, engine); };

    auto animate = [app](Engine*, View* view, double) { animateApp(*app, view); };

    // Resolve output directory relative to the executable.
    auto outDir = resolveOutputDir(utils::Path::getCurrentExecutable().getParent().c_str());
    std::filesystem::create_directories(outDir);
    auto screenshotFile = (outDir / "filament_scene.png").string();

    double aspect = double(app->scene.width) / double(app->scene.height);

    auto postRender = [app, screenshotFile, aspect](Engine*, View* view, Scene*,
                              Renderer* renderer) {
        if (!app->screenshotCaptured) {
            bool allLoaded = std::all_of(app->scene.meshes.begin(), app->scene.meshes.end(),
                    [](MeshEntry const& entry) { return entry.loaded; });
            if (allLoaded) {
                // Wait for 2 frames after load so the GPU has fully uploaded all mesh data.
                static constexpr int kWarmupFramesAfterLoad = 2;
                if (++app->warmupFramesAfterLoad >= kWarmupFramesAfterLoad) {
                    app->screenshotCaptured = true;
                    captureScreenshot(*renderer, *view, screenshotFile);
                }
            }
        }
        // Re-apply camera every frame to prevent the manipulator from overriding it.
        applyCamera(view->getCamera(), app->scene.camera, aspect);
    };

    auto sdlDM =
            std::make_unique<filament::app::SDLDisplayManager>(filament::backend::Backend::OPENGL);

    auto fApp = FilamentApp2::Builder()
                        .title(utils::CString(app->scene.title.c_str()))
                        .size(app->scene.width, app->scene.height)
                        .iblDirectory(utils::CString(app->scene.iblDir.c_str()))
                        .cameraHome(app->scene.camera.eye, app->scene.camera.target)
                        .displayManager(sdlDM.get())
                        .assetLoader(app->appLoader)
                        .postRender(postRender)
                        .setup(setup)
                        .cleanup(cleanup)
                        .animation(animate)
                        .build();

    fApp->run();
    return 0;
}
