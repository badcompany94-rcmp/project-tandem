#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Windows write-watch allocations. Other platforms reject the opt-in path.
void* checkpoint_memory_reserve(size_t bytes);
bool checkpoint_memory_enabled();

// All operations run on one coordinator, with writers stopped and readers
// drained. Regions must be disjoint, page-aligned, committed watched memory.
// Historical images share immutable pages. Restores reset the current baseline.
class MemoryTimeline {
public:
    struct Region { uint8_t* data; size_t size; };
    struct Image;
    using Snapshot=std::shared_ptr<const Image>;
    explicit MemoryTimeline(std::vector<Region> regions);
    ~MemoryTimeline();
    MemoryTimeline(const MemoryTimeline&)=delete;
    MemoryTimeline& operator=(const MemoryTimeline&)=delete;
    Snapshot capture();
    void restore(const Snapshot& image);
    bool matches(const Snapshot& image) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
