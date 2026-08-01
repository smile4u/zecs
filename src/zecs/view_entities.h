#pragma once

#include "zecs/types.h"
#include <vector>

namespace zecs
{

using Entities = std::vector<Entity>;

struct ViewEntitiesIter
{
    inline ViewEntitiesIter(const Entity* it_, const Entity* it_end_) : it(it_), it_end(it_end_) { next_matched(); }
    inline ViewEntitiesIter& operator++() { ++it; next_matched(); return *this; }
    inline bool operator==(const ViewEntitiesIter& other) const { return it == other.it; }
    inline bool operator!=(const ViewEntitiesIter& other) const { return it != other.it; }
    inline Entity operator*() const { return *it; }

private:
    inline void next_matched() noexcept
    {
        while (it != it_end && *it == Entity_INVALID) ++it;
    }

    const Entity* it;
    const Entity* it_end;
};

struct ViewEntities
{
    using It = ViewEntitiesIter;

    inline ViewEntities(const Entities& entities_) : entities(entities_) { }
    inline bool empty() { return entities.empty(); }
    inline It begin() { const Entity* d = entities.data(); return It(d, d + entities.size()); };
    inline It end() { const Entity* d = entities.data(); return It(d + entities.size(), d + entities.size()); };

private:
    const Entities& entities;
};
} // namespace zecs
