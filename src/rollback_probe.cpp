// Development diagnostic: rewind the real recompiled race call over a window
// of deliberately incorrect remote inputs. The optional device checkpoint owns
// audio, interrupts and timed events; graphics and full frames are still excluded.
#include "runtime.h"
#include "replay_audit.h"
#include "thread_checkpoint.h"
#include "device_checkpoint.h"
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>
extern "C" {
#include "recomp_funcs.h"
}

namespace {
struct Image {
    std::shared_ptr<const ThreadCheckpoint> threads;
    std::vector<uint8_t> ram,cache,aram;
    std::vector<DeviceFragment> devices;
    uint64_t count=0,limit=0;
    CPU cpu{};
};
struct Probe {
    CPU* cpu=nullptr;
    CPU caller{};
    Image entry,fork,expected;
    unsigned phase=0,index=0,window=8,players=0,local=0;
    std::array<unsigned,8> seats{};
    std::array<uint8_t,64> pipe{};
    uint32_t pipe_length=0;
    ReplayGuard guard;
    ReplayAudit audit;
    std::vector<ReplayOutput> journal,expected_journal,prefix;
    std::vector<uint8_t> pcm,expected_pcm,prefix_pcm;
    uint64_t began=0,tick=0,correction_began=0;
    unsigned completed=0;
} state;
Image save(bool continuations) {
    Image image;
    if(continuations || state.guard.runtime_devices)image.threads=ThreadCheckpoint::save();
    image.ram.assign(g_mem,g_mem+RAM_SIZE);
    image.cache.assign(g_mem+LC_BASE,g_mem+LC_BASE+LC_SIZE);
    if(state.guard.runtime_devices) {
        image.aram.assign(g_aram,g_aram+ARAM_SIZE);
        image.devices=checkpoint_race_devices();
    }
    image.count=g_vcount;image.limit=g_vlimit;image.cpu=*state.cpu;return image;
}
void restore(const Image& image) {
    memcpy(g_mem,image.ram.data(),RAM_SIZE);memcpy(g_mem+LC_BASE,image.cache.data(),LC_SIZE);
    if(!image.aram.empty())memcpy(g_aram,image.aram.data(),ARAM_SIZE);
    for(const auto& part:image.devices)part.restore();
    g_vcount=image.count;g_vlimit=image.limit;image.threads->restore();
}
bool same(const Image& image) {
    if(!image.aram.empty()) {
        if(memcmp(g_aram,image.aram.data(),ARAM_SIZE) || !image.threads->matches_guest_state())return false;
        for(const auto& part:image.devices)if(!part.matches())return false;
    }
    return !memcmp(g_mem,image.ram.data(),RAM_SIZE) && !memcmp(g_mem+LC_BASE,image.cache.data(),LC_SIZE) &&
           !memcmp(state.cpu,&image.cpu,sizeof(CPU)) && g_vcount==image.count;
}
void boundary(void*) {
    auto& s=state;
    if(s.phase==0 && s.index==0) {checkpoint_graphics_quiesce();s.entry=save(true);return;}
    if(s.phase==1 && s.index==2) {s.fork=save(true);s.prefix=s.journal;s.prefix_pcm=s.pcm;return;}
    if(s.index!=s.window)return;
    if(s.phase==0) {
        s.expected=save(false);s.expected_journal=s.journal;s.expected_pcm=s.pcm;
        s.phase=1;s.index=0;s.journal.clear();s.pcm.clear();restore(s.entry);
    }else if(s.phase==1) {
        if(same(s.expected))fatal("Rollback window did not exercise a misprediction");
        s.phase=2;s.index=2;s.journal=s.prefix;s.pcm=s.prefix_pcm;s.correction_began=host_ns();restore(s.fork);
    }else if(s.phase==2) {
        const double correction_ms=(host_ns()-s.correction_began)/1e6;
        if(!same(s.expected)) {
            for(const auto& part:s.expected.devices)if(!part.matches())fprintf(stderr,"[rollback-device] differs: %s\n",part.name.c_str());
            if(s.guard.runtime_devices && !s.expected.threads->matches_guest_state())fprintf(stderr,"[rollback-device] thread contexts differ\n");
            size_t first=0;while(first<RAM_SIZE && g_mem[first]==s.expected.ram[first])++first;
            fatal("Rollback window diverged at tick %llu (first RAM offset %zx, clocks %llu/%llu)",
                (unsigned long long)s.tick,first,(unsigned long long)g_vcount,(unsigned long long)s.expected.count);
        }
        if(s.journal.size()!=s.expected_journal.size())fatal("Rollback window output count differs");
        if(s.pcm!=s.expected_pcm)fatal("Rollback window audio samples differ");
        for(size_t i=0;i<s.journal.size();++i)
            if(s.journal[i].address!=s.expected_journal[i].address || s.journal[i].value!=s.expected_journal[i].value)
                fatal("Rollback window output journal differs");
        fprintf(stderr,"[rollback-window] PASS tick=%llu depth=%u threads=%zu stack_bytes=%zu elapsed_ms=%.2f correction_ms=%.2f; exact RAM/CPU/cache/clock/output correction, isolated race calls\n",
            (unsigned long long)s.tick,s.window-2,s.entry.threads->thread_count(),s.entry.threads->stack_bytes(),(host_ns()-s.began)/1e6,correction_ms);
        if(s.guard.runtime_devices)fprintf(stderr,"[rollback-devices] PASS tick=%llu context_loads=%llu interrupts=%llu events=%llu pcm_bytes=%zu; graphics remains excluded\n",
            (unsigned long long)s.tick,(unsigned long long)s.audit.context_loads,(unsigned long long)s.audit.interrupts,
            (unsigned long long)s.audit.events,s.expected_pcm.size());
        ++s.completed;s.phase=3;s.index=0;restore(s.entry);
    }
}
}

