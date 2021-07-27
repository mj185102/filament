#include <random>

std::mt19937_64 prg;
std::uniform_int_distribution<unsigned long long> dis;

void feed_to_scene_generator(char* entropy)
{
    printf("Feeding %s to the PRG\n", entropy);
    auto s = std::string(entropy);
	std::seed_seq seed(s.begin(), s.end());
	prg.seed(seed);
};

static unsigned long long nextNumber() {
    return dis(prg);
}

void transform(App &app, Engine* engine, const char* entityName) {
    auto entity = app.asset->getFirstEntityByName(entityName);

    if (nextNumber() & 1) {
        auto away = mat4f(
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f
        );
        engine->getTransformManager().setTransform(engine->getTransformManager().getInstance(entity), away);
        printf("Hiding %s\n", entityName);
    }

    /*

    // unused just yet
    auto m = mat4::rotation(1, float3(0, 0, 1));


    engine->getTransformManager().setTransform(engine->getTransformManager().getInstance(entity),

        mat4f(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        ));
        */
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

    transform(app, engine, "Fork");
    transform(app, engine, "Guitar");
    transform(app, engine, "Man");
    transform(app, engine, "Spaceship");
    transform(app, engine, "Sword");
    transform(app, engine, "Tree");
}

