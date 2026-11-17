/*
 * clip_test.c -- tests for the Sutherland-Hodgman batch polygon clipping API
 * in HandmadeMath.h (HMM_ClipPolygon / HMM_ClipPolygonOne).
 *
 * Standalone: no test framework, no third-party code, no allocation. Build
 * with test/clip/run_clip_test.sh, which compiles this file as both C and C++
 * under the repository's warning flags and runs both binaries.
 *
 * Exits non-zero if any check fails, so the script can treat a clean run as
 * success. Everything printed is also parsed by a human in the delivery
 * notes: vertex counts, area error, per-region consistency and timing.
 */

#include "../../HandmadeMath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks_failed = 0;
static int g_checks_run = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks_run++;                                                   \
        if (!(cond)) {                                                    \
            g_checks_failed++;                                            \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                 \
    } while (0)

static int nearly_equal(float A, float B, float Eps)
{
    float D = A - B;
    if (D < 0.0f) D = -D;
    return D <= Eps;
}

static float polygon_area(const HMM_Vec2 *P, int N)
{
    float TwiceArea = 0.0f;
    int I;

    if (N < 3) return 0.0f;
    for (I = 0; I < N; I++)
    {
        HMM_Vec2 A = P[I];
        HMM_Vec2 B = P[(I + 1) % N];
        TwiceArea += A.X * B.Y - A.Y * B.X;
    }
    if (TwiceArea < 0.0f) TwiceArea = -TwiceArea;
    return 0.5f * TwiceArea;
}

static int has_flag(int Flags, int Bit)
{
    return (Flags & Bit) != 0;
}

/* Storage sizing per the header contract:
 *   HMM_ClipPolygon:     3 * (subject count + sum of region counts)
 *   HMM_ClipPolygonOne:  2 * (subject count + region count)              */
#define BATCH_STORAGE(subj_n, reg_sum) (3 * ((subj_n) + (reg_sum)))
#define ONE_STORAGE(subj_n, reg_n)    (2 * ((subj_n) + (reg_n)))

/* ------------------------------------------------------------------ */
/* Independent reference: a second, separately written S-H pass used  */
/* only to check that batch chaining equals clipping region by region.*/
/* ------------------------------------------------------------------ */

static float ref_side(HMM_Vec2 A, HMM_Vec2 B, HMM_Vec2 P)
{
    return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
}

static HMM_Vec2 ref_intersect(HMM_Vec2 A, HMM_Vec2 B, HMM_Vec2 S, HMM_Vec2 E)
{
    float Dx = B.X - A.X;
    float Dy = B.Y - A.Y;
    float Den = Dx * (S.Y - E.Y) - Dy * (S.X - E.X);
    float T = (Dx * (S.Y - A.Y) - Dy * (S.X - A.X)) / Den;
    HMM_Vec2 R;
    R.X = S.X + T * (E.X - S.X);
    R.Y = S.Y + T * (E.Y - S.Y);
    return R;
}

/* Clips In against one region, returns the new count and sets *OutPtr to the
 * buffer holding the result. Out and Scratch hold InN + Clip.Count vertices
 * each for ping-pong clipping; Clean is a separate buffer of the same size
 * for the final coincident-vertex pass, so nothing is overwritten as it is
 * read. Written independently of the library routine (own side and
 * intersection helpers, own control flow), so vertex-for-vertex agreement is
 * a genuine cross-check rather than a tautology. */
