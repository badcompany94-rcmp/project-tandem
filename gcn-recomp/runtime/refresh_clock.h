#pragma once
#include <algorithm>
#include <cstdint>

// Absolute deadlines avoid rounding drift at rates such as 144 and 165 Hz.
// Late frames skip expired slots instead of issuing a burst of catch-up swaps.
class RefreshClock {
    uint64_t epoch = 0, slot = 0;
    unsigned rate = 0;
public:
    void reset() { epoch = slot = 0; rate = 0; }
    bool due(uint64_t now, unsigned hz) {
        if (!hz) { reset(); return true; }
        if (rate != hz || !slot || now < epoch) { epoch = now; slot = 1; rate = hz; return true; }
        if (now < deadline()) return false;
        slot = (now - epoch) * hz / 1000000000ULL + 1;
        return true;
    }
    uint64_t deadline() const { return rate ? epoch + (slot * 1000000000ULL + rate - 1) / rate : 0; }
};
