/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>
#include <filamentapp/IBL.h>

#include <filament/Camera.h>
#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>

#include <viewer/AutomationEngine.h>
#include <viewer/AutomationSpec.h>
#include <viewer/SimpleViewer.h>

#include <camutils/Manipulator.h>

#include <getopt/getopt.h>

#include <utils/NameComponentManager.h>

#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat3.h>
#include <math/norm.h>

#include <imgui.h>
#include <filagui/ImGuiExtensions.h>

#include <fstream>
#include <iostream>
#include <string>

#include "generated/resources/gltf_viewer.h"
#include <stb_image.h>

using namespace filament;
using namespace filament::math;
using namespace filament::viewer;

using namespace gltfio;
using namespace utils;

enum MaterialSource {
    GENERATE_SHADERS,
    LOAD_UBERSHADERS,
};

bool screenShotTaken = false;


void writeImage(const char* filename, unsigned char* data, int w, int h)
{
    {
        typedef struct                       /**** BMP file header structure ****/
        {
            unsigned int   bfSize;           /* Size of file */
            unsigned short bfReserved1;      /* Reserved */
            unsigned short bfReserved2;      /* ... */
            unsigned int   bfOffBits;        /* Offset to bitmap data */
        } BITMAPFILEHEADER;

        typedef struct                       /**** BMP file info structure ****/
        {
            unsigned int   biSize;           /* Size of info header */
            int            biWidth;          /* Width of image */
            int            biHeight;         /* Height of image */
            unsigned short biPlanes;         /* Number of color planes */
            unsigned short biBitCount;       /* Number of bits per pixel */
            unsigned int   biCompression;    /* Type of compression to use */
            unsigned int   biSizeImage;      /* Size of image data */
            int            biXPelsPerMeter;  /* X pixels per meter */
            int            biYPelsPerMeter;  /* Y pixels per meter */
            unsigned int   biClrUsed;        /* Number of colors used */
            unsigned int   biClrImportant;   /* Number of important colors */
        } BITMAPINFOHEADER;

        BITMAPFILEHEADER bfh;
        BITMAPINFOHEADER bih;

        /* Magic number for file. It does not fit in the header structure due to alignment requirements, so put it outside */
        unsigned short bfType = 0x4d42;
        bfh.bfReserved1 = 0;
        bfh.bfReserved2 = 0;
        bfh.bfSize = 2 + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + w * h * 3;
        bfh.bfOffBits = 0x36;

        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = w;
        bih.biHeight = h;
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = 0;
        bih.biSizeImage = 0;
        bih.biXPelsPerMeter = 5000;
        bih.biYPelsPerMeter = 5000;
        bih.biClrUsed = 0;
        bih.biClrImportant = 0;

        FILE* file = fopen(filename, "wb");
        if (!file)
        {
            printf("Could not write file\n");
            return;
        }

        /*Write headers*/
        fwrite(&bfType, 1, sizeof(bfType), file);
        fwrite(&bfh, 1, sizeof(bfh), file);
        fwrite(&bih, 1, sizeof(bih), file);
        //fwrite(data,1,  w* h * 3, file);
        /*Write bitmap*/
        for (int y = h - 1; y >= 0; y--) /*Scanline loop backwards*/
        {
            for (int x = 0; x < w; x++) /*Column loop forwards*/
            {
                unsigned char r = data[y * w * 3 + x * 3];
                unsigned char g = data[y * w * 3 + x * 3 + 1];
                unsigned char b = data[y * w * 3 + x * 3 + 2];

                fwrite(&b, 1, 1, file);
                fwrite(&g, 1, 1, file);
                fwrite(&r, 1, 1, file);
            }
        }
        fclose(file);
    }
}



struct App {
    bool screenShotTaken = false;
    Engine* engine;
    SimpleViewer* viewer;
    Config config;
    Camera* mainCamera;
    Texture* groundTexture;
    MaterialInstance* groundMaterialInstance;

    AssetLoader* assetLoader;
    FilamentAsset* asset = nullptr;
    NameComponentManager* names;

    MaterialProvider* materials;
    MaterialSource materialSource = GENERATE_SHADERS;

    gltfio::ResourceLoader* resourceLoader = nullptr;
    bool recomputeAabb = false;

    bool actualSize = false;

    struct Scene {
        Entity groundPlane;
        VertexBuffer* groundVertexBuffer;
        IndexBuffer* groundIndexBuffer;
        Material* groundMaterial;
    } scene;

