#pragma once

#include "zecs/types.h"
#include <vector>

namespace zecs
{

using Entities = std::vector<Entity>;

// Goes by index, not by vector iterators: entities can be added during the pass.
// The end is saved when the pass starts, so new slots are not visited.
// A new entity that takes a removed slot ahead is visited.
struct ViewEntitiesIter
{
    inline ViewEntitiesIter(const Entities& entities_, size_t index_, size_t index_end_) : entities(&entities_), index(index_), index_end(index_end_) { next_matched(); }
    inline ViewEntitiesIter& operator++() { ++index; next_matched(); return *this; }
    inline bool operator==(const ViewEntitiesIter& other) const { return index == other.index; }
    inline bool operator!=(const ViewEntitiesIter& other) const { return index != other.index; }
    inline Entity operator*() const { return (*entities)[index]; }

private:
    inline void next_matched() noexcept
    {
        // A live slot keeps its own index, a removed slot keeps another one.
        while (index != index_end && entity_to_index((*entities)[index]) != index) ++index;
    }

    const Entities* entities;
    size_t index;
    size_t index_end;
};

struct ViewEntities
{
    using It = ViewEntitiesIter;

    ViewEntities(const ViewEntities&) = delete;
    ViewEntities(ViewEntities&&) = delete;
    ViewEntities& operator=(const ViewEntities&) = delete;
    ViewEntities& operator=(ViewEntities&&) = delete;

    inline ViewEntities(const Entities& entities_) : entities(entities_) { }
    inline bool empty() { return entities.empty(); }
    inline It begin() { return It(entities, 0, entities.size()); };
    inline It end() { return It(entities, entities.size(), entities.size()); };

private:
    const Entities& entities;
};
} // namespace zecs
