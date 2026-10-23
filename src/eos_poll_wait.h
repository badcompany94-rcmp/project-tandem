#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdexcept>

namespace mkdd::eos {
// This helper has no SDL window to request precise timer scheduling. Use a
// private high-resolution waitable timer instead of coarse Sleep(1) polling.
class PollWait {
public:
    PollWait():timer_(CreateWaitableTimerExW(nullptr,nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE)) {
        if(!timer_)throw std::runtime_error("High-resolution online timer unavailable (Windows 10 1803 or newer required)");
    }
    ~PollWait(){CloseHandle(timer_);}
    PollWait(const PollWait&)=delete;
    PollWait& operator=(const PollWait&)=delete;
    void wait() {
        LARGE_INTEGER due{};due.QuadPart=-10000; // Relative 1 ms, in 100 ns units.
        if(!SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE) ||
           WaitForSingleObject(timer_,INFINITE)!=WAIT_OBJECT_0)
            throw std::runtime_error("Online polling timer failed");
    }
private:
    HANDLE timer_;
};
}
