#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include "hw/pad.h"

// A diagnostic for candidate replay boundaries. Counts operations which a RAM
// copy cannot undo. Never suppresses an operation or changes guest execution.
struct ReplayAudit {
    uint64_t context_saves=0,context_loads=0,interrupts=0,events=0;
    uint64_t mmio_reads=0,mmio_writes=0,gpu_flushes=0,spr_writes=0;
    uint32_t first_mmio=0,first_context=0;
    bool externally_quiet() const {
        return !context_saves && !context_loads && !interrupts && !events &&
               !mmio_reads && !mmio_writes && !gpu_flushes && !spr_writes;
    }
};
// Only the guest thread holding the execution baton touches this pointer.
// The audit spans guest context switches; it is deliberately not thread_local.
extern ReplayAudit* g_replay_audit;

// Opt-in development probe, not a complete save-state implementation. The
// default scope defers interrupts and journals output-only controller writes.
// Owned-device scopes execute saved peripherals; owned-graphics scopes journal
// commands while a quiescent frontend stays unchanged. Unowned operations abort.
struct ReplayOutput {uint32_t address,value;};
struct ReplayGuard {
    std::vector<ReplayOutput> outputs;
    std::vector<uint8_t> pcm;
    std::vector<uint8_t> graphics;
    std::array<PadState,4> pads;
    uint64_t maximum_edges=0;
    bool runtime_devices=false;
    bool runtime_graphics=false;
    bool render_graphics=false;
    // Forward predicted frames freeze physical input too, but publish once.
    // Corrective re-execution suppresses those already-published side effects.
    bool publish_outputs=false;
    bool record_outputs=true;
};
extern ReplayGuard* g_replay_guard;
void replay_guard_forbid(const char* operation,uint32_t detail=0,bool owned_device=false);
bool replay_owned_address(uint32_t address);
bool replay_guard_write(uint32_t address,uint32_t value);