static int ref_clip_one(const HMM_Vec2 *In, int InN, const HMM_Polygon Clip,
                        HMM_Vec2 *Out, HMM_Vec2 *Scratch, HMM_Vec2 *Clean,
                        HMM_Vec2 **OutPtr)
{
    int I, Edge, Active = 0, CurN = InN;
    HMM_Vec2 *Cur = Out;

    for (I = 0; I < InN; I++) Out[I] = In[I];

    for (Edge = 0; Edge < Clip.Count && CurN > 0; Edge++)
    {
        HMM_Vec2 A = Clip.Points[Edge];
        HMM_Vec2 B = Clip.Points[(Edge + 1) % Clip.Count];
        /* First edge writes Scratch, so the input copy in Out is safe; the
         * two buffers then alternate without ever aliasing the input. */
        HMM_Vec2 *Next = Active ? Out : Scratch;
        int NextN = 0;

        for (I = 0; I < CurN; I++)
        {
            HMM_Vec2 S = Cur[I];
            HMM_Vec2 E = Cur[(I + 1) % CurN];
            int SIn = ref_side(A, B, S) >= 0.0f;
            int EIn = ref_side(A, B, E) >= 0.0f;

            if (SIn && EIn)
                Next[NextN++] = E;
            else if (SIn && !EIn)
                Next[NextN++] = ref_intersect(A, B, S, E);
            else if (!SIn && EIn)
            {
                Next[NextN++] = ref_intersect(A, B, S, E);
                Next[NextN++] = E;
            }
        }
        Cur = Next;
        CurN = NextN;
        Active ^= 1;
    }

    /* Match the library's default cleanup: drop consecutive coincident
     * vertices so the comparison is like-for-like. Clean is a third buffer,
     * disjoint from the one being read. */
    if (CurN > 0)
    {
        int CleanN = 0;
        for (I = 0; I < CurN; I++)
        {
            if (CleanN > 0 &&
                nearly_equal(Clean[CleanN - 1].X, Cur[I].X, 1e-9f) &&
                nearly_equal(Clean[CleanN - 1].Y, Cur[I].Y, 1e-9f))
            {
                continue;
            }
            Clean[CleanN++] = Cur[I];
        }
        if (CleanN > 1 &&
            nearly_equal(Clean[CleanN - 1].X, Clean[0].X, 1e-9f) &&
            nearly_equal(Clean[CleanN - 1].Y, Clean[0].Y, 1e-9f))
        {
            CleanN--;
        }
        Cur = Clean;
        CurN = CleanN;
    }

    *OutPtr = Cur;
    return CurN;
}

static void print_poly(const char *Label, const HMM_Vec2 *P, int N, int Flags)
{
    int I;
    printf("  %-22s n=%d flags=0x%02X vertices:", Label, N, Flags);
    for (I = 0; I < N; I++)
        printf(" (%.3f,%.3f)", P[I].X, P[I].Y);
    printf("\n");
}

/* Prints the measured area and its absolute/relative error against the
 * hand-computed expected area for a fixture. */
static void report_area(const char *Label, const HMM_Vec2 *P, int N,
                        float Expected)
{
    float Got = polygon_area(P, N);
    float AbsErr = Got - Expected;
    float RelErr = (Expected != 0.0f) ? (AbsErr / Expected) : 0.0f;

    if (AbsErr < 0.0f) AbsErr = -AbsErr;
    if (RelErr < 0.0f) RelErr = -RelErr;
    printf("  %-22s area=%.6f expected=%.6f abs_err=%.2e rel_err=%.2e\n",
           Label, Got, Expected, AbsErr, RelErr);
}

/* ------------------------------------------------------------------ */
/* Shared small fixtures (integer coordinates, hand-verifiable).      */
/* ------------------------------------------------------------------ */

/* Region C: square [0,4]x[0,4], CCW. Region D: square [2,6]x[2,6], CCW.
 * Region E: triangle with vertices (1,1)(5,1)(1,5), CCW. The point arrays
 * have static storage duration so the HMM_Polygon wrappers never dangle;
 * they are filled in build_fixtures() because HMM_V2 is a function call,
 * not a constant initializer. */

static HMM_Polygon make_poly(HMM_Vec2 *P, int N)
{
    HMM_Polygon Poly;
    Poly.Points = P;
    Poly.Count = N;
    return Poly;
}

typedef struct ClipFixtures
{
    HMM_Vec2 RegionC[4];
    HMM_Vec2 RegionD[4];
    HMM_Vec2 RegionE[3];
    HMM_Polygon C, D, E;
} ClipFixtures;

static ClipFixtures g_fixtures;

static const ClipFixtures *build_fixtures(void)
{
    ClipFixtures *F = &g_fixtures;

    F->RegionC[0] = HMM_V2(0, 0);
    F->RegionC[1] = HMM_V2(4, 0);
    F->RegionC[2] = HMM_V2(4, 4);
    F->RegionC[3] = HMM_V2(0, 4);
    F->RegionD[0] = HMM_V2(2, 2);
    F->RegionD[1] = HMM_V2(6, 2);
    F->RegionD[2] = HMM_V2(6, 6);
    F->RegionD[3] = HMM_V2(2, 6);
    F->RegionE[0] = HMM_V2(1, 1);
    F->RegionE[1] = HMM_V2(5, 1);
    F->RegionE[2] = HMM_V2(1, 5);

    F->C = make_poly(F->RegionC, 4);
    F->D = make_poly(F->RegionD, 4);
    F->E = make_poly(F->RegionE, 3);
    return F;
}

