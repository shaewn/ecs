#ifndef ECS_H_GUARD
#define ECS_H_GUARD

#include <cassert>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "detail.h"

#ifndef ECS_ASSERT
#define ECS_ASSERT(...) assert(__VA_ARGS__)
#endif

namespace ecs {

typedef uint32_t entity;

class Registry {
    template <class... Components> class ComponentView {
        class Iterator {
        public:
            Iterator(Registry &reg, entity *ents, size_t index,
                     size_t end_index)
                : reg_(reg), ents_(ents), index_(index), end_index_(end_index) {}

            bool operator==(const Iterator &other) {
                return ents_ == other.ents_ && index_ == other.index_;
            }

            bool operator!=(const Iterator &other) { return !(*this == other); }

            void operator++() {
                do {
                    index_++;
                } while (index_ != end_index_ &&
                         !has_all_components(ents_[index_]));
            }

            std::tuple<entity, Components &...> operator*() {
                return std::tuple<entity, Components &...>(
                    ents_[index_],
                    *reg_.find_component<Components>(ents_[index_])...);
            }

        private:
            Registry &reg_;
            entity *ents_ = nullptr;
            size_t index_ = 0;
            size_t end_index_ = 0;

            bool has_all_components(entity ent) {
                return (reg_.has_component<Components>(ents_[index_]) && ...);
            }
        };

    public:
        typedef Iterator iterator;

        ComponentView(Registry &reg) : reg_(reg) {}

        iterator begin() {
            auto &smallest_storage = find_smallest_storage();
            return iterator(reg_, smallest_storage.assoc_entities.data(), 0,
                            smallest_storage.size());
        }

        iterator end() {
            auto &smallest_storage = find_smallest_storage();
            return iterator(reg_, smallest_storage.assoc_entities.data(),
                            smallest_storage.size(), smallest_storage.size());
        }

    private:
        Registry &reg_;

        detail::ComponentStorage &find_smallest_storage() {
            return std::min(
                {std::ref(reg_.get_component_storage<Components>())...},
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

        auto &storage = storage_mapping_[detail::ComponentID<T>::get()];

        storage.component_size = sizeof(T);

        storage.initializer = [](uint8_t *bytes) { new (bytes)(T); };

        storage.deleter = [](uint8_t *bytes) {
            T *comp = (T *)bytes;
            comp->~T();
        };

        storage.component_bytes.reserve(init_cap * storage.component_size);
        storage.assoc_entities.reserve(init_cap);
        storage.indices.reserve(init_cap);
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
        for (detail::component_id comp : comps) {
            storage_mapping_[comp].remove_component(ent);
        }

        entities_components_.erase(it);

        // We may reuse this id, so clear everything.
        comps.clear();
        free_ids_.push_back(ent);
    }

    template <class T> T &emplace_component(entity ent) {
        T &result = *(T *)get_component_storage<T>().emplace_component(ent);
        entities_components_[ent].insert(detail::ComponentID<T>::get());
        return result;
    }

    template <class T> bool remove_component(entity ent) {
        bool result = get_component_storage<T>().remove_component(ent);
        entities_components_[ent].erase(detail::ComponentID<T>::get());
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
    std::unordered_map<detail::component_id, detail::ComponentStorage> storage_mapping_;
    std::unordered_map<entity, std::unordered_set<detail::component_id>>
        entities_components_;
    entity counter_ = 0;
    std::deque<entity> free_ids_;

    template <class T> bool component_initialized() {
        return storage_mapping_.find(detail::ComponentID<T>::get()) !=
               storage_mapping_.end();
    }

    template <class T> detail::ComponentStorage &get_component_storage() {
        auto it = storage_mapping_.find(detail::ComponentID<T>::get());
        if (it == storage_mapping_.end()) {
            register_component<T>();
            return storage_mapping_[detail::ComponentID<T>::get()];
        }

        return it->second;
    }
};


} // namespace ecs

#endif // ECS_H_GUARD
