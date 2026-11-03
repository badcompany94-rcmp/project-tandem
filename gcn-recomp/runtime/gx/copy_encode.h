#pragma once
#include "render.h"
#include <span>
namespace gx {
// Encode already-converted copy pixels into GX tiles. Raw row 0 is the top of
// the source rect, independent of the GPU API. Failure leaves output unchanged.
bool encode_copy(const EfbCopyCmd&,unsigned scale,std::span<const uint8_t> rgba,
    std::vector<uint8_t>& output);
}
