#include "memory_checkpoint.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <stdexcept>
#include <thread>

static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    SYSTEM_INFO info;GetSystemInfo(&info);const size_t page=info.dwPageSize;
    // Include separated committed subregions in a larger watched reservation.
    auto* reservation=static_cast<uint8_t*>(checkpoint_memory_reserve(1024*page));
    require(reservation,"reserve");
    const size_t length=31*page;
    uint8_t* regions[]={reservation,reservation+512*page};
    for(auto* region:regions)require(VirtualAlloc(region,length,MEM_COMMIT,PAGE_READWRITE)==region,"commit");
    unsigned restored=0;
    {
        MemoryTimeline timeline({{regions[0],length},{regions[1],length}});
        struct Record {MemoryTimeline::Snapshot image;std::vector<uint8_t> full;};
        std::deque<Record> history;
        std::mt19937 random(0x8965);
        auto compare=[&](const Record& record) {
            require(timeline.matches(record.image),"page image differs");
            for(size_t r=0;r<2;++r)require(!std::memcmp(regions[r],record.full.data()+r*length,length),"independent whole-memory comparison differs");
        };
        for(unsigned iteration=0;iteration<3000;++iteration) {
            // Single-byte, cross-page and bulk writes, including zeroing and
            // repeated identical writes, exercise all OS tracking paths.
            for(unsigned w=0;w<13;++w) {
                auto* region=regions[random()%2];const size_t offset=random()%(length-3*page);
                const size_t bytes=w%3==0?1:random()%(3*page)+1;
                std::memset(region+offset,w%4==0?0:random()%256,bytes);
            }
            if(!history.empty() && iteration%3==0) {
                const auto& record=history[random()%history.size()];
                timeline.restore(record.image);compare(record);++restored;
                timeline.restore(record.image);compare(record);++restored;
                // Modification after restore must become dirty again.
                regions[1][page-1]^=0x5a;regions[1][page]^=0xa5;
            }
            Record record;record.image=timeline.capture();
            for(auto* region:regions)record.full.insert(record.full.end(),region,region+length);
            compare(record);history.push_back(std::move(record));
            if(history.size()>19)history.pop_front();
            if(iteration%11==0) {
                const auto& prior=history[random()%history.size()];
                timeline.restore(prior.image);compare(prior);++restored;
            }
        }
        bool rejected=false;
        std::thread other([&]{try{timeline.capture();}catch(const std::logic_error&){rejected=true;}});other.join();
        require(rejected,"cross-thread access accepted");
        rejected=false;try{timeline.restore({});}catch(const std::logic_error&){rejected=true;}
        require(rejected,"empty image accepted");
        MemoryTimeline unrelated({{regions[0],length}});
        rejected=false;try{unrelated.restore(history.front().image);}catch(const std::logic_error&){rejected=true;}
        require(rejected,"foreign image accepted");
        rejected=false;try{MemoryTimeline invalid({{regions[0],length},{regions[0]+page,length-page}});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"overlap accepted");
    }
    VirtualFree(reservation,0,MEM_RELEASE);
    printf("PASS: 3000 incremental images, %u historical restores, full independent byte comparisons, subregions and invalid ownership\n",restored);
}