/* --------------------------------------------------------------- */
/* 1. Convex subject fully inside one region: identity output.     */
/* --------------------------------------------------------------- */
static void test_convex_inside(const ClipFixtures *F)
{
    HMM_Vec2 subj[4] = {
        HMM_V2(1, 1), HMM_V2(2, 1), HMM_V2(2, 2), HMM_V2(1, 2)
    };
    HMM_Polygon In = make_poly(subj, 4);
    HMM_Polygon Clip = F->C;
    static HMM_Vec2 storage[BATCH_STORAGE(4, 4)];
    HMM_ClipResult R;

    printf("[1] convex subject inside one region\n");
    R = HMM_ClipPolygon(In, &Clip, 1, storage, 0);
    print_poly("convex-inside", R.Points, R.Count, R.Flags);
    report_area("convex-inside", R.Points, R.Count, 1.0f);

    CHECK(R.Count == 4);
    CHECK(R.Flags == 0);
    CHECK(nearly_equal(polygon_area(R.Points, R.Count), 1.0f, 1e-5f));
}

/* --------------------------------------------------------------- */
/* 2. Concave subject (L-shape) clipped by a square region.        */
/* --------------------------------------------------------------- */
static void test_concave(void)
{
    /* CCW L-shape, area 3. */
    HMM_Vec2 subj[6] = {
        HMM_V2(0, 0), HMM_V2(2, 0), HMM_V2(2, 1),
        HMM_V2(1, 1), HMM_V2(1, 2), HMM_V2(0, 2)
    };
    /* Region cuts everything with x > 1.5 away: [..] the remaining piece is
     * the rectangle [0,1.5]x[0,2] minus nothing, area 3 minus the clipped
     * 0.5x1 strip in the lower-right arm = 3 - 0.5 = 2.5. */
    HMM_Vec2 clip_pts[4] = {
        HMM_V2(-1, -1), HMM_V2(1.5f, -1), HMM_V2(1.5f, 3), HMM_V2(-1, 3)
    };
    HMM_Polygon In = make_poly(subj, 6);
    HMM_Polygon Clip = make_poly(clip_pts, 4);
    static HMM_Vec2 storage[BATCH_STORAGE(6, 4)];
    HMM_ClipResult R;

    printf("[2] concave L-shaped subject\n");
    R = HMM_ClipPolygon(In, &Clip, 1, storage, 0);
    print_poly("concave-clipped", R.Points, R.Count, R.Flags);
    report_area("concave-clipped", R.Points, R.Count, 2.5f);

    CHECK(R.Count >= 3);
    CHECK(!has_flag(R.Flags, HMM_CLIP_DEGENERATE));
    CHECK(nearly_equal(polygon_area(R.Points, R.Count), 2.5f, 1e-5f));
}

