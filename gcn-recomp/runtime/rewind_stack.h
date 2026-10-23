#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Owned, fixed-address execution stacks for the experimental replay scheduler.
// A snapshot is process-local and can only be restored to its original inactive
// stack. It is NOT a snapshot of heap objects referenced by stack frames. The
// caller must own/snapshot those separately and prohibit irreversible effects.
class RewindStack {
public:
    struct Snapshot {
        uint64_t identity=0;
        size_t offset=0;
        std::array<uint8_t,512> context{};
        std::vector<uint8_t> stack;
        unsigned mxcsr=0;
        unsigned short x87=0;
        bool finished=false;
    };
    explicit RewindStack(size_t bytes,std::function<void()> entry);
    ~RewindStack();
    RewindStack(const RewindStack&)=delete;
    RewindStack& operator=(const RewindStack&)=delete;
    void activate();
    static void suspend(); // return to the owning OS thread's coordinator stack
    static RewindStack* active();
    static bool supported();
    Snapshot save()const;
    void restore(const Snapshot&);
    bool finished()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    static void enter();
    static void transfer(RewindStack*);
};
