#include <iostream>
#include "ecs.h"

struct TransformComponent {
    int val = 10;

    TransformComponent() {
        std::cout << "Initializer!\n";
    }

    ~TransformComponent() {
        std::cout << "Deleter!\n";
    }
};

int main(int argc, char** argv){
    using namespace ecs;

    Registry reg;
    reg.register_component_type<TransformComponent>();

    entity ent = reg.create();

    auto &tc = reg.emplace_component<TransformComponent>(ent);
    std::cout << (&tc == reg.find_component<TransformComponent>(ent)) << '\n';
}
