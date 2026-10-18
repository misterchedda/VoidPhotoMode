#pragma once
#include <atomic>
// Merge message events with asynchronous polling without losing short taps or
// emitting twice when polling observes a press before its window message arrives.
struct DiscreteInput {
    std::atomic<bool> seen{false}, pending{false};
    bool previous = false;
    void message(bool down) {
        if (down) {
            if (!seen.exchange(true))
                pending = true;
        } else
            seen = false;
    }
    bool poll(bool down) {
        bool event = pending.exchange(false), rising = down && !previous;
        previous = down;
        if (rising && !event)
            seen = true;
        return rising || event;
    }
};