/* --------------------------------------------------------------- */
/* 3. Subject spanning several regions: chained clipping.          */
/* --------------------------------------------------------------- */
static void test_multi_region_chain(const ClipFixtures *F)
{
    /* Big square [0,8]^2, area 64; clipped by C then D then E. */
    HMM_Vec2 subj[4] = {
        HMM_V2(0, 0), HMM_V2(8, 0), HMM_V2(8, 8), HMM_V2(0, 8)
    };
    HMM_Polygon In = make_poly(subj, 4);
    HMM_Polygon Regions[3];
    static HMM_Vec2 storage[BATCH_STORAGE(4, 4 + 4 + 3)];
    static HMM_Vec2 ref_in[64];
    static HMM_Vec2 ref_out[64];
    static HMM_Vec2 ref_scratch[64];
    static HMM_Vec2 ref_clean[64];
    HMM_ClipResult R;
    HMM_Vec2 *RefPtr = ref_in;
    int RefN, I, Mismatches;

    Regions[0] = F->C;
    Regions[1] = F->D;
    Regions[2] = F->E;

    printf("[3] subject spanning three chained regions\n");
    R = HMM_ClipPolygon(In, Regions, 3, storage, 0);
    print_poly("chain C->D->E", R.Points, R.Count, R.Flags);
    report_area("chain C->D->E", R.Points, R.Count, 2.0f);

    /* Reference: same subject clipped one region at a time, independently,
     * using three disjoint scratch buffers per call. */
    RefN = 4;
    for (I = 0; I < RefN; I++) ref_in[I] = subj[I];
    RefN = ref_clip_one(RefPtr, RefN, Regions[0], ref_out, ref_scratch,
                        ref_clean, &RefPtr);
    RefN = ref_clip_one(RefPtr, RefN, Regions[1], ref_out, ref_scratch,
                        ref_clean, &RefPtr);
    RefN = ref_clip_one(RefPtr, RefN, Regions[2], ref_out, ref_scratch,
                        ref_clean, &RefPtr);

    printf("  %-22s n=%d\n", "reference per-region", RefN);
    CHECK(R.Count == RefN);
    CHECK(!has_flag(R.Flags, HMM_CLIP_DEGENERATE));

    /* Geometry of C then D: overlap square [2,4]^2, area 4. Triangle E keeps
     * the part under x+y<=6, cutting off the corner triangle above that
     * line: legs of length 2 (points (4,2),(2,4)), removed area 2. */
    CHECK(nearly_equal(polygon_area(R.Points, R.Count), 2.0f, 1e-4f));

    Mismatches = 0;
    if (R.Count == RefN)
    {
        for (I = 0; I < RefN; I++)
        {
            if (!nearly_equal(R.Points[I].X, RefPtr[I].X, 1e-5f) ||
                !nearly_equal(R.Points[I].Y, RefPtr[I].Y, 1e-5f))
            {
                Mismatches++;
            }
        }
    }
    else
    {
        Mismatches = -1;
    }
    CHECK(Mismatches == 0);
    printf("  per-region consistency: %d vertex mismatches\n",
           Mismatches < 0 ? -1 : Mismatches);

    /* RegionCount is configurable: zero regions is a cleaned passthrough. */
    {
        HMM_ClipResult R0 = HMM_ClipPolygon(In, Regions, 0, storage, 0);
        CHECK(R0.Count == 4);
        CHECK(R0.Flags == 0);
        CHECK(nearly_equal(polygon_area(R0.Points, R0.Count), 64.0f, 1e-4f));
    }
}

/* --------------------------------------------------------------- */
/* 4. Subject fully outside: empty, flagged degenerate + empty.    */
/* --------------------------------------------------------------- */
static void test_fully_outside(const ClipFixtures *F)
{
    HMM_Vec2 subj[4] = {
        HMM_V2(10, 10), HMM_V2(12, 10), HMM_V2(12, 12), HMM_V2(10, 12)
    };
    HMM_Polygon In = make_poly(subj, 4);
    HMM_Polygon Clip = F->C;
    static HMM_Vec2 storage[BATCH_STORAGE(4, 4)];
    HMM_ClipResult R;

    printf("[4] subject fully outside the region\n");
    R = HMM_ClipPolygon(In, &Clip, 1, storage, 0);
    print_poly("fully-outside", R.Points, R.Count, R.Flags);
    report_area("fully-outside", R.Points, R.Count, 0.0f);

    CHECK(R.Count == 0);
    CHECK(has_flag(R.Flags, HMM_CLIP_EMPTY));
    CHECK(has_flag(R.Flags, HMM_CLIP_DEGENERATE));
    CHECK(nearly_equal(polygon_area(R.Points, R.Count), 0.0f, 1e-9f));
}

