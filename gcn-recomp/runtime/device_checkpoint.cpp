#include "device_checkpoint.h"
#include "thread_checkpoint.h"
#include "rewind_stack.h"
#include <stdexcept>
std::vector<DeviceFragment> checkpoint_race_devices() {
    if(!threads_have_owned_stacks() || RewindStack::active())
        throw std::logic_error("Device checkpoint requires the owned execution coordinator");
    return {checkpoint_clock(),checkpoint_interrupts(),checkpoint_vi(),checkpoint_si(),
        checkpoint_dsp(),checkpoint_dsp_hle(),checkpoint_jaudio(),checkpoint_ax(),checkpoint_ai(),checkpoint_di()};
}