    // zero-initialized so that the first time through is always dirty.
    ColorGradingSettings lastColorGradingOptions = { 0 };

    ColorGrading* colorGrading = nullptr;

    std::string messageBoxText;
    std::string settingsFile;
    std::string batchFile;

    AutomationSpec* automationSpec = nullptr;
    AutomationEngine* automationEngine = nullptr;
};

#include "gltf_remodel.cpp"

static const char* DEFAULT_IBL = "round_platform_env";

static void printUsage(char* name) {
    std::string exec_name(Path(name).getName());
    std::string usage(
        "SHOWCASE renders the specified glTF file, or a built-in file if none is specified\n"
        "Usage:\n"
        "    SHOWCASE [options] <gltf path>\n"
        "Options:\n"
        "   --help, -h\n"
        "       Prints this message\n\n"
        "   --api, -a\n"
        "       Specify the backend API: opengl (default), vulkan, or metal\n\n"
        "   --batch=<path to JSON file or 'default'>, -b\n"
        "       Start automation using the given JSON spec, then quit the app\n\n"
        "   --headless, -e\n"
        "       Use a headless swapchain; ignored if --batch is not present\n\n"
        "   --ibl=<path>, -i <path>\n"
        "       Override the built-in IBL\n"
        "       path can either be a directory containing IBL data files generated by cmgen,\n"
        "       or, a .hdr equiretangular image file\n\n"
        "   --actual-size, -s\n"
        "       Do not scale the model to fit into a unit cube\n\n"
        "   --recompute-aabb, -r\n"
        "       Ignore the min/max attributes in the glTF file\n\n"
        "   --settings=<path to JSON file>, -t\n"
        "       Apply the settings in the given JSON file\n\n"
        "   --ubershader, -u\n"
        "       Enable ubershaders (improves load time, adds shader complexity)\n\n"
        "   --camera=<camera mode>, -c <camera mode>\n"
        "       Set the camera mode: orbit (default) or flight\n"
        "       Flight mode uses the following controls:\n"
        "           Click and drag the mouse to pan the camera\n"
        "           Use the scroll weel to adjust movement speed\n"
        "           W / S: forward / backward\n"
        "           A / D: left / right\n"
        "           E / Q: up / down\n\n"
        "   --split-view, -v\n"
        "       Splits the window into 4 views\n"
    );
    const std::string from("SHOWCASE");
    for (size_t pos = usage.find(from); pos != std::string::npos; pos = usage.find(from, pos)) {
        usage.replace(pos, from.length(), exec_name);
    }
    std::cout << usage;
}

static std::ifstream::pos_type getFileSize(const char* filename) {
    std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
    return in.tellg();
}

static int handleCommandLineArguments(int argc, char* argv[], App* app) {
    static constexpr const char* OPTSTR = "ha:i:usc:rt:b:ev";
    static const struct option OPTIONS[] = {
        { "help",         no_argument,       nullptr, 'h' },
        { "api",          required_argument, nullptr, 'a' },
        { "batch",        required_argument, nullptr, 'b' },
        { "headless",     no_argument,       nullptr, 'e' },
        { "ibl",          required_argument, nullptr, 'i' },
        { "ubershader",   no_argument,       nullptr, 'u' },
        { "actual-size",  no_argument,       nullptr, 's' },
        { "camera",       required_argument, nullptr, 'c' },
        { "recompute-aabb", no_argument,     nullptr, 'r' },
        { "settings",     required_argument, nullptr, 't' },
        { "split-view",   no_argument,       nullptr, 'v' },
        { nullptr, 0, nullptr, 0 }
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, OPTSTR, OPTIONS, &option_index)) >= 0) {
        std::string arg(optarg ? optarg : "");
        switch (opt) {
            default:
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'a':
                if (arg == "opengl") {
                    app->config.backend = Engine::Backend::OPENGL;
                } else if (arg == "vulkan") {
                    app->config.backend = Engine::Backend::VULKAN;
                } else if (arg == "metal") {
                    app->config.backend = Engine::Backend::METAL;
                } else {
                    std::cerr << "Unrecognized backend. Must be 'opengl'|'vulkan'|'metal'.\n";
                }
                break;
            case 'c':
                if (arg == "flight") {
                    app->config.cameraMode = camutils::Mode::FREE_FLIGHT;
                } else if (arg == "orbit") {
                    app->config.cameraMode = camutils::Mode::ORBIT;
                } else {
                    std::cerr << "Unrecognized camera mode. Must be 'flight'|'orbit'.\n";
                }
                break;
            case 'e':
                app->config.headless = true;
                break;
            case 'i':
                app->config.iblDirectory = arg;
                break;
            case 'u':
                app->materialSource = LOAD_UBERSHADERS;
                break;
            case 's':
                app->actualSize = true;
                break;
            case 'r':
                app->recomputeAabb = true;
                break;
            case 't':
                app->settingsFile = arg;
                break;
            case 'b': {
                app->batchFile = arg;
                break;
            }
            case 'v': {
                app->config.splitView = true;
                break;
            }
        }
    }
    if (app->config.headless && app->batchFile.empty()) {
        std::cerr << "--headless is allowed only when --batch is present." << std::endl;
        app->config.headless = false;
    }
    return optind;
}

