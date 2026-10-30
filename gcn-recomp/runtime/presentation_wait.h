#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// A render-thread timer, created lazily only for high-refresh output. Ordinary
// Windows millisecond waits can round to a scheduler tick in hidden/background
// windows. This avoids a process-wide timer-period change or a busy-spin loop.
class PresentationWait {
#ifdef _WIN32
    HANDLE timer=nullptr;
#endif
public:
    PresentationWait()=default;
    PresentationWait(const PresentationWait&)=delete;
    ~PresentationWait() { reset(); }
    void reset() {
#ifdef _WIN32
        if(timer)CloseHandle(timer);timer=nullptr;
#endif
    }
    void sleep(uint64_t ns) {
        if(!ns)return;
#ifdef _WIN32
        if(!timer)timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if(timer) {
            LARGE_INTEGER due{};due.QuadPart=-LONGLONG(std::max<uint64_t>(1,(ns+99)/100));
            if(SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE) && WaitForSingleObject(timer,INFINITE)==WAIT_OBJECT_0)return;
        }
#endif
        std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
    }
};
