// Development-only full guest-frame rewind experiment. The real guest main loop
// runs between checkpoints, including draw construction and peripheral events.
// Speculative audio is journaled. Graphics can be journaled only, or rendered
// with GPU checkpoints; neither diagnostic publishes speculative frames.
#include "runtime.h"
#include "replay_audit.h"
#include "thread_checkpoint.h"
#include "device_checkpoint.h"
#include "native_checkpoint.h"
#include "gx/render.h"
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>
extern "C" {
#include "recomp_funcs.h"
}
DeviceFragment mkdd_checkpoint_online_input();
namespace {
const bool rendered=getenv("MKDD_REPLAY_RENDER")!=nullptr;
using Image=mkdd::NativeCheckpoint;
struct Experiment {
    bool active=false;
    unsigned phase=0,index=0,window=8,players=0,local=0,completed=0;
    std::array<unsigned,8> seats{};
    uint64_t tick=0,began=0,correction_began=0;
    Image entry,fork,expected;
    ReplayGuard guard;
    ReplayAudit audit;
    std::vector<uint8_t> expected_pcm,expected_graphics,prefix_pcm,prefix_graphics;
    std::vector<ReplayOutput> expected_outputs,prefix_outputs;
} s;
Image save() {return Image::capture(rendered);}
void restore(const Image& image) {image.restore();}
bool same(const Image& image,bool report=false) {return image.matches(report);}
void clear_outputs(){s.guard.outputs.clear();s.guard.pcm.clear();s.guard.graphics.clear();}
void boundary(void*) {
    if(rendered) {
        if(s.phase!=0 || s.index!=0)g_replay_guard=&s.guard;
        checkpoint_graphics_quiesce();g_replay_guard=nullptr;
        gx::pipeline_control([]{});
    }
    if(s.phase==0 && s.index==0){checkpoint_graphics_quiesce();s.entry=save();return;}
    if(s.phase==0 && s.index==2) {s.fork=save();return;}
    if(s.phase==1 && s.index==2) {
        if(!same(s.fork,true))fatal("Full-frame reference and incorrect-input run differ before the first misprediction");
        s.prefix_pcm=s.guard.pcm;s.prefix_graphics=s.guard.graphics;s.prefix_outputs=s.guard.outputs;return;
    }
    if(s.index!=s.window)return;
    if(s.phase==0) {
        s.expected=save();s.expected_pcm=s.guard.pcm;s.expected_graphics=s.guard.graphics;s.expected_outputs=s.guard.outputs;
        s.phase=1;s.index=0;clear_outputs();restore(s.entry);
    }else if(s.phase==1) {
        if(same(s.expected))fatal("Full-frame experiment did not exercise a misprediction");
        s.phase=2;s.index=2;s.guard.pcm=s.prefix_pcm;s.guard.graphics=s.prefix_graphics;s.guard.outputs=s.prefix_outputs;
        s.correction_began=host_ns();restore(s.fork);
    }else if(s.phase==2) {
        const double correction=(host_ns()-s.correction_began)/1e6;
        if(!same(s.expected,true))fatal("Full-frame correction diverged at tick %llu",(unsigned long long)s.tick);
        if(s.guard.pcm!=s.expected_pcm || s.guard.graphics!=s.expected_graphics || s.guard.outputs.size()!=s.expected_outputs.size()) {
            fprintf(stderr,"[rollback-frame] output comparison pcm=%d (%zu/%zu) graphics=%d (%zu/%zu) outputs=%zu/%zu\n",
                s.guard.pcm==s.expected_pcm,s.guard.pcm.size(),s.expected_pcm.size(),s.guard.graphics==s.expected_graphics,
                s.guard.graphics.size(),s.expected_graphics.size(),s.guard.outputs.size(),s.expected_outputs.size());
            fatal("Full-frame output journals differ");
        }
        for(size_t i=0;i<s.guard.outputs.size();++i)
            if(s.guard.outputs[i].address!=s.expected_outputs[i].address || s.guard.outputs[i].value!=s.expected_outputs[i].value)
                fatal("Full-frame controller output differs");
        if(!s.audit.context_loads || !s.audit.events || !s.audit.gpu_flushes || s.guard.graphics.empty() || s.guard.pcm.empty())
            fatal("Full-frame correction omitted required system activity");
        fprintf(stderr,"[rollback-frame] PASS tick=%llu depth=%u threads=%zu context_loads=%llu events=%llu gpu_flushes=%llu pcm_bytes=%zu graphics_bytes=%zu correction_ms=%.2f elapsed_ms=%.2f; exact guest state and output journals, no live prediction\n",
            (unsigned long long)s.tick,s.window-2,s.entry.threads->thread_count(),(unsigned long long)s.audit.context_loads,
            (unsigned long long)s.audit.events,(unsigned long long)s.audit.gpu_flushes,s.guard.pcm.size(),s.guard.graphics.size(),
            correction,(host_ns()-s.began)/1e6);
        if(rendered)fprintf(stderr,"[rollback-render] PASS tick=%llu depth=%u; exact frontend and GPU texture/framebuffer pixels\n",(unsigned long long)s.tick,s.window-2);
        ++s.completed;s.phase=3;s.index=0;restore(s.entry);
    }
}
std::array<uint8_t,2> input(unsigned peer,unsigned frame,bool permit_wrong=true) {
    if(permit_wrong && s.phase==1 && frame>=2 && peer!=s.local)return {0,0};
    return {(uint8_t)((frame*5+peer*3)&31),(uint8_t)(1u|(frame==3?64u:0u)|(frame==5?12u:0u)|(frame==6?2u:0u))};
}
}
extern "C" void mkdd_frame_probe_pads(uint8_t* pads) {
    memset(pads,0,32);
    // PadMgr precedes the next RaceApp boundary. Keep the prefix correct until
    // the fork has actually been captured; otherwise bad input leaks into the
    // checkpoint we are about to use to correct it.
    for(unsigned peer=0;peer<s.players;++peer){auto value=input(peer,s.index+1,s.index>=2);pads[2*s.seats[peer]]=value[0];pads[2*s.seats[peer]+1]=value[1];}
}
extern "C" void mkdd_full_frame_probe(CPU* c,unsigned players,unsigned local,const unsigned* seats,uint64_t tick,bool racing) {
    static const bool enabled=getenv("MKDD_REPLAY_FRAMES")!=nullptr;
    static const uint64_t start=[] {
        const char* value=getenv("MKDD_REPLAY_START");if(!value)return uint64_t{1080};
        char* end=nullptr;const auto parsed=strtoull(value,&end,10);
        if(!*value || *end || parsed<1080 || parsed>1000000)fatal("Invalid full-frame experiment start");
        return (uint64_t)parsed;
    }();
    if(!enabled)return;
    if(!s.active) {
        if(!racing || tick<start || tick%180 || s.completed>=8)return;
        if(!threads_have_owned_stacks() || !clock_is_virtual() || g_replay_guard)
            fatal("Full-frame probe requires owned stacks and virtual time");
        s.active=true;s.phase=0;s.index=0;s.players=players;s.local=local;s.tick=tick;s.began=host_ns();
        for(unsigned i=0;i<players;++i)s.seats[i]=seats[i];
        if(const char* value=getenv("MKDD_REPLAY_DEPTH")) {
            const int depth=atoi(value);if(depth<1 || depth>12)fatal("Invalid full-frame correction depth");s.window=(unsigned)depth+2;
        }
        s.guard.runtime_devices=true;s.guard.runtime_graphics=true;s.guard.render_graphics=rendered;s.guard.pads=pad_capture_states();s.audit={};clear_outputs();
    }else {++s.index;g_replay_guard=nullptr;g_replay_audit=nullptr;}
    threads_on_coordinator(boundary,nullptr);
    if(s.phase==3) {if(rendered)gx::pipeline_suppress_present(false);s.active=false;s.entry={};s.fork={};s.expected={};clear_outputs();clock_update_limit();return;}
    if(rendered)gx::pipeline_suppress_present(true);
    s.guard.maximum_edges=g_vcount+10000000;g_replay_guard=&s.guard;g_replay_audit=&s.audit;
    // The first update already passed PadMgr before entry; later frames also use
    // the normal PadMgr path. Expand the same deterministic fixture at this boundary.
    const CPU caller=*c;const uint32_t scratch=caller.r[1]-32;const uint16_t before=mem_r16(scratch);
    for(unsigned peer=0;peer<players;++peer) {
        const auto value=input(peer,s.index);mem_w8(scratch,value[0]);mem_w8(scratch+1,value[1]);
        *c=caller;c->r[3]=mem_r32(0x80352808+4*s.seats[peer]);c->r[4]=scratch;fn_801D1410(c);
    }
    mem_w16(scratch,before);*c=caller;
}