static bool loadSettings(const char* filename, Settings* out) {
    auto contentSize = getFileSize(filename);
    if (contentSize <= 0) {
        return false;
    }
    std::ifstream in(filename, std::ifstream::binary | std::ifstream::in);
    std::vector<char> json(static_cast<unsigned long>(contentSize));
    if (!in.read(json.data(), contentSize)) {
        return false;
    }
    JsonSerializer serializer;
    return serializer.readJson(json.data(), contentSize, out);
}
using MinFilter = TextureSampler::MinFilter;
using MagFilter = TextureSampler::MagFilter;
static void createGroundPlane(Engine* engine, Scene* scene, App& app) {
    Path path = FilamentApp::getRootAssetsPath() + "textures/Parquet_flooring_05/Parquet_flooring_05_Color.png";
    if (!path.exists()) {
        std::cerr << "The texture " << path << " does not exist" << std::endl;
        exit(1);
    }
    int w, h, n;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (data == nullptr) {
        std::cerr << "The texture " << path << " could not be loaded" << std::endl;
        exit(1);
    }
    std::cout << "Loaded texture: " << w << "x" << h << std::endl;
    Texture::PixelBufferDescriptor buffer(data, size_t(w * h * 4),
        Texture::Format::RGBA, Texture::Type::UBYTE,
        (Texture::PixelBufferDescriptor::Callback)&stbi_image_free);
    app.groundTexture = Texture::Builder()
        .width(uint32_t(w))
        .height(uint32_t(h))
        .levels(1)
        .sampler(Texture::Sampler::SAMPLER_2D)
        .format(Texture::InternalFormat::RGBA8)
        .build(*engine);
    app.groundTexture->setImage(*engine, 0, std::move(buffer));
    TextureSampler sampler(MinFilter::LINEAR, MagFilter::LINEAR);

    auto& em = EntityManager::get();

    Material* shadowMaterial = Material::Builder()
        .package(GLTF_VIEWER_BAKEDTEXTURE_DATA, GLTF_VIEWER_BAKEDTEXTURE_SIZE)
        .build(*engine);
    app.groundMaterialInstance = shadowMaterial->createInstance();
    app.groundMaterialInstance->setParameter("albedo", app.groundTexture, sampler);
    
    
    /*Material* shadowMaterial = Material::Builder()
            .package(GLTF_VIEWER_GROUNDSHADOW_DATA, GLTF_VIEWER_GROUNDSHADOW_SIZE)
            .build(*engine);
    auto& viewerOptions = app.viewer->getSettings().viewer;
    shadowMaterial->setDefaultParameter("strength", viewerOptions.groundShadowStrength);*/

    const static uint32_t indices[] = {
            0, 1, 2, 2, 3, 0
    };

    Aabb aabb = app.asset->getBoundingBox();
    if (!app.actualSize) {
        mat4f transform = fitIntoUnitCube(aabb, 4);
        aabb = aabb.transform(transform);
    }

    float3 planeExtent{10.0f * aabb.extent().x, 0.0f, 10.0f * aabb.extent().z};

    const static float3 vertices[] = {
            { -planeExtent.x, 0, -planeExtent.z },
            { -planeExtent.x, 0,  planeExtent.z },
            {  planeExtent.x, 0,  planeExtent.z },
            {  planeExtent.x, 0, -planeExtent.z },
    };

    const static float2 uv[] = {
            { 0, 0 },
            { 0, 1 },
            {  1, 1 },
            {  1, 0 },
    };

    short4 tbn = packSnorm16(
            mat3f::packTangentFrame(
                    mat3f{
                            float3{ 1.0f, 0.0f, 0.0f },
                            float3{ 0.0f, 0.0f, 1.0f },
                            float3{ 0.0f, 1.0f, 0.0f }
                    }
            ).xyzw);

    const static short4 normals[] { tbn, tbn, tbn, tbn };

    VertexBuffer* vertexBuffer = VertexBuffer::Builder()
            .vertexCount(4)
            .bufferCount(3)
            .attribute(VertexAttribute::POSITION,
                    0, VertexBuffer::AttributeType::FLOAT3)
            .attribute(VertexAttribute::TANGENTS,
                    1, VertexBuffer::AttributeType::SHORT4)
            .attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::FLOAT2)
            .normalized(VertexAttribute::TANGENTS)
            .build(*engine);

    vertexBuffer->setBufferAt(*engine, 0, VertexBuffer::BufferDescriptor(
            vertices, vertexBuffer->getVertexCount() * sizeof(vertices[0])));
    vertexBuffer->setBufferAt(*engine, 1, VertexBuffer::BufferDescriptor(
            normals, vertexBuffer->getVertexCount() * sizeof(normals[0])));
    vertexBuffer->setBufferAt(*engine, 2, VertexBuffer::BufferDescriptor(
            uv, vertexBuffer->getVertexCount() * sizeof(uv[0])));

    IndexBuffer* indexBuffer = IndexBuffer::Builder()
            .indexCount(6)
            .build(*engine);

    indexBuffer->setBuffer(*engine, IndexBuffer::BufferDescriptor(
            indices, indexBuffer->getIndexCount() * sizeof(uint32_t)));

    Entity groundPlane = em.create();
    RenderableManager::Builder(1)
            .boundingBox({
                    {}, { planeExtent.x, 1e-4f, planeExtent.z }
            })
            .material(0, app.groundMaterialInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                    vertexBuffer, indexBuffer, 0, 6)
            .culling(false)
            .receiveShadows(true)
            .castShadows(false)
            .build(*engine, groundPlane);

    scene->addEntity(groundPlane);

    auto& tcm = engine->getTransformManager();
    tcm.setTransform(tcm.getInstance(groundPlane),
            mat4f::translation(float3{ 0, aabb.min.y, -4 }));

    auto& rcm = engine->getRenderableManager();
    auto instance = rcm.getInstance(groundPlane);
    rcm.setLayerMask(instance, 0xff, 0x00);

    app.scene.groundPlane = groundPlane;
    app.scene.groundVertexBuffer = vertexBuffer;
    app.scene.groundIndexBuffer = indexBuffer;
    app.scene.groundMaterial = shadowMaterial;
}

