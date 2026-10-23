#include "memory_checkpoint.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

bool checkpoint_memory_enabled() { return std::getenv("GCN_WRITE_WATCH")!=nullptr; }
void* checkpoint_memory_reserve(size_t bytes) {
#ifdef _WIN32
    return VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_WRITE_WATCH,PAGE_READWRITE);
#else
    throw std::logic_error("Write-watch checkpoints require Windows");
#endif
}
struct MemoryTimeline::Image {
    using Page=std::vector<uint8_t>;
    std::shared_ptr<const int> owner;
    std::vector<std::shared_ptr<const Page>> pages;
};
struct MemoryTimeline::Impl {
    std::vector<Region> regions;
    std::vector<uint8_t*> pages;
    size_t page_size=0;
    std::shared_ptr<const int> owner=std::make_shared<const int>(0);
    std::thread::id thread=std::this_thread::get_id();
    Snapshot baseline;
    std::vector<void*> addresses;
    void check_thread() const {
        if(thread!=std::this_thread::get_id())throw std::logic_error("Memory checkpoint coordinator changed");
    }
    void check(const Snapshot& image) const {
        check_thread();
        if(!image || image->owner!=owner || image->pages.size()!=pages.size())
            throw std::logic_error("Memory checkpoint belongs to a different timeline");
    }
    std::vector<bool> dirty() {
        std::vector<bool> result(pages.size(),false);
#ifdef _WIN32
        size_t offset=0;
        for(const auto& region:regions) {
            ULONG_PTR count=addresses.size();DWORD granularity=0;
            if(GetWriteWatch(WRITE_WATCH_FLAG_RESET,region.data,region.size,addresses.data(),&count,&granularity) || granularity!=page_size)
                throw std::runtime_error("GetWriteWatch failed or changed page granularity");
            for(size_t i=0;i<count;++i) {
                auto address=reinterpret_cast<uintptr_t>(addresses[i]);
                const auto begin=reinterpret_cast<uintptr_t>(region.data);
                if(address<begin || address>=begin+region.size || (address-begin)%page_size)
                    throw std::runtime_error("GetWriteWatch returned an invalid page");
                result[offset+(address-begin)/page_size]=true;
            }
            offset+=region.size/page_size;
        }
#else
        throw std::logic_error("Write-watch checkpoints require Windows");
#endif
        return result;
    }
    void reset() {
#ifdef _WIN32
        for(const auto& region:regions)if(ResetWriteWatch(region.data,region.size))
            throw std::runtime_error("ResetWriteWatch failed");
#endif
    }
};
MemoryTimeline::MemoryTimeline(std::vector<Region> regions):impl(std::make_unique<Impl>()) {
#ifdef _WIN32
    SYSTEM_INFO info;GetSystemInfo(&info);impl->page_size=info.dwPageSize;
#else
    throw std::logic_error("Write-watch checkpoints require Windows");
#endif
    if(regions.empty())throw std::invalid_argument("Empty memory checkpoint regions");
    const size_t size=impl->page_size;
    for(size_t i=0;i<regions.size();++i) {
        const auto& region=regions[i];const auto begin=reinterpret_cast<uintptr_t>(region.data);
        if(!begin || !region.size || begin%size || region.size%size || begin+region.size<begin)
            throw std::invalid_argument("Memory checkpoint region is not page-aligned");
        for(size_t j=0;j<i;++j) {
            const auto other=reinterpret_cast<uintptr_t>(regions[j].data);
            if(begin<other+regions[j].size && other<begin+region.size)
                throw std::invalid_argument("Overlapping memory checkpoint regions");
        }
        for(size_t offset=0;offset<region.size;offset+=size)impl->pages.push_back(region.data+offset);
    }
    impl->regions=std::move(regions);impl->addresses.resize(impl->pages.size());
}
MemoryTimeline::~MemoryTimeline()=default;
MemoryTimeline::Snapshot MemoryTimeline::capture() {
    auto& s=*impl;s.check_thread();const auto dirty=s.dirty();
    auto image=std::make_shared<Image>();image->owner=s.owner;
    if(s.baseline)image->pages=s.baseline->pages;else image->pages.resize(s.pages.size());
    for(size_t i=0;i<s.pages.size();++i)if(!image->pages[i] || (dirty[i] && std::memcmp(s.pages[i],image->pages[i]->data(),s.page_size)))
        image->pages[i]=std::make_shared<const Image::Page>(s.pages[i],s.pages[i]+s.page_size);
    s.baseline=image;return image;
}
void MemoryTimeline::restore(const Snapshot& image) {
    auto& s=*impl;s.check(image);const auto dirty=s.dirty();
    for(size_t i=0;i<s.pages.size();++i)
        if(!s.baseline || dirty[i] || s.baseline->pages[i]!=image->pages[i])
            if(std::memcmp(s.pages[i],image->pages[i]->data(),s.page_size))
                std::memcpy(s.pages[i],image->pages[i]->data(),s.page_size);
    s.reset();s.baseline=image;
}
bool MemoryTimeline::matches(const Snapshot& image) const {
    const auto& s=*impl;s.check(image);
    for(size_t i=0;i<s.pages.size();++i)if(std::memcmp(s.pages[i],image->pages[i]->data(),s.page_size))return false;
    return true;
}
