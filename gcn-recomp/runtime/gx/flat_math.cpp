#include "flat_math.h"
#include <cmath>
namespace gx {
static constexpr int EFB_W=640,EFB_H=528;
bool flat_zfreeze_plane(const Batch& b, const Cmd& c, unsigned scale, float out[4]) {
    if (c.prim != 0 || c.count < 3) return false;
    const PixelState& st = b.states[c.state];
    const float* p = st.proj;
    const float* vp = st.viewport;
    const bool perspective = (int)p[6] == 0;
    int xoff, yoff;
    xoff = int(st.bp[0x59] & 0x3FF) * 2;
    yoff = int((st.bp[0x59] >> 10) & 0x3FF) * 2;
    const float vpa[4] = {2.0f * (vp[3] - xoff) / EFB_W - 1.0f, 2.0f * vp[0] / EFB_W, 2.0f * (vp[4] - yoff) / EFB_H - 1.0f, 2.0f * vp[1] / EFB_H};
    const float vpb[2] = {2.0f * vp[5] / 16777215.0f - 1.0f, 2.0f * vp[2] / 16777215.0f};
    double X[3], Y[3], Z[3];
    for (int i = 0; i < 3; i++) {
        const float* v = b.verts[b.indices[c.first + c.count - 3 + i]].pos;
        float cx, cy, cz, cw;
        if (perspective) { cx = p[0] * v[0] + p[1] * v[2]; cy = p[2] * v[1] + p[3] * v[2]; cw = -v[2]; }
        else { cx = p[0] * v[0] + p[1]; cy = p[2] * v[1] + p[3]; cw = 1.0f; }
        cz = p[4] * v[2] + p[5];
        if (cw <= 0.0f) return false;
        const float px = vpa[0] * cw + vpa[1] * cx, py = vpa[2] * cw + vpa[3] * cy, pz = vpb[0] * cw + vpb[1] * cz;
        X[i] = (px / cw + 1.0) * 0.5 * EFB_W * scale;
        Y[i] = (-py / cw + 1.0) * 0.5 * EFB_H * scale;
        Z[i] = (pz / cw + 1.0) * 0.5;
    }
    const double ux = X[1] - X[0], uy = Y[1] - Y[0], uz = Z[1] - Z[0];
    const double wx = X[2] - X[0], wy = Y[2] - Y[0], wz = Z[2] - Z[0];
    const double det = ux * wy - uy * wx;
    if (fabs(det) < 1e-9) return false;
    const double a = (uz * wy - uy * wz) / det, bb = (ux * wz - uz * wx) / det;
    out[0] = (float)a;
    out[1] = (float)bb;
    out[2] = (float)(Z[0] - a * X[0] - bb * Y[0]);
    out[3] = 1.0f;
    return true;
}

}
