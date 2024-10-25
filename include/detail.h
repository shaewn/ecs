#ifndef ECS_DETAIL_H_GUARD
#define ECS_DETAIL_H_GUARD

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ecs {

typedef uint32_t entity;

namespace detail {

typedef size_t component_id;

struct Global {
    inline static component_id next_id = 0;
};

template <class T> class ComponentID {
public:
    static component_id get() {
        static component_id id_ = Global::next_id++;
        return id_;
    }
};

struct ComponentStorage {
    size_t component_size;
    std::vector<uint8_t> component_bytes;
    std::vector<entity> assoc_entities;
    std::unordered_map<entity, size_t> indices;

    void (*initializer)(uint8_t *);
    void (*deleter)(uint8_t *);

    // post: This ComponentStorage is invalidated and performing any further
    // operations on it is undefined behavior.
    void destroy_components() {
        for (uint8_t *ptr = component_bytes.data();
             ptr != component_bytes.data() + component_bytes.size();
             ptr += component_size) {
            deleter(ptr);
        }
    }

    // pre: !has_component(ent)
    uint8_t *emplace_component(entity ent) {
        size_t index = component_bytes.size() / component_size;
        indices[ent] = index;
        component_bytes.resize(component_bytes.size() + component_size);
        initializer(&component_bytes[index * component_size]);
        assoc_entities.push_back(ent);

        return &component_bytes[index * component_size];
    }

    uint8_t *find_component(entity ent) {
        auto it = indices.find(ent);

        if (it == indices.end()) {
            return nullptr;
        }

        size_t index = it->second;
        return &component_bytes.data()[index * component_size];
    }

    bool remove_component(entity ent) {
        auto it = indices.find(ent);

        if (it == indices.end()) {
            return false;
        }

        size_t index = it->second;
        indices.erase(it);

        // Only swap if we're not removing the last component
        if (index != component_bytes.size() / component_size - 1) {
            std::swap_ranges(component_bytes.begin() + index * component_size,
                             component_bytes.begin() + index * component_size +
                                 component_size,
                             component_bytes.end() - component_size);
            assoc_entities[index] = assoc_entities.back();
            entity swapped_entity = assoc_entities[index];
            indices[swapped_entity] = index;
        }

        deleter(&component_bytes[component_bytes.size() - component_size]);
        component_bytes.resize(component_bytes.size() - component_size);
        assoc_entities.pop_back();

        return true;
    }

    bool has_component(entity ent) const {
        return indices.find(ent) != indices.end();
    }

    size_t size() const { return indices.size(); }
};

} // namespace detail
} // namespace ecs

#endif // ECS_DETAIL_H_GUARD
