/*
 * Standalone tests/benchmark for the unprojection API added to HandmadeMath.h
 * (HMM_Unproject, HMM_UnprojectBatch, HMM_UnprojectRay, HMM_TryInvViewProj).
 *
 * Not wired into the test Makefile on purpose; build and run directly:
 *
 *   cc -std=c99 -O2 -Wall -Wextra -I.. unproject_test.c -lm -o unproject_test
 *   ./unproject_test
 *
 * Exits non-zero if any check fails.
 */

#define _POSIX_C_SOURCE 199309L /* clock_gettime */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../HandmadeMath.h"

static int g_failures = 0;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            printf("  FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

/* Deterministic pseudo-random floats in [lo, hi) (xorshift32). */
static unsigned int g_rng_state = 0x12345678u;
static float frand(float lo, float hi)
{
    unsigned int x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return lo + (hi - lo) * ((float)(x & 0xFFFFFFu) / (float)0x1000000u);
}

/* Forward projection (world -> screen + depth), mirroring the mapping that
   HMM_Unproject inverts. */
static void project_forward(HMM_Vec3 world, HMM_Mat4 view_proj, HMM_UnprojectOpts opts,
                            HMM_Vec2 *out_screen, float *out_depth)
{
    HMM_Vec4 clip = HMM_MulM4V4(view_proj, HMM_V4V(world, 1.0f));
    HMM_Vec3 ndc = HMM_DivV3F(clip.XYZ, clip.W);

    out_screen->X = opts.ViewportX + (ndc.X + 1.0f) * 0.5f * opts.ViewportWidth;
    out_screen->Y = opts.ViewportY + (ndc.Y + 1.0f) * 0.5f * opts.ViewportHeight;

    {
        float t = opts.NDCZeroToOne ? ndc.Z : (ndc.Z + 1.0f) * 0.5f;
        *out_depth = opts.DepthMin + t * (opts.DepthMax - opts.DepthMin);
    }
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1.0e3 + (double)ts.tv_nsec / 1.0e6;
}

/* Round-trip one (screen, depth) pair; returns max screen error in pixels and
   depth error via out params. */
static void round_trip(HMM_Vec2 screen, float depth, HMM_Mat4 view_proj, HMM_Mat4 inv_view_proj,
                       HMM_UnprojectOpts opts, float *out_screen_err, float *out_depth_err)
{
    HMM_Vec3 world = HMM_Unproject(screen, depth, inv_view_proj, opts);
    HMM_Vec2 screen2;
    float depth2;
    project_forward(world, view_proj, opts, &screen2, &depth2);

    {
        float ex = screen2.X - screen.X;
        float ey = screen2.Y - screen.Y;
        *out_screen_err = HMM_MAX(HMM_ABS(ex), HMM_ABS(ey));
        *out_depth_err = HMM_ABS(depth2 - depth);
    }
}

static void test_round_trip_groups(HMM_Mat4 view_proj, HMM_Mat4 inv_view_proj, HMM_UnprojectOpts opts,
                                   const char *label)
{
    struct { const char *name; HMM_Vec2 screen; float depth; } cases[18];
    const char *group_names[] = { "center", "edge", "outside", "extreme depth" };
    float group_screen_err[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float group_depth_err[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int i, g, n;

    HMM_UnprojectOpts no_clip = opts;
    no_clip.ClipToDepthRange = 0; /* extreme-depth cases extrapolate outside [DepthMin, DepthMax] */

    n = 0;
#define CASE(group, sx, sy, d) \
    do { \
        cases[n].name = group; \
        cases[n].screen = HMM_V2(sx, sy); \
        cases[n].depth = d; \
        ++n; \
    } while (0)

    /* viewport center */
    CASE("center", 640.0f, 360.0f, 0.1f);
    CASE("center", 640.0f, 360.0f, 0.5f);
    CASE("center", 640.0f, 360.0f, 0.9f);
    /* viewport edges/corners */
    CASE("edge", 0.0f, 0.0f, 0.5f);
    CASE("edge", 1279.0f, 0.0f, 0.5f);
    CASE("edge", 0.0f, 719.0f, 0.5f);
    CASE("edge", 1279.0f, 719.0f, 0.5f);
    CASE("edge", 640.0f, 0.0f, 0.2f);
    CASE("edge", 0.0f, 360.0f, 0.8f);
    /* outside the viewport */
    CASE("outside", -100.0f, 360.0f, 0.5f);
    CASE("outside", 1400.0f, 360.0f, 0.5f);
    CASE("outside", 640.0f, -50.0f, 0.5f);
    CASE("outside", 640.0f, 900.0f, 0.5f);
    CASE("outside", -200.0f, -200.0f, 0.3f);
    /* extreme depths (exact near/far, plus extrapolation beyond both) */
    CASE("extreme depth", 640.0f, 360.0f, 0.0f);
    CASE("extreme depth", 640.0f, 360.0f, 1.0f);
    CASE("extreme depth", 320.0f, 180.0f, -0.5f);
    CASE("extreme depth", 960.0f, 540.0f, 2.0f);
#undef CASE

    for (i = 0; i < n; ++i)
    {
        float serr, derr;
        round_trip(cases[i].screen, cases[i].depth, view_proj, inv_view_proj, no_clip, &serr, &derr);
        for (g = 0; g < 4; ++g)
        {
            if (strcmp(cases[i].name, group_names[g]) == 0)
            {
                group_screen_err[g] = HMM_MAX(group_screen_err[g], serr);
                group_depth_err[g] = HMM_MAX(group_depth_err[g], derr);
            }
        }
    }

    printf("  [%s] round-trip max errors (screen px / depth):\n", label);
    for (g = 0; g < 4; ++g)
    {
        printf("    %-14s %9.6f px / %.3e\n", group_names[g], (double)group_screen_err[g], (double)group_depth_err[g]);
        CHECK(group_screen_err[g] < 0.05f, "%s: screen round-trip error too large: %g px", group_names[g], (double)group_screen_err[g]);
        CHECK(group_depth_err[g] < 1.0e-3f, "%s: depth round-trip error too large: %g", group_names[g], (double)group_depth_err[g]);
    }
}

#define BATCH_N 10000

static void test_batch(HMM_Mat4 inv_view_proj, HMM_UnprojectOpts opts)
{
    static HMM_Vec2 screens[BATCH_N];
    static float depths[BATCH_N];
    static HMM_Vec3 out_batch[BATCH_N];
    static HMM_Vec3 out_single[BATCH_N];
    int i;
    int mismatches = 0;
    int valid;
    double t0, t1, batch_ms, loop_ms;

    g_rng_state = 0xABCDEF1u;
    for (i = 0; i < BATCH_N; ++i)
    {
        screens[i] = HMM_V2(frand(-100.0f, 1380.0f), frand(-100.0f, 820.0f));
        depths[i] = frand(0.0f, 1.0f);
    }

    /* Bit-exactness: batch vs per-point loop. */
    valid = HMM_UnprojectBatch(out_batch, screens, depths, BATCH_N, inv_view_proj, opts);
    for (i = 0; i < BATCH_N; ++i)
    {
        out_single[i] = HMM_Unproject(screens[i], depths[i], inv_view_proj, opts);
    }
    for (i = 0; i < BATCH_N; ++i)
    {
        if (memcmp(&out_batch[i], &out_single[i], sizeof(HMM_Vec3)) != 0)
        {
            ++mismatches;
        }
    }
    printf("  bit-exact batch vs per-point: %d / %d mismatches, %d valid\n", mismatches, BATCH_N, valid);
    CHECK(mismatches == 0, "batch results differ bitwise from per-point results (%d mismatches)", mismatches);
    CHECK(valid == BATCH_N, "unexpected clipped points with clipping disabled: %d", BATCH_N - valid);

    /* Timing: one batch call over 10000 points, and the equivalent per-point loop. */
    t0 = now_ms();
    HMM_UnprojectBatch(out_batch, screens, depths, BATCH_N, inv_view_proj, opts);
    t1 = now_ms();
    batch_ms = t1 - t0;

    t0 = now_ms();
    for (i = 0; i < BATCH_N; ++i)
    {
        out_single[i] = HMM_Unproject(screens[i], depths[i], inv_view_proj, opts);
    }
    t1 = now_ms();
    loop_ms = t1 - t0;

    printf("  batch of %d points: %.3f ms (batch call) vs %.3f ms (per-point loop)\n",
           BATCH_N, batch_ms, loop_ms);
    CHECK(batch_ms >= 0.0, "nonsensical timing");
}

static void test_singular(void)
{
    HMM_Mat4 inv;
    HMM_Mat4 zero = {0};
    HMM_Mat4 dup_row = HMM_M4D(1.0f);
    HMM_Mat4 nan_m = HMM_M4D(1.0f);
    HMM_Mat4 inf_m = HMM_M4D(1.0f);
    HMM_Mat4 tiny = HMM_M4D(1.0f);
    HMM_Mat4 valid = HMM_MulM4(HMM_Perspective_RH_NO(HMM_AngleDeg(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f),
                               HMM_LookAt_RH(HMM_V3(3.0f, 4.0f, 5.0f), HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f)));

    dup_row.Elements[1][0] = dup_row.Elements[0][0]; /* duplicate a row -> det = 0 */
    dup_row.Elements[1][1] = dup_row.Elements[0][1];
    dup_row.Elements[1][2] = dup_row.Elements[0][2];
    dup_row.Elements[1][3] = dup_row.Elements[0][3];

    nan_m.Elements[0][0] = 0.0f / 0.0f;
    inf_m.Elements[0][0] = 1.0f / 0.0f;
    tiny.Elements[0][0] = 1.0e-30f; /* det = 1e-30, denormal-scale -> rejected */

    printf("  singular matrix handling:\n");
    printf("    zero matrix      -> %s\n", HMM_TryInvViewProj(zero, &inv) ? "accepted (BAD)" : "rejected");
    CHECK(!HMM_TryInvViewProj(zero, &inv), "zero matrix should be rejected");
    printf("    duplicate rows   -> %s\n", HMM_TryInvViewProj(dup_row, &inv) ? "accepted (BAD)" : "rejected");
    CHECK(!HMM_TryInvViewProj(dup_row, &inv), "rank-deficient matrix should be rejected");
    printf("    NaN element      -> %s\n", HMM_TryInvViewProj(nan_m, &inv) ? "accepted (BAD)" : "rejected");
    CHECK(!HMM_TryInvViewProj(nan_m, &inv), "NaN matrix should be rejected");
    printf("    Inf element      -> %s\n", HMM_TryInvViewProj(inf_m, &inv) ? "accepted (BAD)" : "rejected");
    CHECK(!HMM_TryInvViewProj(inf_m, &inv), "Inf matrix should be rejected");
    printf("    det ~ 1e-30      -> %s\n", HMM_TryInvViewProj(tiny, &inv) ? "accepted (BAD)" : "rejected");
    CHECK(!HMM_TryInvViewProj(tiny, &inv), "denormal-determinant matrix should be rejected");

    CHECK(HMM_TryInvViewProj(valid, &inv), "valid view-projection should be accepted");
    {
        /* M * M^-1 should be the identity. */
        HMM_Mat4 ident = HMM_MulM4(valid, inv);
        float max_err = 0.0f;
        int c, r;
        for (c = 0; c < 4; ++c)
        {
            for (r = 0; r < 4; ++r)
            {
                float expected = (c == r) ? 1.0f : 0.0f;
                max_err = HMM_MAX(max_err, HMM_ABS(ident.Elements[c][r] - expected));
            }
        }
        printf("    valid view-proj  -> accepted, |M*M^-1 - I| max = %.3e\n", (double)max_err);
        CHECK(max_err < 1.0e-3f, "inverse failed identity check: %g", (double)max_err);
    }
}

static void test_clipping(HMM_Mat4 inv_view_proj, HMM_UnprojectOpts opts)
{
    HMM_Vec2 screens[4] = {
        HMM_V2(640.0f, 360.0f), HMM_V2(640.0f, 360.0f),
        HMM_V2(100.0f, 100.0f), HMM_V2(100.0f, 100.0f)
    };
    float depths[4] = { -0.1f, 0.5f, 1.0f, 1.1f };
    HMM_Vec3 out[4];
    int valid;
    HMM_Vec3 clipped = HMM_Unproject(screens[0], depths[0], inv_view_proj, opts);

    printf("  depth clipping (range [0, 1], clipping enabled):\n");
    CHECK(clipped.X != clipped.X, "depth -0.1 should be clipped to NaN");
    printf("    depth -0.1 -> NaN (clipped)\n");

    valid = HMM_UnprojectBatch(out, screens, depths, 4, inv_view_proj, opts);
    printf("    batch of 4 (depths -0.1, 0.5, 1.0, 1.1) -> %d valid\n", valid);
    CHECK(valid == 2, "expected 2 valid points, got %d", valid);
    CHECK(out[0].X != out[0].X && out[3].X != out[3].X, "out-of-range depths should be NaN");
    CHECK(out[1].X == out[1].X && out[2].X == out[2].X, "in-range depths should be finite");
}

static void test_ray(HMM_Mat4 view_proj, HMM_Mat4 inv_view_proj, HMM_UnprojectOpts opts)
{
    /* A world point projected to screen must lie on the ray unprojected from
       that screen position. */
    HMM_Vec3 p = HMM_V3(1.5f, -2.0f, 0.5f);
    HMM_Vec2 screen;
    float depth;
    HMM_Ray ray;
    HMM_Vec3 to_p;
    float dist;

    project_forward(p, view_proj, opts, &screen, &depth);
    ray = HMM_UnprojectRay(screen, inv_view_proj, opts);

    /* distance from P to the ray line = |cross(P - Origin, Direction)| */
    to_p = HMM_SubV3(p, ray.Origin);
    dist = HMM_LenV3(HMM_Cross(to_p, ray.Direction));

    printf("  picking ray through projected world point:\n");
    printf("    screen (%.1f, %.1f), |direction| = %.6f, point-to-ray distance = %.3e\n",
           (double)screen.X, (double)screen.Y, (double)HMM_LenV3(ray.Direction), (double)dist);
    CHECK(HMM_ABS(HMM_LenV3(ray.Direction) - 1.0f) < 1.0e-4f, "ray direction not normalized");
    CHECK(dist < 1.0e-3f, "world point not on its picking ray: %g", (double)dist);
}

int main(void)
{
    HMM_UnprojectOpts opts_no, opts_zo;
    HMM_Mat4 view_no, view_zo, view_ortho;
    HMM_Mat4 inv_no, inv_zo, inv_ortho;

    opts_no.ViewportX = 0.0f;
    opts_no.ViewportY = 0.0f;
    opts_no.ViewportWidth = 1280.0f;
    opts_no.ViewportHeight = 720.0f;
    opts_no.DepthMin = 0.0f;
    opts_no.DepthMax = 1.0f;
    opts_no.NDCZeroToOne = 0;
    opts_no.ClipToDepthRange = 1;

    opts_zo = opts_no;
    opts_zo.NDCZeroToOne = 1;

    view_no = HMM_MulM4(HMM_Perspective_RH_NO(HMM_AngleDeg(60.0f), 1280.0f / 720.0f, 0.1f, 1000.0f),
                        HMM_LookAt_RH(HMM_V3(3.0f, 4.0f, 5.0f), HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f)));
    view_zo = HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(60.0f), 1280.0f / 720.0f, 0.1f, 1000.0f),
                        HMM_LookAt_RH(HMM_V3(3.0f, 4.0f, 5.0f), HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f)));
    view_ortho = HMM_MulM4(HMM_Orthographic_RH_NO(-16.0f, 16.0f, -9.0f, 9.0f, 0.1f, 100.0f),
                           HMM_LookAt_RH(HMM_V3(3.0f, 4.0f, 5.0f), HMM_V3(0.0f, 0.0f, 0.0f), HMM_V3(0.0f, 1.0f, 0.0f)));

    if (!HMM_TryInvViewProj(view_no, &inv_no) ||
        !HMM_TryInvViewProj(view_zo, &inv_zo) ||
        !HMM_TryInvViewProj(view_ortho, &inv_ortho))
    {
        printf("FATAL: could not invert test view-projection matrices\n");
        return 1;
    }

    printf("== Unprojection tests (viewport 1280x720, depth [0,1]) ==\n");

    printf("[1] Round-trip errors, perspective RH_NO:\n");
    test_round_trip_groups(view_no, inv_no, opts_no, "perspective NO");

    printf("[2] Round-trip errors, perspective RH_ZO:\n");
    test_round_trip_groups(view_zo, inv_zo, opts_zo, "perspective ZO");

    printf("[3] Round-trip errors, orthographic RH_NO:\n");
    test_round_trip_groups(view_ortho, inv_ortho, opts_no, "orthographic NO");

    printf("[4] Batch correctness and timing (%d points):\n", BATCH_N);
    test_batch(inv_no, opts_no);

    printf("[5] Singular matrices:\n");
    test_singular();

    printf("[6] Depth clipping:\n");
    test_clipping(inv_no, opts_no);

    printf("[7] Picking ray:\n");
    test_ray(view_no, inv_no, opts_no);

    if (g_failures)
    {
        printf("== %d CHECK(S) FAILED ==\n", g_failures);
        return 1;
    }

    printf("== ALL CHECKS PASSED ==\n");
    return 0;
}
