#include <random>

std::mt19937_64 prg;
std::uniform_int_distribution<unsigned short> dis;

void feed_to_scene_generator(char* entropy)
{
    printf("Feeding %s to the PRG\n", entropy);
    auto s = std::string(entropy);
	std::seed_seq seed(s.begin(), s.end());
	prg.seed(seed);
};

static unsigned short nextNumber() {
    return dis(prg);
}

void transform(App &app, Engine* engine, const char* entityName, bool upsideDownAllowed) {
    auto entity = app.asset->getFirstEntityByName(entityName);

    if (nextNumber() % 4 == 0) {  // kill 25% of objects
        auto away = mat4f(
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f
        );
        engine->getTransformManager().setTransform(engine->getTransformManager().getInstance(entity), away);
        printf("Hiding %s\n", entityName);
    } else {
        /* Translate (slide) along every coordinate */
        int x, y, z;

        const float translationOffsetsX[] = { -4, -2, 0, 2, 5 };    // from left to right
        const float translationOffsetsY[] = { -4, -2, 0, 2, 3.1 };  // upwards
        const float translationOffsetsZ[] = { -20, -4, 0, 2.2 };     // toward the viewer
        x = y = z = nextNumber();
        x %= 5;
        y = (y / 5) % 5;
        z = (z / 25) % 4;

        // suppress translations: x = y = z = 2;

        auto translation = mat4f(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            translationOffsetsX[x], translationOffsetsY[y], translationOffsetsZ[z], 1.0f
        );

        printf("Sliding %s by x=%d, y=%d, z=%d\n", entityName, x, y, z);
        /* ...but the actual translating gets done only later.  We need to rotate and scale first, then translate, or we'd rotate around the origin - often moving out of the viewable area.  */


        /* Rotate around every coordinate */

        float tilt[] = { -0.5, 0, 0, +0.5 };
        float turnAround[] = { -3, -1.6, -1, -0.6, 0, 0.6, 1, 1.6 };
        float barelyTilt[] = { -0.2, 0, 0, 0.2 };

        x = y = z = nextNumber();

        float *rotationOffsetsX;
        float *rotationOffsetsY;
        float *rotationOffsetsZ;

        if (upsideDownAllowed) {
            rotationOffsetsX = barelyTilt;               // tilt toward the viewer
            rotationOffsetsY = turnAround;
            rotationOffsetsZ = turnAround;               // tilt to the side as much as you please
            x %= 4;
            y = (y / 4) % 8;
            z = (z / 32) % 8;
        }
        else {
            rotationOffsetsX = barelyTilt;               // tilt toward the viewer
            rotationOffsetsY = turnAround;
            rotationOffsetsZ = tilt;                     // tilt to the side but don't cause nausea
            x %= 4;
            y = (y / 4) % 8;
            z = (z / 32) % 4;
        }


        auto rotX = mat4f::rotation(rotationOffsetsX[x], float3(1, 0, 0));
        auto rotY = mat4f::rotation(rotationOffsetsY[y], float3(0, 1, 0));
        auto rotZ = mat4f::rotation(rotationOffsetsZ[z], float3(0, 0, 1));
        printf("Rotating %s by %.1f rad around x, %.1f rad around y, %.1f rad around z\n", entityName, rotationOffsetsX[x], rotationOffsetsY[y], rotationOffsetsZ[z]);

        const bool ofCourse = true;
        const bool scalingAllowed = ofCourse;

        if (scalingAllowed) {
            const float scalingFactorsX[] = { 0.8, 1, 1, 1.3 };    // thin to fat
            const float scalingFactorsY[] = { 0.8, 1, 1, 1.3 };    // plump to tall
            const float scalingFactorsZ[] = { 0.9, 1, 1, 1.1 };    // I doubt that this is well visible
            x = y = z = nextNumber();
            x %= 4;
            y = (y / 4) % 4;
            z = (z / 16) % 4;

            auto scaleX = scalingFactorsX[x];
            auto scaleY = scalingFactorsY[y];
            auto scaleZ = scalingFactorsZ[z];

            auto scaling = mat4f(
                scaleX, 0.0f, 0.0f, 0.0f,
                0.0f, scaleY, 0.0f, 0.0f,
                0.0f, 0.0f, scaleZ, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );

            if (scaleX != 1 || scaleY != 1 || scaleZ != 1) {
                printf("Scaling %s by %.1f along x, by %.1f along y, and %.1f along z\n", entityName, scaleX, scaleY, scaleZ);
            }

            /*
            * Twisting doesn't seem to produce any useful effects.  It makes the scene look *less*
            * like a 3D scene, regardless of where I put it.  Had I paid more attention to linear
            * algebra at school, I'd have learned why is that.
            * 
            float k = 0.4;
            float l = 0.4;
            auto twist = mat4f(
                cos(k), -sin(k), 0.0f, l * sin(k),
                sin(k), cos(k), 0.0f, l * (1 - cos(k)),
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            );
            */

            auto instance = engine->getTransformManager().getInstance(entity);
            engine->getTransformManager().setTransform(instance, rotX * rotY * rotZ * scaling * engine->getTransformManager().getTransform(instance) * translation);

        }
    }
}

void swapMaterials(App& app, Engine* engine, const char* entityName, int m1, int m2, int m3, int m4) {
    auto entity = app.asset->getFirstEntityByName(entityName);
    auto renderableManager = engine->getRenderableManager().getInstance(entity);

    int swappableMaterials[] = {m1, m2, m3, m4};

    int iteration = 0;

    while (nextNumber() % 4 > iteration++) {  // At least one swapping gets done in almost 70% of cases.  Two ones in about 20%. Three ones - super rarely.
        int firstMaterial = swappableMaterials[nextNumber() % 4];
        int secondMaterial = swappableMaterials[nextNumber() % 4];
        if (firstMaterial == secondMaterial) {
            continue;
        }
        engine->getRenderableManager().setMaterialInstanceAt(renderableManager, firstMaterial, engine->getRenderableManager().getMaterialInstanceAt(renderableManager, secondMaterial));
        engine->getRenderableManager().setMaterialInstanceAt(renderableManager, secondMaterial, engine->getRenderableManager().getMaterialInstanceAt(renderableManager, firstMaterial));
        printf("Swapping materials %d and %d on %s\n", firstMaterial, secondMaterial, entityName);
    }
}



void adjust_scene(App &app, Engine* engine, View* view, double now) {
    //app.resourceLoader->asyncUpdateLoad();

// Optionally fit the model into a unit cube at the origin.
    app.viewer->updateRootTransform();

    /*size_t n = app.asset->getEntityCount();
    Entity *list = app.asset->getEntities();
    for (int i = 0; i < n; i++) {
        if (engine->getTransformManager().getInstance(list[i]))
    }*/

    swapMaterials(app, engine, "Man", 1, 2, 3, 5);

    transform(app, engine, "Fork", true);
    transform(app, engine, "Guitar", true);
    transform(app, engine, "Man", false);
    transform(app, engine, "Spaceship", false);
    transform(app, engine, "Sword", true);
    transform(app, engine, "Tree", false);
}

