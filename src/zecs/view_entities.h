#pragma once

#include "zecs/types.h"
#include <vector>

namespace zecs
{

using Entities = std::vector<Entity>;

struct ViewEntitiesIter
{
    using InternalIt = Entities::const_iterator;

    inline ViewEntitiesIter(InternalIt it_, InternalIt it_end_) : it(it_), it_end(it_end_) { next_matched(); }
    inline ViewEntitiesIter& operator++() { ++it; next_matched(); return *this; }
    inline bool operator==(const ViewEntitiesIter& other) const { return it == other.it; }
    inline bool operator!=(const ViewEntitiesIter& other) const { return it != other.it; }
    inline Entity operator*() const { return *it; }

private:
    inline void next_matched() noexcept
    {
        while (it != it_end && *it == Entity_INVALID) ++it;
    }

    InternalIt it;
    InternalIt it_end;
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
    inline It begin() { return It(entities.begin(), entities.end()); };
    inline It end() { return It(entities.end(), entities.end()); };

private:
    const Entities& entities;
};
} // namespace zecs
