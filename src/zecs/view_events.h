#pragma once

#include "zecs/events.h"
#include "zecs/types.h"

namespace zecs
{
// DRAFT: events aren't finished and not reviewed yet. See roadmap.md.
template<typename T>
struct ViewEventsIter
{
    using InternalIt = EventDeque<T>::Deque::iterator;

    inline ViewEventsIter() = default;
    inline ViewEventsIter(InternalIt it) : event_it(it) { }
    inline ViewEventsIter& operator++() { ++event_it; return *this; };
    inline bool operator==(const ViewEventsIter& other) const { return event_it == other.event_it; }
    inline bool operator!=(const ViewEventsIter& other) const { return event_it != other.event_it; }
    inline ViewEventsIter& operator=(const ViewEventsIter& other) { event_it = other.event_it; return *this; }
    inline const T& operator*() const { return *static_cast<const T*>(&*event_it); }

private:
    InternalIt event_it;
};

template<typename T>
struct ViewEvents
{
    friend ViewEventsIter<T>;
    using It = ViewEventsIter<T>;

    inline ViewEvents(EventDeque<T>* deque, GTick since)
    {
        if (deque)
        {
            it_begin = deque->lower_bound(since);
            it_end = deque->events.end();
        }
        else
        {
            it_begin = typename EventDeque<T>::Deque::iterator();
            it_end = it_begin;
        }
    }

    ViewEvents(const ViewEvents&) = delete;
    ViewEvents(ViewEvents&&) = delete;
    ViewEvents& operator=(const ViewEvents&) = delete;
    ViewEvents& operator=(ViewEvents&&) = delete;

    inline bool empty() { return it_begin == it_end; }
    inline It begin() { return it_begin; }
    inline It end() { return it_end; }

protected:
    EventDeque<T>::Deque::iterator it_begin;
    EventDeque<T>::Deque::iterator it_end;
};
} //namespace zecs