static LinearColor inverseTonemapSRGB(sRGBColor x) {
    return (x * -0.155) / (x - 1.019);
}

static float sGlobalScale = 1.0f;
static float sGlobalScaleAnamorphism = 0.0f;

int main(int argc, char** argv) {
    App app;

    app.config.title = "Filament";
    app.config.iblDirectory = FilamentApp::getRootAssetsPath() + DEFAULT_IBL;
    app.config.backend = Engine::Backend::OPENGL;
    app.config.headless = true;

    //int optionIndex = handleCommandLineArguments(argc, argv, &app);

    //utils::Path filename = R"(c:\Users\mj185102\OneDrive - NCR Corporation\Documents\untitled.glb)";
    //utils::Path filename = R"(c:\dev\filament\samples\scenes\DemoScene1.glb)";
    utils::Path filename = R"(c:\Dev\public\filament\samples\scenes\DemoScene1.glb)";

    if (argc >= 2) {
        feed_to_scene_generator(argv[1]);
    }

    /* int num_args = argc - optionIndex;
    if (num_args >= 1) {
        filename = argv[optionIndex];
        if (!filename.exists()) {
            std::cerr << "file " << filename << " not found!" << std::endl;
            return 1;
        }
        if (filename.isDirectory()) {
            auto files = filename.listContents();
            for (auto file : files) {
                if (file.getExtension() == "gltf" || file.getExtension() == "glb") {
                    filename = file;
                    break;
                }
            }
            if (filename.isDirectory()) {
                std::cerr << "no glTF file found in " << filename << std::endl;
                return 1;
            }
        }
    }*/

    auto loadAsset = [&app](utils::Path filename) {
        // Peek at the file size to allow pre-allocation.
        long contentSize = static_cast<long>(getFileSize(filename.c_str()));
        if (contentSize <= 0) {
            std::cerr << "Unable to open " << filename << std::endl;
            exit(1);
        }

        // Consume the glTF file.
        std::ifstream in(filename.c_str(), std::ifstream::binary | std::ifstream::in);
        std::vector<uint8_t> buffer(static_cast<unsigned long>(contentSize));
        if (!in.read((char*) buffer.data(), contentSize)) {
            std::cerr << "Unable to read " << filename << std::endl;
            exit(1);
        }

        // Parse the glTF file and create Filament entities.
        if (filename.getExtension() == "glb") {
            app.asset = app.assetLoader->createAssetFromBinary(buffer.data(), buffer.size());
        } else {
            app.asset = app.assetLoader->createAssetFromJson(buffer.data(), buffer.size());
        }
        buffer.clear();
        buffer.shrink_to_fit();

        if (!app.asset) {
            std::cerr << "Unable to parse " << filename << std::endl;
            exit(1);
        }
    };

    auto loadResources = [&app] (utils::Path filename) {
        // Load external textures and buffers.
        std::string gltfPath = filename.getAbsolutePath();
        ResourceConfiguration configuration = {};
        configuration.engine = app.engine;
        configuration.gltfPath = gltfPath.c_str();
        configuration.recomputeBoundingBoxes = app.recomputeAabb;
        configuration.normalizeSkinningWeights = true;
        if (!app.resourceLoader) {
            app.resourceLoader = new gltfio::ResourceLoader(configuration);
        }
        app.resourceLoader->loadResources(app.asset);

        // Load animation data then free the source hierarchy.
        app.asset->getAnimator();
        app.asset->releaseSourceData();

        auto ibl = FilamentApp::get().getIBL();
        if (ibl) {
            app.viewer->setIndirectLight(ibl->getIndirectLight(), ibl->getSphericalHarmonics());
        }
    };

    auto animate = [&app](Engine* engine, View* view, double now) {
        //adjust_scene(app, engine, view, now);
        // 
        //engine->getTransformManager().getChildren(engine->getTransformManager().getInstance(), &entity, 1);
        // Add renderables to the scene as they become ready.
        app.viewer->populateScene(app.asset);

        app.viewer->applyAnimation(now);
    };

    auto resize = [&app](Engine* engine, View* view) {
        Camera& camera = view->getCamera();
        if (&camera == app.mainCamera) {
            // Don't adjut the aspect ratio of the main camera, this is done inside of
            // FilamentApp.cpp
            return;
        }
        const Viewport& vp = view->getViewport();
        double aspectRatio = (double)vp.width / vp.height;
        camera.setScaling({ 1.0 / aspectRatio, 1.0 });
    };

    auto setup = [&](Engine* engine, View* view, Scene* scene) {
        app.engine = engine;
        app.names = new NameComponentManager(EntityManager::get());
        app.viewer = new SimpleViewer(engine, scene, view, 410);
        app.viewer->getSettings().viewer.autoScaleEnabled = !app.actualSize;

        //view->setScreenSpaceRefractionEnabled(false);
        //view->setViewport(Viewport)
        const bool batchMode = !app.batchFile.empty();

        // If no custom spec has been provided, or if in interactive mode, load the default spec.
        if (!app.automationSpec) {
            app.automationSpec = AutomationSpec::generateDefaultTestCases();
        }

        app.automationEngine = new AutomationEngine(app.automationSpec, &app.viewer->getSettings());

        app.materials = (app.materialSource == GENERATE_SHADERS) ?
            createMaterialGenerator(engine) : createUbershaderLoader(engine);
        app.assetLoader = AssetLoader::create({ engine, app.materials, app.names });
        app.mainCamera = &view->getCamera();

        loadAsset(filename); // + added
        loadResources(filename);

        createGroundPlane(engine, scene, app);

        auto& tm = engine->getTransformManager();
        auto& rm = engine->getRenderableManager();
        auto& lm = engine->getLightManager();


        std::function<void(utils::Entity)> treeNode;

        auto renderableTreeItem = [&rm](utils::Entity entity) {
            auto instance = rm.getInstance(entity);
            bool scaster = rm.isShadowCaster(instance);
            rm.setCastShadows(instance, scaster);
        };

        auto lightTreeItem = [&lm](utils::Entity entity) {
            auto instance = lm.getInstance(entity);
            bool lcaster = lm.isShadowCaster(instance);
            lm.setShadowCaster(instance, lcaster);
        };

        treeNode = [&](utils::Entity entity) {
            auto tinstance = tm.getInstance(entity);
            auto rinstance = rm.getInstance(entity);
            auto linstance = lm.getInstance(entity);
            intptr_t treeNodeId = 1 + entity.getId();

            const char* name = app.asset->getName(entity);

            std::vector<utils::Entity> children(tm.getChildCount(tinstance));
            if (rinstance) {
                renderableTreeItem(entity);
            }
            if (linstance) {
                lightTreeItem(entity);
            }
            tm.getChildren(tinstance, children.data(), children.size());
            for (auto ce : children) {
                treeNode(ce);
            }
        };

        // -------------------
        // Override material
        // -------------------

        /*auto entity = app.asset->getFirstEntityByName("Guitar");

        auto mDefaultColorMaterial = Material::Builder()
            .package(GLTF_VIEWER_AIDEFAULTMAT_DATA, GLTF_VIEWER_AIDEFAULTMAT_SIZE)
            .build(*engine);

        mDefaultColorMaterial->setDefaultParameter("baseColor", RgbType::sRGB, float3{ 0.0, 0.5, 1 });
        mDefaultColorMaterial->setDefaultParameter("metallic", 2.4f);
        mDefaultColorMaterial->setDefaultParameter("roughness", 2.8f);
        mDefaultColorMaterial->setDefaultParameter("reflectance", 0.8f);

        engine->getRenderableManager().setMaterialInstanceAt(engine->getRenderableManager().getInstance(entity), 0, mDefaultColorMaterial->getDefaultInstance());*/
        // ------------------------


        // -------------------
        // Set sun from gltf
        // -------------------
        /*auto sunEntity = app.asset->getFirstEntityByName("Sun");

        mat4f gltfSunTransform = engine->getTransformManager().getWorldTransform(engine->getTransformManager().getInstance(sunEntity));

        gltfSunTransform[3] = {};

        auto rotX2 = mat4f::rotation(-F_PI_2, float3{ 1, 0, 0 });

        auto pointing = mat4f::translation(float3{ 0,0,1 });
        pointing *= gltfSunTransform * rotX2;

        auto lightCount = scene->getLightCount();
        
        float3 direction = { pointing[0].z, pointing[1].z, -pointing[2].z };


        size_t lightComponents = engine->getLightManager().getComponentCount();
        for (int i = 0; i < lightComponents; ++i)
        {
            const auto& lightEntity = engine->getLightManager().getEntities()[i];
            auto instance = engine->getLightManager().getInstance(lightEntity);
            
            auto type = engine->getLightManager().getType(instance);
            engine->getLightManager().setDirection(instance, direction);

        }*/
        // ------------------------

        treeNode(app.asset->getRoot());


        AmbientOcclusionOptions ssao;

        int quality = (int) ssao.quality;
        int lowpass = (int) ssao.lowPassFilter;
        ssao.upsampling = View::QualityLevel::ULTRA;
        ssao.lowPassFilter = (View::QualityLevel) lowpass;
        ssao.quality = (View::QualityLevel) quality;

        //            int sampleCount = ssao.ssct.sampleCount;
        //            ImGui::Checkbox("Enabled##dls", &ssao.ssct.enabled);
        //            ImGui::SliderFloat("Cone angle", &ssao.ssct.lightConeRad, 0.0f, (float)M_PI_2);
        //            ImGui::SliderFloat("Shadow Distance", &ssao.ssct.shadowDistance, 0.0f, 10.0f);
        //            ImGui::SliderFloat("Contact dist max", &ssao.ssct.contactDistanceMax, 0.0f, 100.0f);
        //            ImGui::SliderFloat("Intensity##dls", &ssao.ssct.intensity, 0.0f, 10.0f);
        //            ImGui::SliderFloat("Depth bias", &ssao.ssct.depthBias, 0.0f, 1.0f);
        //            ImGui::SliderFloat("Depth slope bias", &ssao.ssct.depthSlopeBias, 0.0f, 1.0f);
        //            ImGui::SliderInt("Sample Count", &sampleCount, 1, 32);
        //            ImGuiExt::DirectionWidget("Direction##dls", ssao.ssct.lightDirection.v);
        //            ssao.ssct.sampleCount = sampleCount;


        VsmShadowOptions vsmShadowOptions;
        vsmShadowOptions.anisotropy = 0;
        
        ssao.enabled = true;
        BloomOptions bloom;
        bloom.enabled = true;
        FogOptions fog;
        DepthOfFieldOptions dof;
        VignetteOptions vignette;
        RenderQuality renderQuality;
        TemporalAntiAliasingOptions taa;
        DynamicLightingSettings dynamicLighting;

        view->setSampleCount(4);
        view->setAntiAliasing(AntiAliasing::FXAA);
        view->setTemporalAntiAliasingOptions(taa);
        view->setAmbientOcclusionOptions(ssao);
        view->setBloomOptions(bloom);
        view->setFogOptions(fog);
        view->setDepthOfFieldOptions(dof);
        view->setVignetteOptions(vignette);
        view->setDithering(Dithering::TEMPORAL);
        view->setRenderQuality(renderQuality);
        view->setDynamicLightingOptions(dynamicLighting.zLightNear,
            dynamicLighting.zLightFar);
        view->setShadowType(ShadowType::PCF);
        view->setVsmShadowOptions(vsmShadowOptions);
        view->setPostProcessingEnabled(true);

        LightSettings light;
        lm.forEachComponent([&lm, &light](utils::Entity e, LightManager::Instance ci) {
            lm.setShadowOptions(ci, light.shadowOptions);
            lm.setShadowCaster(ci, light.enableShadows);
            });
    };

    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        app.automationEngine->terminate();
        app.resourceLoader->asyncCancelLoad();
        app.assetLoader->destroyAsset(app.asset);
        app.materials->destroyMaterials();

        engine->destroy(app.groundMaterialInstance);
        engine->destroy(app.scene.groundPlane);
        engine->destroy(app.scene.groundVertexBuffer);
        engine->destroy(app.scene.groundIndexBuffer);
        engine->destroy(app.colorGrading);
        engine->destroy(app.groundTexture);
        engine->destroy(app.scene.groundMaterial);

        delete app.viewer;
        delete app.materials;
        delete app.names;

        AssetLoader::destroy(&app.assetLoader);
    };

    

    auto gui = [&app](Engine* engine, View* view) {
        //filament::viewer::applySettings(app.viewer->getSettings().view, view);
        //app.viewer->updateUserInterface();
        //app.viewer->updateRootTransform();
        //FilamentApp::get().setSidebarWidth(app.viewer->getSidebarWidth());

        
    };

    auto preRender = [&app](Engine* engine, View* view, Scene* scene, Renderer* renderer) {
        auto& rcm = engine->getRenderableManager();
        auto instance = rcm.getInstance(app.scene.groundPlane);
        //const auto viewerOptions = app.automationEngine->getViewerOptions();
        const auto& dofOptions = app.viewer->getSettings().view.dof;
        rcm.setLayerMask(instance,
                0xff, 0xff);

        // Note that this focal length might be different from the slider value because the
        // automation engine applies Camera::computeEffectiveFocalLength when DoF is enabled.
        //FilamentApp::get().getCameraFocalLength() = viewerOptions.cameraFocalLength;

        const size_t cameraCount = app.asset->getCameraEntityCount();
        view->setCamera(app.mainCamera);


        //app.scene.groundMaterial->setDefaultParameter(
        //        "strength", viewerOptions.groundShadowStrength);

        // This applies clear options, the skybox mask, and some camera settings.
        Camera& camera = view->getCamera();
        Skybox* skybox = scene->getSkybox();

        auto cameraEntity = app.asset->getFirstEntityByName("Camera");

        mat4f gltfCameraTransform = engine->getTransformManager().getWorldTransform(engine->getTransformManager().getInstance(cameraEntity));

        // Camera looks down by default, we need to rotate it
        auto rotX2 = mat4f::rotation(-F_PI_2, float3{ 1, 0, 0 });
        gltfCameraTransform *= rotX2;

        camera.setModelMatrix(gltfCameraTransform);

        const utils::Entity* cameras = app.asset->getCameraEntities();
        //Camera* camerap = engine->getCameraComponent(cameras[currentCamera - 1]);

        applySettings(app.viewer->getSettings().viewer, &camera, skybox, renderer);

        auto a = camera.getPosition();
        auto b = camera.getForwardVector();
        auto c = camera.getFieldOfViewInDegrees(Camera::Fov::VERTICAL);

        const Viewport& vp = view->getViewport();
        double aspectRatio = (double)vp.width / vp.height;
        auto gggsfd = camera.getScaling();
        camera.setScaling({ 1, 1 });

        // Check if color grading has changed.
        ColorGradingSettings& options = app.viewer->getSettings().view.colorGrading;
        if (options.enabled) {
            if (options != app.lastColorGradingOptions) {
                ColorGrading *colorGrading = createColorGrading(options, engine);
                engine->destroy(app.colorGrading);
                app.colorGrading = colorGrading;
                app.lastColorGradingOptions = options;
            }
            view->setColorGrading(app.colorGrading);
        } else {
            view->setColorGrading(nullptr);
        }

        view->setDynamicResolutionOptions({
                .minScale = {
                        lerp(sGlobalScale, 1.0f,
                                sGlobalScaleAnamorphism >= 0.0f ? sGlobalScaleAnamorphism : 0.0f),
                        lerp(sGlobalScale, 1.0f,
                                sGlobalScaleAnamorphism <= 0.0f ? -sGlobalScaleAnamorphism : 0.0f),
                },
                .maxScale = {
                        lerp(sGlobalScale, 1.0f,
                                sGlobalScaleAnamorphism >= 0.0f ? sGlobalScaleAnamorphism : 0.0f),
                        lerp(sGlobalScale, 1.0f,
                                sGlobalScaleAnamorphism <= 0.0f ? -sGlobalScaleAnamorphism : 0.0f),
                },
                .enabled = sGlobalScale != 1.0f,
        });


    };

    struct XXX
    {
        const Viewport* vp;
    };

    auto postRender = [&app](Engine* engine, View* view, Scene* scene, Renderer* renderer) {
        if (app.automationEngine->shouldClose()) {
            FilamentApp::get().close();
            return;
        }
        AutomationEngine::ViewerContent content = {
            .view = view,
            .renderer = renderer,
            .materials = app.asset->getMaterialInstances(),
            .materialCount = app.asset->getMaterialInstanceCount(),
        };
        //app.automationEngine->tick(content, ImGui::GetIO().DeltaTime);
       // return;
        const Viewport& vp = view->getViewport();
        const size_t byteCount = vp.width * vp.height * 3;

        if (app.screenShotTaken)
        {
            //return;
        }

        app.screenShotTaken = true;

        auto xxx = new XXX();
        xxx->vp = &vp;

        // Create a buffer descriptor that writes the PPM after the data becomes ready on the CPU.
        backend::PixelBufferDescriptor buffer(
            new uint8_t[byteCount], byteCount,
            backend::PixelBufferDescriptor::PixelDataFormat::RGB,
            backend::PixelBufferDescriptor::PixelDataType::UBYTE,
            [](void* buffer, size_t size, void* user) {

                XXX* xxx = (XXX*)user;
                const Viewport& vp = *xxx->vp;
                Path out(R"(c:\dev\public\filament\samples\scenes\DemoScene1.bmp)");
                writeImage(out.c_str(), static_cast<unsigned char*>(buffer), vp.width, vp.height);
                /*std::ofstream ppmStream(out);
                ppmStream << "P6 " << vp.width << " " << vp.height << " " << 255 << std::endl;
                ppmStream.write(static_cast<char*>(buffer), vp.width * vp.height * 3);*/
                delete[] static_cast<uint8_t*>(buffer);
                FilamentApp::get().close();
                delete xxx;
            },
            xxx
            );

        // Invoke readPixels asynchronously.
        renderer->readPixels((uint32_t)vp.left, (uint32_t)vp.bottom, vp.width, vp.height,
            std::move(buffer));
    };

    FilamentApp& filamentApp = FilamentApp::get();
    filamentApp.animate(animate);
    filamentApp.resize(resize);

    filamentApp.setDropHandler([&] (std::string path) {
        app.resourceLoader->asyncCancelLoad();
        app.resourceLoader->evictResourceData();
        app.viewer->removeAsset();
        app.assetLoader->destroyAsset(app.asset);
        loadAsset(path);
        loadResources(path);
    });

    filamentApp.run(app.config, setup, cleanup, nullptr, preRender, postRender);

    return 0;
}