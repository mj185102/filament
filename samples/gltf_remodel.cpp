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

    if (nextNumber() % 2 == 0) {  // kill 50% of objects
        auto away = mat4f(
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f
        );
        engine->getTransformManager().setTransform(engine->getTransformManager().getInstance(entity), away);
        printf("Hiding %s\n", entityName);
    } else {
        /* Shift along every coordinate */
        int x, y, z;

        const float translationOffsetsX[] = { -4, -2, 0, 2, 5 };    // from left to right
        const float translationOffsetsY[] = { -4, -2, 0, 2, 3.1 };  // upwards
        const float translationOffsetsZ[] = { -20, -4, 0, 2.2 };     // toward the viewer
        x = y = z = nextNumber();
        x %= 5;
        y = (y / 5) % 5;
        z = (z / 25) % 4;

        // suppress shifts: x = y = z = 2;

        auto shift = mat4f(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            translationOffsetsX[x], translationOffsetsY[y], translationOffsetsZ[z], 1.0f
        );

        printf("Shifting %s by x=%d, y=%d, z=%d\n", entityName, x, y, z);
        /* ...but the actual shifting gets done only later.  We need to rotate first, then shift, or we'd rotate around the origin - out of the viewable area.  */


        /* Rotate around every coordinate*/

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
        auto instance = engine->getTransformManager().getInstance(entity);
        engine->getTransformManager().setTransform(instance, rotX * rotY * rotZ * engine->getTransformManager().getTransform(instance) * shift);
        printf("Rotating %s by %.1f rad around x, %.1f rad around y, %.1f rad around z\n", entityName, rotationOffsetsX[x], rotationOffsetsY[y], rotationOffsetsZ[z]);
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

