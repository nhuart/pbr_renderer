#include "app_runner.h"
#include "camera.h"
#include "light.h"
#include "mesh_loader.h"
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
#include <filament/IndirectLight.h>
#include <filament/Scene.h>
#include <filament/ToneMapper.h>
#include <filament/View.h>

#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>
#include <utils/Path.h>

#include <materials/uberarchive.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

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
    std::vector<utils::Entity> lightEntities;
    IndirectLight* ambientIndirectLight = nullptr;
    bool screenshotCaptured = false;
    int warmupFramesAfterLoad = 0;
    std::string screenshotFile;
};

static void setupApp(App& app, Engine* engine, View* view, Scene* scene) {
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
    view->setShadowType(app.scene.shadowType);

    if (app.scene.ambientOcclusion) {
        view->setAmbientOcclusionOptions(*app.scene.ambientOcclusion);
    }

    double aspect = double(app.scene.width) / double(app.scene.height);
    applyCamera(view->getCamera(), app.scene.camera, aspect);

    createLights(*engine, *scene, app.scene.lights, app.lightEntities);

    if (app.scene.iblDir.empty() && app.scene.ambientIntensity > 0.0f) {
        float sh0 = app.scene.ambientIntensity / float(std::sqrt(4.0 * M_PI));
        math::float3 sh[1] = { { sh0, sh0, sh0 } };
        app.ambientIndirectLight =
                IndirectLight::Builder().irradiance(1, sh).intensity(30000.0f).build(*engine);
        scene->setIndirectLight(app.ambientIndirectLight);
    }

    if (!app.scene.showSkybox) {
        scene->setSkybox(nullptr);
    }

    MeshLoaderContext ctx{ engine, app.appLoader, app.gltfLoader };
    for (auto& entry: app.scene.meshes) {
        loadMesh(ctx, entry);
    }
}

static void cleanupApp(App& app, Engine* engine) {
    for (auto& entry: app.scene.meshes) {
        destroyMesh(*app.gltfLoader, entry);
    }
    destroyLights(*engine, app.lightEntities);
    if (app.ambientIndirectLight) {
        engine->destroy(app.ambientIndirectLight);
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

void runScene(SceneDesc scene, const char* screenshotName) {
    auto app = std::make_shared<App>();
    app->scene = std::move(scene);

    auto outDir = resolveOutputDir(utils::Path::getCurrentExecutable().getParent().c_str());
    std::filesystem::create_directories(outDir);
    app->screenshotFile = (outDir / (std::string(screenshotName) + ".png")).string();

    auto desktopLoader =
            std::make_unique<filament::app::DesktopAssetLoader>(utils::Path::getCurrentDirectory());
    app->appLoader = desktopLoader.get();

    double aspect = double(app->scene.width) / double(app->scene.height);

    auto setup = [app](Engine* engine, View* view, Scene* scene) {
        setupApp(*app, engine, view, scene);
    };

    auto cleanup = [app](Engine* engine, View*, Scene*) { cleanupApp(*app, engine); };

    auto animate = [app](Engine*, View* view, double) { animateApp(*app, view); };

    auto postRender = [app, aspect](Engine*, View* view, Scene*, Renderer* renderer) {
        // Re-apply camera every frame to prevent the manipulator from overriding it.
        applyCamera(view->getCamera(), app->scene.camera, aspect);

        if (app->screenshotCaptured) {
            return;
        }
        bool allLoaded = std::all_of(app->scene.meshes.begin(), app->scene.meshes.end(),
                [](MeshEntry const& entry) { return entry.loaded; });
        if (allLoaded) {
            // Wait for 2 frames after load so the GPU has fully uploaded all mesh data.
            static constexpr int kWarmupFramesAfterLoad = 2;
            if (++app->warmupFramesAfterLoad >= kWarmupFramesAfterLoad) {
                app->screenshotCaptured = true;
                captureScreenshot(*renderer, *view, app->screenshotFile);
            }
        }
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
}
