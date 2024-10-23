#ifndef ECS_H_GUARD
#define ECS_H_GUARD

#include <cassert>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#ifndef ECS_ASSERT
#define ECS_ASSERT(...) assert(__VA_ARGS__)
#endif

namespace ecs {

typedef uint32_t entity;

class Registry {
    typedef size_t component_id;

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
                std::swap_ranges(component_bytes.begin() +
                                     index * component_size,
                                 component_bytes.begin() +
                                     index * component_size + component_size,
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

    inline static component_id next_id = 0;

    template <class T> class ComponentID {
    public:
        static component_id get() {
            static component_id id_ = next_id++;
            return id_;
        }
    };

    template <class... Components> class ComponentView {
        class Iterator {
        public:
            Iterator(Registry &reg, entity *ents, size_t index,
                     size_t end_index)
                : reg(reg), ents(ents), index(index), end_index(end_index) {}

            bool operator==(const Iterator &other) {
                return ents == other.ents && index == other.index;
            }

            bool operator!=(const Iterator &other) { return !(*this == other); }

            void operator++() {
                do {
                    index++;
                } while (index != end_index &&
                         !has_all_components(ents[index]));
            }

            std::tuple<entity, Components &...> operator*() {
                return std::tuple<entity, Components &...>(
                    ents[index],
                    *reg.find_component<Components>(ents[index])...);
            }

        private:
            Registry &reg;
            entity *ents = nullptr;
            size_t index = 0;
            size_t end_index = 0;

            bool has_all_components(entity ent) {
                return (reg.has_component<Components>(ents[index]) && ...);
            }
        };

    public:
        typedef Iterator iterator;

        ComponentView(Registry &reg) : reg(reg) {}

        iterator begin() {
            auto &smallest_storage = find_smallest_storage();
            return iterator(reg, smallest_storage.assoc_entities.data(), 0,
                            smallest_storage.size());
        }

        iterator end() {
            auto &smallest_storage = find_smallest_storage();
            return iterator(reg, smallest_storage.assoc_entities.data(),
                            smallest_storage.size(), smallest_storage.size());
        }

    private:
        Registry &reg;

        ComponentStorage &find_smallest_storage() {
            return std::min(
                {std::ref(reg.get_component_storage<Components>())...},
                [](auto &a, auto &b) {
                    return a.get().size() < b.get().size();
                });
        }
    };

public:
    ~Registry() {
        for (auto &[comp, storage] : storage_mapping_) {
            storage.destroy_components();
        }
    }

    // Use this if you wish to set a specific initial capacity for
    // the ComponentStorage
    template <class T> void register_component(size_t init_cap = 32) {
        ECS_ASSERT(!component_initialized<T>() &&
                   "Component already initialized");

        auto &storage = storage_mapping_[ComponentID<T>::get()];

        storage.component_size = sizeof(T);

        storage.initializer = [](uint8_t *bytes) { new (bytes)(T); };

        storage.deleter = [](uint8_t *bytes) {
            T *comp = (T *)bytes;
            comp->~T();
        };

        storage.component_bytes.reserve(init_cap * storage.component_size);
    }

    entity create() {
        entity new_ent;

        if (!free_ids_.empty()) {
            new_ent = free_ids_.front();
            free_ids_.pop_front();
        } else {
            new_ent = counter_++;
        }

        entities_components_.emplace();
        return new_ent;
    }

    void destroy(entity ent) {
        auto it = entities_components_.find(ent);

        if (it == entities_components_.end()) {
            return;
        }

        auto &comps = it->second;
        for (component_id comp : comps) {
            storage_mapping_[comp].remove_component(ent);
        }

        entities_components_.erase(it);

        // We may reuse this id, so clear everything.
        comps.clear();
        free_ids_.push_back(ent);
    }

    template <class T> T &emplace_component(entity ent) {
        T &result = *(T *)get_component_storage<T>().emplace_component(ent);
        entities_components_[ent].insert(ComponentID<T>::get());
        return result;
    }

    template <class T> bool remove_component(entity ent) {
        bool result = get_component_storage<T>().remove_component(ent);
        entities_components_[ent].erase(ComponentID<T>::get());
        return result;
    }

    template <class T> T *find_component(entity ent) {
        return (T *)get_component_storage<T>().find_component(ent);
    }

    template <class T> bool has_component(entity ent) {
        return get_component_storage<T>().has_component(ent);
    }

    template <class... Components> ComponentView<Components...> view() {
        return ComponentView<Components...>(*this);
    }

private:
    std::unordered_map<component_id, ComponentStorage> storage_mapping_;
    std::unordered_map<entity, std::unordered_set<component_id>>
        entities_components_;
    entity counter_ = 0;
    std::deque<entity> free_ids_;

    template <class T> bool component_initialized() {
        return storage_mapping_.find(ComponentID<T>::get()) !=
               storage_mapping_.end();
    }

    template <class T> ComponentStorage &get_component_storage() {
        auto it = storage_mapping_.find(ComponentID<T>::get());
        if (it == storage_mapping_.end()) {
            register_component<T>();
            return storage_mapping_[ComponentID<T>::get()];
        }

        return it->second;
    }
};

} // namespace ecs

#endif // ECS_H_GUARD
