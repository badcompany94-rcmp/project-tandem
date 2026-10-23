#include "rewind_stack.h"
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <mutex>

#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <float.h>
#include <xmmintrin.h>
#define LIBCO_MP
#define LIBCO_MPROTECT
#define NVALGRIND
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wregister"
#endif
#include "../third_party/libco/amd64.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace {
struct Bounds {void* base;void* limit;};
Bounds bounds() {auto* tib=(NT_TIB*)NtCurrentTeb();return {tib->StackBase,tib->StackLimit};}
void bounds(Bounds b) {auto* tib=(NT_TIB*)NtCurrentTeb();tib->StackBase=b.base;tib->StackLimit=b.limit;}
unsigned short x87_read() {
#if defined(__clang__) || defined(__GNUC__)
    unsigned short v;__asm__ volatile("fnstcw %0" : "=m"(v));return v;
#else
    unsigned v=0;_controlfp_s(&v,0,0);return (unsigned short)v;
#endif
}
void x87_write(unsigned short v) {
#if defined(__clang__) || defined(__GNUC__)
    __asm__ volatile("fldcw %0" : : "m"(v));
#else
    unsigned unused=0;_controlfp_s(&unused,v,_MCW_RC|_MCW_PC|_MCW_EM);
#endif
}
struct Coordinator {cothread_t handle=nullptr;Bounds stack{};unsigned mxcsr=0;unsigned short x87=0;};
thread_local Coordinator root;
thread_local RewindStack* running=nullptr;
std::atomic<uint64_t> identities{0};
std::mutex derive_mutex;
}
struct RewindStack::Impl {
    uint64_t identity=++identities;
    std::thread::id owner=std::this_thread::get_id();
    uint8_t* allocation=nullptr;
    uint8_t* memory=nullptr;
    size_t bytes=0;
    cothread_t handle=nullptr;
    std::function<void()> entry;
    unsigned mxcsr=_mm_getcsr();
    unsigned short x87=x87_read();
    bool finished=false;
    void check()const {if(owner!=std::this_thread::get_id())throw std::logic_error("Execution stack used from another OS thread");}
};
bool RewindStack::supported(){return true;}
RewindStack* RewindStack::active(){return running;}
RewindStack::RewindStack(size_t bytes,std::function<void()> entry):impl_(new Impl) {
    if(!entry || bytes<65536 || bytes>128u*1024u*1024u)throw std::invalid_argument("Invalid execution stack");
    SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
    auto& p=*impl_;p.bytes=(bytes+page-1)&~(page-1);p.entry=std::move(entry);
    // Both guard pages remain inaccessible; committed storage has stable addresses
    // for jmp_buf pointers and is never relocated while snapshots may exist.
    p.allocation=(uint8_t*)VirtualAlloc(nullptr,p.bytes+2*page,MEM_RESERVE,PAGE_NOACCESS);
    if(!p.allocation)throw std::bad_alloc();
    p.memory=(uint8_t*)VirtualAlloc(p.allocation+page,p.bytes,MEM_COMMIT,PAGE_READWRITE);
    if(!p.memory){VirtualFree(p.allocation,0,MEM_RELEASE);throw std::bad_alloc();}
    {std::lock_guard<std::mutex> lock(derive_mutex);
     p.handle=co_derive(p.memory,(unsigned)p.bytes,&RewindStack::enter);}
}
RewindStack::~RewindStack() {
    if(running==this)std::terminate();
    if(impl_->allocation)VirtualFree(impl_->allocation,0,MEM_RELEASE);
}
bool RewindStack::finished()const {impl_->check();return impl_->finished;}
void RewindStack::enter() {
    auto* self=running;
    try {self->impl_->entry();}catch(...){std::terminate();}
    self->impl_->finished=true;
    suspend();
    std::terminate();
}
void RewindStack::transfer(RewindStack* next) {
    if(next==running)throw std::logic_error("Cannot switch an execution stack to itself");
    auto* previous=running;
    if(previous){previous->impl_->check();previous->impl_->mxcsr=_mm_getcsr();previous->impl_->x87=x87_read();}
    else {root={co_active(),bounds(),_mm_getcsr(),x87_read()};}
    cothread_t destination;
    if(next) {
        next->impl_->check();auto& p=*next->impl_;
        if(p.finished)throw std::logic_error("Cannot resume completed execution stack");
        destination=p.handle;bounds({p.memory+p.bytes,p.memory+512});
        _mm_setcsr(p.mxcsr);x87_write(p.x87);
    }else {
        if(!root.handle)throw std::logic_error("No execution coordinator");
        destination=root.handle;bounds(root.stack);_mm_setcsr(root.mxcsr);x87_write(root.x87);
    }
    running=next;
    co_switch(destination);
    // The switching stack restores this thread's bounds and FP controls before
    // returning here. Do not own RAII resources in this frame: guest longjmp may
    // resume above it, exactly as it does in the existing baton scheduler.
}
void RewindStack::activate(){transfer(this);}
void RewindStack::suspend(){if(!running)throw std::logic_error("Coordinator cannot suspend itself");transfer(nullptr);}
RewindStack::Snapshot RewindStack::save()const {
    const auto& p=*impl_;p.check();
    if(running==this)throw std::logic_error("Cannot snapshot active execution stack");
    uintptr_t sp=0;memcpy(&sp,p.memory,sizeof sp);
    const auto base=(uintptr_t)p.memory;
    if(sp<base+512 || sp>base+p.bytes || (sp&7))throw std::runtime_error("Invalid saved stack pointer");
    Snapshot s;s.identity=p.identity;s.offset=sp-base;
    memcpy(s.context.data(),p.memory,s.context.size());
    s.stack.assign(p.memory+s.offset,p.memory+p.bytes);
    s.mxcsr=p.mxcsr;s.x87=p.x87;s.finished=p.finished;return s;
}
void RewindStack::restore(const Snapshot& s) {
    auto& p=*impl_;p.check();
    if(running==this)throw std::logic_error("Cannot restore active execution stack");
    uintptr_t sp=0;memcpy(&sp,s.context.data(),sizeof sp);
    if(s.identity!=p.identity || s.offset<512 || s.offset>p.bytes ||
       s.stack.size()!=p.bytes-s.offset || sp!=(uintptr_t)(p.memory+s.offset) || (sp&7) || (s.mxcsr&0xffff0000u))
        throw std::invalid_argument("Snapshot does not belong to this execution stack");
    memcpy(p.memory,s.context.data(),s.context.size());
    memcpy(p.memory+s.offset,s.stack.data(),s.stack.size());
    p.mxcsr=s.mxcsr;p.x87=s.x87;p.finished=s.finished;
}
#else
struct RewindStack::Impl {};
bool RewindStack::supported(){return false;}
RewindStack* RewindStack::active(){return nullptr;}
RewindStack::RewindStack(size_t,std::function<void()>){throw std::runtime_error("Owned execution stacks require Windows x64");}
RewindStack::~RewindStack()=default;
void RewindStack::activate(){throw std::logic_error("Unsupported execution stack");}
void RewindStack::suspend(){throw std::logic_error("Unsupported execution stack");}
RewindStack::Snapshot RewindStack::save()const{throw std::logic_error("Unsupported execution stack");}
void RewindStack::restore(const Snapshot&){throw std::logic_error("Unsupported execution stack");}
bool RewindStack::finished()const{return true;}
#endif
