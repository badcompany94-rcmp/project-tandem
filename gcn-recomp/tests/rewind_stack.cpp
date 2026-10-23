#include "rewind_stack.h"
#include "compat.h"
#include <array>
#include <csetjmp>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <xmmintrin.h>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"invalid operation accepted");}
struct World {uint64_t sum=0;unsigned step=0;std::array<uint64_t,8> values{};jmp_buf continuation;};
static void nested(World& world,unsigned depth,uint64_t seed) {
    volatile uint64_t local[64];for(unsigned i=0;i<64;++i)local[i]=seed+i;
    if(depth)nested(world,depth-1,seed*3+7);
    else {
        for(unsigned step=0;step<300;++step) {
            // This is the actual guest context-switch primitive, including
            // longjmp through frames that were restored from a previous tick.
            if(GCN_SETJMP(world.continuation)==0) {
                RewindStack::suspend();
                GCN_LONGJMP(world.continuation,1);
            }
            local[step%64]+=world.values[step%8]+step;
            world.sum=world.sum*6364136223846793005ULL+local[step%64];
            world.step=step+1;
        }
    }
    for(unsigned i=0;i<64;++i)world.sum^=local[i];
}
int main() {
    try {
        require(RewindStack::supported(),"unsupported test platform");
        const unsigned root_fp=_mm_getcsr();
        World world;RewindStack fiber(1u<<20,[&]{nested(world,24,123);});
        auto fresh=fiber.save();
        fiber.activate();
        for(unsigned round=0;round<250;++round) {
            auto saved=fiber.save();const World before=world;
            for(unsigned i=0;i<8;++i){world.values[i]=round*17+i;fiber.activate();}
            const World after=world;auto resulting=fiber.save();
            fiber.restore(saved);world=before;
            for(unsigned i=0;i<8;++i){world.values[i]=round*17+i;fiber.activate();}
            auto replayed=fiber.save();
            require(world.sum==after.sum && world.step==after.step,"rewound continuation differs");
            require(resulting.context==replayed.context && resulting.stack==replayed.stack,"restored native stack differs");
            // Return to the same early checkpoint for each different input run.
            fiber.restore(saved);world=before;
        }
        RewindStack other(1u<<20,[]{});
        rejects([&]{other.restore(fresh);});
        auto corrupt=fresh;corrupt.offset++;rejects([&]{fiber.restore(corrupt);});
        corrupt=fresh;corrupt.stack.pop_back();rejects([&]{fiber.restore(corrupt);});
        corrupt=fresh;corrupt.mxcsr=0xffffffffu;rejects([&]{fiber.restore(corrupt);});
        bool rejected=false;std::thread wrong([&]{try{fiber.save();}catch(const std::logic_error&){rejected=true;}});wrong.join();
        require(rejected,"cross-thread snapshot accepted");
        bool running_rejected=false;
        RewindStack* active=nullptr;
        RewindStack controls(1u<<20,[&]{
            try{active->save();}catch(const std::logic_error&){running_rejected=true;}
            _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_DOWN);
            RewindStack::suspend();
            require((_mm_getcsr()&_MM_ROUND_MASK)==_MM_ROUND_DOWN,"FP control not restored");
        });
        active=&controls;controls.activate();auto control=controls.save();
        require(_mm_getcsr()==root_fp,"root FP control changed");
        controls.activate();require(controls.finished(),"completion not recorded");
        rejects([&]{controls.activate();});
        controls.restore(control);controls.activate();
        require(controls.finished() && running_rejected,"restored completion/active validation failed");
        require(_mm_getcsr()==root_fp,"FP controls leaked after restore");
        struct Shared {uint64_t value=7,input=0;unsigned frame=0;} shared;
        RewindStack *main_stack=nullptr,*worker_stack=nullptr;
        RewindStack worker(1u<<20,[&]{
            uint64_t memory=31;
            for(;;) {
                memory=memory*17+shared.input;
                shared.value^=memory;
                main_stack->activate();
            }
        });
        RewindStack main(1u<<20,[&]{
            uint64_t memory=11;
            for(;;) {
                memory=memory*43+shared.input;
                worker_stack->activate();
                shared.value+=memory;++shared.frame;
                RewindStack::suspend();
            }
        });
        main_stack=&main;worker_stack=&worker;
        for(unsigned round=0;round<200;++round) {
            auto a=main.save(),b=worker.save();const auto before=shared;
            for(unsigned i=0;i<8;++i){shared.input=round*13+i;main.activate();}
            const auto correct=shared;
            main.restore(a);worker.restore(b);shared=before;
            for(unsigned i=0;i<8;++i){shared.input=round*13+i+1;main.activate();}
            require(shared.value!=correct.value,"two-stack fixture did not mispredict");
            main.restore(a);worker.restore(b);shared=before;
            for(unsigned i=0;i<8;++i){shared.input=round*13+i;main.activate();}
            require(shared.value==correct.value && shared.frame==correct.frame,"cross-stack correction diverged");
        }
        printf("PASS: 200 cross-stack correction windows, 1600 corrected updates with live continuation handoffs\n");
        printf("PASS: 2,000 replayed steps through nested stacks/setjmp, exact stack comparisons, FP controls, completion and invalid snapshot checks\n");
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
