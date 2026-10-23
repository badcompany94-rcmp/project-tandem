#pragma once
#include "thread_checkpoint.h"
#include "device_checkpoint.h"
#include "memory_checkpoint.h"
#include <memory>
#include <vector>
namespace mkdd {
// Only capture/restore on the owned execution coordinator after the complete
// frontend has drained. GPU snapshots follow queued batches in order.
// Includes game-owned input globals.
struct NativeCheckpoint {
    std::shared_ptr<const ThreadCheckpoint> threads;
    std::vector<uint8_t> ram,aram,cache;
    MemoryTimeline::Snapshot memory;
    std::vector<DeviceFragment> devices;
    bool has_graphics=false;
    static NativeCheckpoint capture(bool graphics);
    void refresh(bool graphics); // reuse large memory allocations in a bounded ring
    void restore() const;
    bool matches(bool report=false) const;
};
}
