#ifndef ECS_H_GUARD
#define ECS_H_GUARD

#include <cassert>
#include <set>
#include <typeindex>
#include <unordered_map>

#ifndef ECS_ASSERT
#define ECS_ASSERT(...) assert(__VA_ARGS__)
#endif

namespace ecs {

typedef uint32_t entity;

class Registry {
    struct ComponentStorage {
        struct EntityIndex {
            entity ent;
            size_t index;

            bool operator<(const EntityIndex &other) const {
                return ent < other.ent;
            }
        };

        size_t component_size;
        std::vector<uint8_t> component_bytes;
        std::set<EntityIndex> indices;

        void (*initializer)(uint8_t *);
        void (*deleter)(uint8_t *);

        void destroy_components() {
            for (uint8_t *ptr = component_bytes.data();
                 ptr != component_bytes.data() + component_bytes.size();
                 ptr += component_size) {
                deleter(ptr);
            }
        }

        uint8_t *emplace_component(entity ent) {
            size_t index = component_bytes.size();
            indices.insert(EntityIndex{ent, index});
            component_bytes.resize(component_bytes.size() + component_size);
            initializer(&component_bytes[index]);

            return &component_bytes[index];
        }

        uint8_t *find_component(entity ent) {
            auto it = indices.find(EntityIndex{ent});

            if (it == indices.end()) {
                return nullptr;
            }

            size_t index = it->index;
            return &component_bytes.data()[index];
        }

        bool has_component(entity ent) {
            return indices.find(EntityIndex{ent}) != indices.end();
        }

        bool remove_component(entity ent) {
            auto it = indices.find(EntityIndex{ent});

            if (it == indices.end()) {
                return false;
            }

            size_t index = it->index;
            indices.erase(it);

            deleter(&component_bytes[index]);
            component_bytes.erase(component_bytes.begin() + index,
                                  component_bytes.begin() + index +
                                      component_size);

            return true;
        }
    };

public:
    ~Registry() {
        for (auto &it : storage_mapping_) {
            it.second.destroy_components();
        }
    }

    template <class T> void register_component_type() {
        auto it = storage_mapping_.find(typeid(T));

        if (it != storage_mapping_.end()) {
            return;
        }

        auto &storage = storage_mapping_[typeid(T)];

        storage.component_size = sizeof(T);

        storage.initializer = [](uint8_t *bytes) { new (bytes)(T); };

        storage.deleter = [](uint8_t *bytes) {
            T *comp = (T *)bytes;
            comp->~T();
        };
    }

    template <class T> bool component_type_initialized() {
        return storage_mapping_.find(typeid(T)) != storage_mapping_.end();
    }

    entity create() { return counter_++; }

    void destroy(entity ent) {
        for (auto &it : storage_mapping_) {
            it.second.remove_component(ent);
        }
    }

    template <class T> T &emplace_component(entity ent) {
        ECS_ASSERT(component_type_initialized<T>());
        auto &storage = storage_mapping_[typeid(T)];
        return *(T *)storage.emplace_component(ent);
    }

    template <class T> T *find_component(entity ent) {
        ECS_ASSERT(component_type_initialized<T>());
        auto &storage = storage_mapping_[typeid(T)];
        return (T *)storage.find_component(ent);
    }

    template <class T> bool has_component(entity ent) {
        ECS_ASSERT(component_type_initialized<T>());
        auto &storage = storage_mapping_[typeid(T)];
        return storage.has_component(ent);
    }

private:
    std::unordered_map<std::type_index, ComponentStorage> storage_mapping_;
    entity counter_;
};

} // namespace ecs

#endif // ECS_H_GUARD