/* --------------------------------------------------------------- */
/* 5. Degenerate inputs: self-intersection and repeated points.    */
/* --------------------------------------------------------------- */
static void test_degenerate_inputs(const ClipFixtures *F)
{
    /* Bowtie: (0,0)->(2,2)->(0,2)->(2,0) crosses in the middle. */
    HMM_Vec2 bowtie[4] = {
        HMM_V2(0, 0), HMM_V2(2, 2), HMM_V2(0, 2), HMM_V2(2, 0)
    };
    HMM_Vec2 repeated[5] = {
        HMM_V2(1, 1), HMM_V2(1, 1), HMM_V2(3, 1), HMM_V2(3, 3), HMM_V2(1, 3)
    };
    HMM_Polygon Clip = F->C;
    static HMM_Vec2 storage_bt[BATCH_STORAGE(4, 4)];
    static HMM_Vec2 storage_rp[BATCH_STORAGE(5, 4)];
    HMM_ClipResult Rbt, Rrp;

    printf("[5] degenerate inputs\n");

    Rbt = HMM_ClipPolygon(make_poly(bowtie, 4), &Clip, 1, storage_bt, 0);
    print_poly("self-intersecting", Rbt.Points, Rbt.Count, Rbt.Flags);
    CHECK(has_flag(Rbt.Flags, HMM_CLIP_INPUT_SELF_INTER));
    /* It still produces a well-defined vertex sequence inside the region. */
    CHECK(Rbt.Count >= 3);

    Rrp = HMM_ClipPolygon(make_poly(repeated, 5), &Clip, 1, storage_rp, 0);
    print_poly("repeated-points", Rrp.Points, Rrp.Count, Rrp.Flags);
    CHECK(has_flag(Rrp.Flags, HMM_CLIP_INPUT_REPEATED));
    /* Duplicate removed: the cleaned subject is a square of area 4. */
    CHECK(Rrp.Count == 4);
    CHECK(nearly_equal(polygon_area(Rrp.Points, Rrp.Count), 4.0f, 1e-5f));

    /* Collapsed input: all points identical. */
    {
        HMM_Vec2 flat[3] = { HMM_V2(1, 1), HMM_V2(1, 1), HMM_V2(1, 1) };
        static HMM_Vec2 storage_flat[BATCH_STORAGE(3, 4)];
        HMM_ClipResult Rf =
            HMM_ClipPolygon(make_poly(flat, 3), &Clip, 1, storage_flat, 0);
        print_poly("all-coincident", Rf.Points, Rf.Count, Rf.Flags);
        CHECK(has_flag(Rf.Flags, HMM_CLIP_INPUT_REPEATED));
        CHECK(has_flag(Rf.Flags, HMM_CLIP_DEGENERATE));
        CHECK(Rf.Count == 1);
    }
}

/* --------------------------------------------------------------- */
/* 6. PreserveEdges: zero-length edges kept vs removed + flagged.  */
/* --------------------------------------------------------------- */
static void test_preserve_edges(const ClipFixtures *F)
{
    /* Triangle outside region C that touches the region only at corner
     * (0,0). Two clip edges meet the subject at that same corner, so the
     * boundary handling emits two coincident consecutive vertices. */
    HMM_Vec2 subj[3] = {
        HMM_V2(0, 0), HMM_V2(4, -2), HMM_V2(-2, 4)
    };
    HMM_Polygon In = make_poly(subj, 3);
    HMM_Polygon Clip = F->C;
    static HMM_Vec2 storage_keep[BATCH_STORAGE(3, 4)];
    static HMM_Vec2 storage_drop[BATCH_STORAGE(3, 4)];
    HMM_ClipResult Keep, Drop;
    int I, Repeats = 0;

    printf("[6] preserve-degenerate-edges option\n");

    Keep = HMM_ClipPolygon(In, &Clip, 1, storage_keep, 1);
    Drop = HMM_ClipPolygon(In, &Clip, 1, storage_drop, 0);
    print_poly("preserve=1", Keep.Points, Keep.Count, Keep.Flags);
    print_poly("preserve=0", Drop.Points, Drop.Count, Drop.Flags);

    for (I = 0; I < Keep.Count; I++)
    {
        HMM_Vec2 A = Keep.Points[I];
        HMM_Vec2 B = Keep.Points[(I + 1) % Keep.Count];
        if (nearly_equal(A.X, B.X, 1e-6f) && nearly_equal(A.Y, B.Y, 1e-6f))
            Repeats++;
    }
    CHECK(Repeats > 0);
    CHECK(Keep.Count == 5);
    CHECK(Drop.Count == 3);
    CHECK(Keep.Count >= Drop.Count);
    CHECK(has_flag(Drop.Flags, HMM_CLIP_DROPPED_EDGES));
    CHECK(!has_flag(Keep.Flags, HMM_CLIP_DROPPED_EDGES));
    /* Same geometry either way: the surviving triangle has area 2. */
    CHECK(nearly_equal(polygon_area(Keep.Points, Keep.Count), 2.0f, 1e-5f));
    CHECK(nearly_equal(polygon_area(Drop.Points, Drop.Count), 2.0f, 1e-5f));
}

