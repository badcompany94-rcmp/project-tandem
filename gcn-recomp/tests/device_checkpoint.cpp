#include "device_checkpoint.h"
#include <bit>
#include <cstdio>
#include <deque>
#include <stdexcept>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        // Device state includes raw register arrays, pending variable-sized
        // protocol messages, atomics and floating point payloads. Snapshots must
        // own these values through container reallocation and repeated restores.
        uint32_t regs[3][4]{};
        volatile uint64_t counter=7;
        std::atomic<uint32_t> irq{9};
        std::deque<std::vector<uint32_t>> messages{{1,2,3},{4}};
        float coefficient=std::bit_cast<float>(uint32_t{0x7fc00001});
        auto first=checkpoint_fields("test devices",regs,counter,irq,messages,coefficient);
        require(first.matches(),"NaN payload did not match its own checkpoint");
        regs[2][3]=22;counter=19;irq=31;messages={{9,8}};coefficient=-0.0f;
        auto second=checkpoint_fields("test devices",regs,counter,irq,messages,coefficient);
        require(!first.matches() && second.matches(),"snapshots alias current values");
        coefficient=0.0f;require(!second.matches(),"floating point sign bit was lost");
        for(unsigned n=0;n<1000;++n) {
            messages.assign(20,std::vector<uint32_t>(n%97+1,n));regs[n%3][n%4]=n+100;
            counter=n+100;irq=n+100;coefficient=42;
            first.restore();require(first.matches(),"first checkpoint failed after container growth");
            require(counter==7 && irq==9 && regs[2][3]==0 && messages.front()==std::vector<uint32_t>({1,2,3}),"first values differ");
            second.restore();require(second.matches(),"second checkpoint was altered by restoring the first");
            require(counter==19 && irq==31 && regs[2][3]==22 && messages.size()==1,"second values differ");
        }
        auto retained=first;first={};retained.restore();require(retained.matches(),"copied checkpoint lost ownership");
        puts("PASS: 2,000 interleaved device restores after container reallocation; independent history, atomic/volatile registers and exact floating point bits");
        return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