extern "C" void mkdd_rollback_window_probe(CPU* c,unsigned players,unsigned local,const unsigned* seats,uint64_t tick) {
    static const bool enabled=getenv("MKDD_REPLAY_WINDOW")!=nullptr;
    if(!enabled || tick<900 || tick%180 || state.completed>=8)return;
    if(!threads_have_owned_stacks() || !clock_is_virtual() || g_replay_guard)
        fatal("Window probe requires owned execution stacks and virtual time");
    auto& s=state;s.cpu=c;s.caller=*c;s.players=players;s.local=local;s.tick=tick;
    if(const char* depth=getenv("MKDD_REPLAY_DEPTH")) {
        char* end=nullptr;const unsigned long value=strtoul(depth,&end,10);
        if(!*depth || *end || value<1 || value>12)fatal("Replay depth must be 1 through 12");
        s.window=(unsigned)value+2;
    }
    for(unsigned i=0;i<players;++i)s.seats[i]=seats[i];
    s.phase=0;s.index=0;s.journal.clear();s.prefix.clear();s.expected_journal.clear();s.began=host_ns();
    s.guard.runtime_devices=getenv("MKDD_REPLAY_DEVICES")!=nullptr;
    s.guard.pads=pad_capture_states();s.audit={};s.pcm.clear();s.prefix_pcm.clear();s.expected_pcm.clear();
    s.pipe_length=g_gp_len;memcpy(s.pipe.data(),g_gp_buf,s.pipe.size());
    if(!s.guard.runtime_devices)fn_800EB1B8(c); // old isolated boundary remains available
    *c=s.caller;
    for(unsigned frame=0;frame<=s.window;++frame) {
        s.index=frame;threads_on_coordinator(boundary,nullptr);
        if(s.phase==3)break; // restoring entry resumes the original call stack here
        if(frame==s.window)fatal("Window coordinator failed to rewind");
        s.guard.outputs.clear();s.guard.pcm.clear();s.guard.maximum_edges=g_vcount+1000000;
        g_replay_guard=&s.guard;
        if(s.guard.runtime_devices)g_replay_audit=&s.audit;
        const uint32_t scratch=s.caller.r[1]-32;
        const uint16_t scratch_before=mem_r16(scratch);
        for(unsigned peer=0;peer<players;++peer) {
            // Includes steering changes, acceleration/braking, items and driver
            // swaps. Every peer gets the same local input in all three runs.
            uint8_t stick=(uint8_t)((frame*5+peer*3)&31);
            uint8_t buttons=(uint8_t)(1u | (frame==3?64u:0u) | (frame==5?12u:0u) | (frame==6?2u:0u));
            if(s.phase==1 && frame>=2 && peer!=local){stick=0;buttons=0;}
            mem_w8(scratch,stick);mem_w8(scratch+1,buttons);
            *c=s.caller;c->r[3]=mem_r32(0x80352808+4*s.seats[peer]);c->r[4]=scratch;
            fn_801D1410(c); // retail KartGamePad::expand
        }
        mem_w16(scratch,scratch_before);*c=s.caller;
        orig_fn_801CD92C(c);
        g_replay_guard=nullptr;
        g_replay_audit=nullptr;
        s.journal.insert(s.journal.end(),s.guard.outputs.begin(),s.guard.outputs.end());
        s.pcm.insert(s.pcm.end(),s.guard.pcm.begin(),s.guard.pcm.end());
        if(g_gp_len!=s.pipe_length || memcmp(g_gp_buf,s.pipe.data(),s.pipe.size()))fatal("Window probe changed unowned graphics pipe");
    }
    // Every speculative result/output is discarded. The ordinary race call runs
    // once afterward with the actual synchronized inputs and original CPU state.
    *c=s.caller;if(!s.guard.runtime_devices)fn_800EB1F8(c);*c=s.caller;clock_update_limit();
    s.entry={};s.fork={};s.expected={};
}
