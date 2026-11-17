#pragma once

#include "zecs/guid.h"
#include "zecs/types.h"

#include <algorithm>
#include <cassert>
#include <deque>

namespace zecs
{
// DRAFT: events are not finished and not reviewed yet. See known_issues.md.
struct EventDequeBase
{
    virtual ~EventDequeBase() = default;

    virtual GUid guid() const = 0;
    virtual const std::type_info* type() const = 0;

    virtual size_t size() const = 0;
    virtual GTick raw_event_get_tick(size_t index) const = 0;
    virtual const void* raw_event_get(size_t index) const = 0;
    virtual void raw_gc(GTick until) = 0;
};

template<typename T>
struct EventDeque : public EventDequeBase
{
    struct EventWrapper : public T
    {
        GTick tick;
    };

    using Deque = std::deque<EventWrapper>;

    inline GUid guid() const override { return globalUid<T>; }
    inline const std::type_info* type() const override { return &typeid(T); }
    inline size_t size() const override { return events.size(); }
    inline GTick raw_event_get_tick(size_t index) const override { return events[index].tick; }
    inline const void* raw_event_get(size_t index) const override { return &events[index]; }
    inline void raw_gc(GTick until) override{ events.erase(events.begin(), lower_bound(until - 1)); }
    inline void push(T&& event, GTick tick) { events.emplace_back(std::move(event), tick); }

    inline Deque::iterator lower_bound(GTick tick)
    {
        return std::lower_bound(events.begin(), events.end(), tick, LessOp{});
    }

    inline Deque::iterator upper_bound(GTick tick)
    {
        return std::upper_bound(events.begin(), events.end(), tick, LessOp{});
    }

    Deque events;

private:
    struct LessOp
    {
        constexpr bool operator()(const EventWrapper& h, const GTick& tick) { return h.tick < tick; };
        constexpr bool operator()(const GTick& tick, const EventWrapper& h) { return tick < h.tick; };
    };
};
}
