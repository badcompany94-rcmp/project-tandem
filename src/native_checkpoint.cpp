#include "native_checkpoint.h"
#include "runtime.h"
#include <cstdlib>
DeviceFragment mkdd_checkpoint_online_input();
DeviceFragment mkdd_checkpoint_online_tags();
namespace mkdd {
static MemoryTimeline& memory_timeline() {
    static MemoryTimeline timeline({{g_mem,RAM_SIZE},{g_aram,ARAM_SIZE},{g_mem+LC_BASE,LC_SIZE}});
    return timeline;
}
NativeCheckpoint NativeCheckpoint::capture(bool graphics) {
    NativeCheckpoint image;image.refresh(graphics);return image;
}
void NativeCheckpoint::refresh(bool graphics) {
    has_graphics=graphics;
    const auto t0=host_ns();
    devices.clear();threads=ThreadCheckpoint::save();
    const auto t1=host_ns();
    if(checkpoint_memory_enabled())memory=memory_timeline().capture();
    if(!memory || getenv("MKDD_MEMORY_VERIFY")) {
        ram.assign(g_mem,g_mem+RAM_SIZE);aram.assign(g_aram,g_aram+ARAM_SIZE);
        cache.assign(g_mem+LC_BASE,g_mem+LC_BASE+LC_SIZE);
    }
    const auto t2=host_ns();
    devices=checkpoint_race_devices();
    devices.push_back(checkpoint_graphics_fifo());devices.push_back(checkpoint_graphics_skim());
    devices.push_back(checkpoint_exi());devices.push_back(mkdd_checkpoint_online_input());
    devices.push_back(mkdd_checkpoint_online_tags());
    const auto t3=host_ns();
    uint64_t t4=t3;
    if(graphics) {
        devices.push_back(checkpoint_graphics_commands());devices.push_back(checkpoint_graphics_frontend());
        devices.push_back(checkpoint_graphics_textures());t4=host_ns();
        devices.push_back(checkpoint_graphics_render(getenv("MKDD_PREDICTION_ASYNC_RENDER")!=nullptr));
    }
    const auto t5=host_ns();
    if(getenv("MKDD_CHECKPOINT_PROFILE")) {
        static double totals[5]{};static unsigned count=0;
        const uint64_t marks[]={t0,t1,t2,t3,t4,t5};
        for(unsigned i=0;i<5;++i)totals[i]+=(marks[i+1]-marks[i])/1e6;
        if(++count%120==0)fprintf(stderr,"[checkpoint-cost] n=%u threads_ms=%.3f memory_ms=%.3f devices_ms=%.3f frontend_ms=%.3f gpu_ms=%.3f\n",
            count,totals[0]/count,totals[1]/count,totals[2]/count,totals[3]/count,totals[4]/count);
    }
}
void NativeCheckpoint::restore() const {
    // Render jobs own decoded inputs, including display height, and capture
    // rejects callbacks into live game memory. An opt-in ordered restore may
    // therefore overlap CPU restore; subsequent GPU work follows it in order.
    if(has_graphics) {
        const auto& graphics=devices.back();
        if(getenv("MKDD_PREDICTION_ASYNC_RESTORE") && graphics.restore_enqueued)graphics.restore_enqueued();
        else graphics.restore();
    }
    if(memory) {
        memory_timeline().restore(memory);
        if(!ram.empty() && (memcmp(g_mem,ram.data(),RAM_SIZE) || memcmp(g_aram,aram.data(),ARAM_SIZE) || memcmp(g_mem+LC_BASE,cache.data(),LC_SIZE)))
            fatal("Incremental memory restore differs from independent full-copy oracle");
    } else {
        memcpy(g_mem,ram.data(),RAM_SIZE);memcpy(g_aram,aram.data(),ARAM_SIZE);
        memcpy(g_mem+LC_BASE,cache.data(),LC_SIZE);
    }
    for(size_t i=0;i<devices.size()-(has_graphics?1:0);++i)devices[i].restore();
    threads->restore();
}
bool NativeCheckpoint::matches(bool report) const {
    bool equal=true;
    for(const auto& device:devices)if(!device.matches()) {
        equal=false;if(report)fprintf(stderr,"[rollback-frame] differs: %s\n",device.name.c_str());
    }
    if(!threads->matches_guest_state()) {
        equal=false;if(report)fprintf(stderr,"[rollback-frame] thread state differs\n");
    }
    if(memory && !memory_timeline().matches(memory)) {
        equal=false;if(report)fprintf(stderr,"[rollback-frame] incremental memory differs\n");
    }
    if(!ram.empty() && memcmp(g_mem,ram.data(),RAM_SIZE)) {
        equal=false;if(report){size_t first=0;while(g_mem[first]==ram[first])++first;
            fprintf(stderr,"[rollback-frame] RAM differs first at %zx\n",first);}
    }
    if(!aram.empty() && (memcmp(g_aram,aram.data(),ARAM_SIZE) || memcmp(g_mem+LC_BASE,cache.data(),LC_SIZE)))equal=false;
    return equal;
}
}
