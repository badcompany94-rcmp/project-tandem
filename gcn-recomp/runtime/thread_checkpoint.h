#pragma once
#include <cstddef>
#include <memory>

// Development-only scheduler checkpoint. This owns guest CPU continuations, not
// guest RAM, peripherals, graphics, audio, network state or external resources.
// Save/load must run on the coordinator through threads_on_coordinator().
class ThreadCheckpoint {
public:
    static std::shared_ptr<const ThreadCheckpoint> save();
    void restore()const;
    size_t stack_bytes()const;
    size_t thread_count()const;
    bool matches_guest_state()const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ThreadCheckpoint(std::shared_ptr<const Impl> p):impl_(std::move(p)){}
};
bool threads_have_owned_stacks();
void threads_on_coordinator(void(*callback)(void*),void* argument);
