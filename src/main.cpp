#include <iostream>
#include "ecs.h"

struct TransformComponent {
    int val = 10;

    TransformComponent() {
        // Working.
        //std::cout << "Initializer!\n";
    }

    ~TransformComponent() {
        // Working.
        //std::cout << "Deleter!\n";
    }
};

struct FooComponent {
    float data = M_PI;
};

int main(int argc, char** argv){
    using namespace ecs;

    Registry reg;
    reg.register_component<TransformComponent>();
    // Make sure that the registry works when using more than one component.
    reg.register_component<FooComponent>();

    entity ents[100];

    for (int i = 0; i < 100; i++) {
        ents[i] = reg.create();
        auto &tc = reg.emplace_component<TransformComponent>(ents[i]);
        tc.val = i;
    }

    auto view = reg.view<TransformComponent>();

    for (auto [ent, tc] : view) {
        std::cout << "ent: " << ent << ", tc.val: " << tc.val << '\n';
        tc.val *= 2;
    }

    std::cout << "\n\n";

    for (auto [ent, tc] : view) {
        std::cout << "ent: " << ent << ", tc.val: " << tc.val << '\n';
    }

    // Test re-use deleted id's.
    std::cout << "\n\nDeleting entity " << ents[5] << '\n';
    reg.destroy(ents[5]);

    entity new_ent = reg.create();

    std::cout << "New entity created with id: " << new_ent << '\n';

    entity new_ent2 = reg.create();
    std::cout << "New entity created with id: " << new_ent2 << '\n';
}
