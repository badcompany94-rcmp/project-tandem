#include "copy_shaders.h"
#include "shadergen.h"
#include "gl.h"
namespace gx {
// Without a version line: glsl_header_with_clip() goes in front. Clip distances 4 and 5
// are enabled for every program on desktop GL (see render_init), so this writes them.
static const char* kCopyVS = R"(
out vec2 v_uv;
uniform vec4 u_rect;  // source rect in normalized EFB coords (x0,y0,x1,y1), y down
void main() {
    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    v_uv = mix(u_rect.xy, u_rect.zw, p);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
#ifdef GCN_CLIP
    gl_ClipDistance[4] = 1.0;
    gl_ClipDistance[5] = 1.0;
#endif
}
)";

// Converts EFB color (or depth) to the requested GX copy format. Output row 0 = top.
static const char* kCopyFS = GCN_GLSL_VERSION R"(
in vec2 v_uv;
uniform sampler2D u_src;
uniform sampler2D u_depth;
uniform int u_mode;   // copy texture format (+16 intensity, +32 depth)
out vec4 o;
void main() {
    vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec4 c = texture(u_src, uv);
    if ((u_mode & 32) != 0) {
        float z = texture(u_depth, uv).r;
        // Clamped: at the far plane z * 16777215 + 0.5 rounds up to 16777216 in single
        // precision, whose top byte is 0, so a cleared depth buffer copied as Z8 read as
        // the nearest depth instead of the farthest.
        uint zi = min(uint(z * 16777215.0 + 0.5), 16777215u);
        c = vec4(float((zi >> 16) & 255u), float((zi >> 8) & 255u), float(zi & 255u), 255.0) / 255.0;
        c = vec4(c.r, c.g, c.b, c.r);
        // The two-byte depth formats store the lower of their two bytes first: Z16 is
        // (middle, top) and Z16L (low, middle), the reverse of the colour order the
        // swizzles below assume. Read back as IA8 that puts the top byte in the
        // intensity and the middle in the alpha, and an indirect lookup that takes its
        // column from the alpha and its row from the intensity indexes a ramp whose rows
        // are the coarse axis. With the bytes the other way round the lookup is
        // transposed, and the ramp's steep last rows light up wherever the middle byte
        // nears 255, in lines across the fog.
        int dfmt = u_mode & 15;
        if (dfmt == 11) c = vec4(c.g, c.r, c.b, c.a);
        else if (dfmt == 12) c = vec4(c.r, c.b, c.g, c.a);
    }
    int fmt = u_mode & 15;
    bool intensity = (u_mode & 16) != 0;
    if (intensity) {
        float y = clamp(dot(c.rgb, vec3(0.257, 0.504, 0.098)) + 16.0 / 255.0, 0.0, 1.0);
        c.rgb = vec3(y);
        // I4 and I8 hold the intensity alone, which a texture of either format reads back
        // in all four components, the alpha too; IA4 and IA8 keep the frame's alpha. I4
        // keeps the top four bits of the 8-bit value: a game that reads the copy back as
        // CI4 indexes its palette with exactly those.
        if (fmt == 0) c = vec4(floor(floor(y * 255.0 + 0.5) / 16.0) / 15.0);
        else if (fmt == 1) c = vec4(y);
    }
    // Single-channel formats replicate into all components like the texture decoder does.
    // R4 (0) is the red channel at 4 bits, read back as I4: a projected shadow draws its
    // caster white into a corner of the frame and copies that as its mask, whose alpha
    // is then the red, not whatever the frame's alpha held there.
    if (fmt == 7) c = vec4(c.a);              // A8
    else if (fmt == 0 && !intensity) c = vec4(floor(c.r * 15.0 + 0.5) / 15.0);   // R4
    else if (fmt == 8) c = vec4(c.r);         // R8
    else if (fmt == 9) c = vec4(c.g);         // G8
    else if (fmt == 10) c = vec4(c.b);        // B8
    else if (fmt == 1 && !intensity) c = vec4(c.r);
    // The two-channel formats are read back as IA8: the first byte is the alpha and the
    // second the intensity. (A 16-bit depth copy is RG8 with its bytes swapped above.)
    else if (fmt == 11) c = vec4(c.g, c.g, c.g, c.r);   // RG8
    else if (fmt == 12) c = vec4(c.b, c.b, c.b, c.g);   // GB8
    if (fmt == 4) c.a = 1.0;                  // RGB565
    o = c;
}
)";

static const char* kBlitFS = GCN_GLSL_VERSION R"(
in vec2 v_uv;
uniform sampler2D u_src;
out vec4 o;
void main() { o = vec4(texture(u_src, vec2(v_uv.x, 1.0 - v_uv.y)).rgb, 1.0); }
)";

std::string scanout_pixel_shader(){return kBlitFS;}
std::string copy_vertex_shader(){return glsl_header_with_clip()+kCopyVS;}
std::string copy_pixel_shader(){return kCopyFS;}
}