/* --------------------------------------------------------------- */
/* 7. Public per-region entry point matches the batch entry point. */
/* --------------------------------------------------------------- */
static void test_one_matches_batch(const ClipFixtures *F)
{
    HMM_Vec2 subj[4] = {
        HMM_V2(-1, -1), HMM_V2(5, -1), HMM_V2(5, 5), HMM_V2(-1, 5)
    };
    HMM_Polygon In = make_poly(subj, 4);
    HMM_Polygon Clip = F->C;
    static HMM_Vec2 s_one[ONE_STORAGE(4, 4)];
    static HMM_Vec2 s_batch[BATCH_STORAGE(4, 4)];
    HMM_ClipResult One, Batch;
    int I, Mismatches = 0;

    printf("[7] HMM_ClipPolygonOne vs HMM_ClipPolygon\n");

    One = HMM_ClipPolygonOne(In, Clip, s_one, 0, 0);
    Batch = HMM_ClipPolygon(In, &Clip, 1, s_batch, 0);
    print_poly("one-region API", One.Points, One.Count, One.Flags);
    print_poly("batch API", Batch.Points, Batch.Count, Batch.Flags);

    CHECK(One.Count == Batch.Count);
    CHECK(One.Flags == Batch.Flags);
    if (One.Count == Batch.Count)
    {
        for (I = 0; I < One.Count; I++)
        {
            if (!nearly_equal(One.Points[I].X, Batch.Points[I].X, 1e-6f) ||
                !nearly_equal(One.Points[I].Y, Batch.Points[I].Y, 1e-6f))
                Mismatches++;
        }
    }
    else
    {
        Mismatches = -1;
    }
    CHECK(Mismatches == 0);
    CHECK(One.Count == 4);
    CHECK(nearly_equal(polygon_area(One.Points, One.Count), 16.0f, 1e-4f));

    /* Clockwise region must give the same answer as CCW. */
    {
        HMM_Vec2 cw[4] = {
            HMM_V2(0, 0), HMM_V2(0, 4), HMM_V2(4, 4), HMM_V2(4, 0)
        };
        static HMM_Vec2 s_cw[ONE_STORAGE(4, 4)];
        HMM_ClipResult Rcw =
            HMM_ClipPolygonOne(In, make_poly(cw, 4), s_cw, 0, 0);
        print_poly("clockwise region", Rcw.Points, Rcw.Count, Rcw.Flags);
        CHECK(Rcw.Count == One.Count);
        CHECK(nearly_equal(polygon_area(Rcw.Points, Rcw.Count), 16.0f, 1e-4f));
    }
}

/* --------------------------------------------------------------- */
/* 8. Timing: batch cost over the cross-region fixture.            */
/* --------------------------------------------------------------- */
static void test_timing(const ClipFixtures *F, int Iterations)
{
    HMM_Vec2 subj[4] = {
        HMM_V2(0, 0), HMM_V2(8, 0), HMM_V2(8, 8), HMM_V2(0, 8)
    };
    HMM_Polygon In = make_poly(subj, 4);
    HMM_Polygon Regions[3];
    static HMM_Vec2 storage[BATCH_STORAGE(4, 11)];
    clock_t Start, End;
    double ElapsedMs, PerCallUs;
    int I, Accum = 0;

    Regions[0] = F->C;
    Regions[1] = F->D;
    Regions[2] = F->E;

    printf("[8] batch timing (%d iterations, 3 regions each)\n", Iterations);
    Start = clock();
    for (I = 0; I < Iterations; I++)
    {
        HMM_ClipResult R = HMM_ClipPolygon(In, Regions, 3, storage, 0);
        Accum += R.Count;
    }
    End = clock();

    ElapsedMs = (double)(End - Start) * 1000.0 / CLOCKS_PER_SEC;
    PerCallUs = ElapsedMs * 1000.0 / (double)Iterations;
    printf("  total=%.3f ms  per-call=%.3f us  (checksum %d)\n",
           ElapsedMs, PerCallUs, Accum);
    CHECK(Accum == Iterations * 3);
}

int main(int Argc, char **Argv)
{
    int Iterations = 200000;

    if (Argc > 1)
        Iterations = atoi(Argv[1]);
    if (Iterations <= 0)
        Iterations = 200000;

    const ClipFixtures *F = build_fixtures();

    test_convex_inside(F);
    test_concave();
    test_multi_region_chain(F);
    test_fully_outside(F);
    test_degenerate_inputs(F);
    test_preserve_edges(F);
    test_one_matches_batch(F);
    test_timing(F, Iterations);

    printf("\n%d checks run, %d failed\n", g_checks_run, g_checks_failed);
    return g_checks_failed == 0 ? 0 : 1;
}
