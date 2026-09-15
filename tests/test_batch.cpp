// Host-side unit tests for draw batching / instancing / frame pacing.
// No GL, no wasm, no game data.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../src/draw_batch.hpp"

static int failures = 0;

static void expect(bool cond, const char* name) {
    if (!cond) {
        failures++;
        printf("FAIL %s\n", name);
    } else {
        printf("ok %s\n", name);
    }
}

int main() {
    using namespace th10;

    expect(kFvfWorldUv == 0x102, "WorldUv FVF");
    expect(is_world_uv_sprite(0x102, 20, kPrimStrip, 2), "sprite detect");
    expect(!is_world_uv_sprite(0x142, 24, kPrimStrip, 2), "colored world is not instanced");
    expect(!is_world_uv_sprite(0x102, 20, kPrimTriangles, 2), "triangle list is not instanced");
    expect(vertex_count(kPrimStrip, 2) == 4, "strip 2 -> 4 verts");
    expect(vertex_count(kPrimTriangles, 2) == 6, "two triangles -> 6 verts");
    expect(is_triangle_like(kPrimFan), "fan is triangle-like");
    expect(!is_triangle_like(kPrimLines), "lines are not triangle-like");

    // Expand a 4-vertex strip (count=2) into two triangles 0,1,2 and 2,1,3.
    uint8_t src[80];
    for (int i = 0; i < 4; i++) {
        float* v = (float*)(src + i * 20);
        v[0] = (float)i;
        v[1] = (float)(10 + i);
        v[2] = 0;
        v[3] = 0;
        v[4] = 0;  // uv
    }
    uint8_t dest[120];
    uint32_t n = expand_to_triangles(dest, src, 20, kPrimStrip, 2);
    expect(n == 2, "expand strip count");
    auto vx = [](const uint8_t* p) { return *(const float*)p; };
    expect(vx(dest) == 0 && vx(dest + 20) == 1 && vx(dest + 40) == 2,
           "first tri 0,1,2");
    expect(vx(dest + 60) == 2 && vx(dest + 80) == 1 && vx(dest + 100) == 3,
           "second tri 2,1,3 (strip winding)");

    CompactPipeline a{}, b{};
    a.fvf = b.fvf = kFvfWorldUv;
    a.stride = b.stride = 20;
    a.primitive = b.primitive = kPrimStrip;
    a.texture = b.texture = 7;
    a.target = b.target = 1;
    a.world[0] = 1;
    b.world[0] = 2;  // different sprite transform
    a.factor = 0xff00ff00;
    b.factor = 0xffffffff;
    expect(pipeline_key_equal(a, b), "key ignores world+factor");
    expect(!pipeline_equal(a, b), "full equal includes world");
    expect(!can_triangle_batch(a, b), "world sprites don't triangle-batch");

    uint8_t quad[80];
    memset(quad, 0, 80);
    expect(can_instance_with(a, b, quad, quad, 80),
           "same mesh + key => instance together");
    uint8_t quad2[80];
    memset(quad2, 1, 80);
    expect(!can_instance_with(a, b, quad, quad2, 80),
           "different mesh does not instance");

    CompactPipeline screen = a;
    screen.fvf = kFvfXyzRhw | kFvfDiffuse | kFvfTex1;  // 0x144
    screen.primitive = kPrimTriangles;
    CompactPipeline screen2 = screen;
    screen2.world[0] = 99;
    screen2.primitive = kPrimStrip;
    expect(can_triangle_batch(screen, screen2),
           "screen-space strip+list batch (world ignored)");

    InstanceRec rec{};
    write_instance(&rec, b);
    expect(rec.world[0] == 2 && rec.factor == 0xffffffffu, "instance rec");
    expect(sizeof(InstanceRec) == 68, "instance stride 68");

    expect(frame_sleep_ms(0) == 0, "no sleep when on-time");
    expect(frame_sleep_ms(1.0) == 0, "no 1ms leftover sleep");
    expect(frame_sleep_ms(1.9) == 0, "sub-2ms is vsync slack");
    expect(frame_sleep_ms(8.2) == 8, "sleep remaining whole ms");
    expect(frame_sleep_ms(999) == 50, "sleep cap");

    expect(kPipelineKeySize == offsetof(CompactPipeline, primitive),
           "key ends at primitive");

    printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
