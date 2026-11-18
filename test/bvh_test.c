/*
  bvh_test.c — tests and benchmarks for the BVH ray-casting extension in
  HandmadeMath.h.

  This file is self-contained and does not touch the repository test harness
  or its build scripts. Build and run it directly, for example:

      cc -O2 -std=c11 bvh_test.c -lm -o bvh_test && ./bvh_test

  The same source also compiles as C++ (compile the file with a C++ driver).

  It contains two parts:
    1. Correctness tests for the three exact leaf routines (AABB, sphere,
       plane), the zero-direction contract, the broad-phase switch, the leaf
       capacity and O(n log n)-style build invariants, including bit-for-bit
       agreement with the per-primitive reference path on four scenarios.
    2. A benchmark over a small hand-built mixed scene that reports, for each
       of the four scenarios (dense hits, sparse hits, all miss, tangency):
       hit counts, consistency with the brute-force path, speedup and tree
       build memory. It also reports build and query timings for 10,000 rays.
*/

#define _POSIX_C_SOURCE 199309L

#include "../HandmadeMath.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* ------------------------------------------------------------------------- */
/* Tiny test harness                                                         */
/* ------------------------------------------------------------------------- */

static int G_TestsRun = 0;
static int G_TestsFailed = 0;

#define CHECK(Cond)                                                      \
    do {                                                                 \
        G_TestsRun++;                                                    \
        if (!(Cond)) {                                                   \
            G_TestsFailed++;                                             \
            printf("    [FAIL] line %d: %s\n", __LINE__, #Cond);        \
        }                                                                \
    } while (0)

static int FloatBitsEqual(float A, float B)
{
    union { float F; unsigned int U; } BA, BB;
    BA.F = A;
    BB.F = B;
    return BA.U == BB.U;
}

static HMM_Ray MakeRay(float Ox, float Oy, float Oz,
                       float Dx, float Dy, float Dz)
{
    HMM_Ray R;
    R.Origin = HMM_V3(Ox, Oy, Oz);
    R.Direction = HMM_V3(Dx, Dy, Dz);
    return R;
}

static HMM_BVHPrimitive MakeSphere(float Cx, float Cy, float Cz, float R,
                                   unsigned short User)
{
    HMM_BVHPrimitive P;
    P.Type = HMM_PRIM_SPHERE;
    P.Center = HMM_V3(Cx, Cy, Cz);
    P.Radius = R;
    P.AABB.Min = HMM_V3(0.0f, 0.0f, 0.0f);
    P.AABB.Max = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Plane.Normal = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Plane.Point = HMM_V3(0.0f, 0.0f, 0.0f);
    P.UserIndex = User;
    return P;
}

static HMM_BVHPrimitive MakeBox(float MinX, float MinY, float MinZ,
                                float MaxX, float MaxY, float MaxZ,
                                unsigned short User)
{
    HMM_BVHPrimitive P;
    P.Type = HMM_PRIM_AABB;
    P.AABB.Min = HMM_V3(MinX, MinY, MinZ);
    P.AABB.Max = HMM_V3(MaxX, MaxY, MaxZ);
    P.Center = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Radius = 0.0f;
    P.Plane.Normal = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Plane.Point = HMM_V3(0.0f, 0.0f, 0.0f);
    P.UserIndex = User;
    return P;
}

static HMM_BVHPrimitive MakePlane(float Nx, float Ny, float Nz,
                                  float Px, float Py, float Pz,
                                  unsigned short User)
{
    HMM_BVHPrimitive P;
    P.Type = HMM_PRIM_PLANE;
    P.Plane.Normal = HMM_V3(Nx, Ny, Nz);
    P.Plane.Point = HMM_V3(Px, Py, Pz);
    P.AABB.Min = HMM_V3(0.0f, 0.0f, 0.0f);
    P.AABB.Max = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Center = HMM_V3(0.0f, 0.0f, 0.0f);
    P.Radius = 0.0f;
    P.UserIndex = User;
    return P;
}

/* ------------------------------------------------------------------------- */
/* Exact leaf routine tests                                                  */
/* ------------------------------------------------------------------------- */

static void TestAABB(void)
{
    HMM_AABB Box;
    HMM_RayHit Hit;

    Box.Min = HMM_V3(-1.0f, -1.0f, -1.0f);
    Box.Max = HMM_V3(1.0f, 1.0f, 1.0f);

    /* Straight hit through the center: entry at t = 1. */
    Hit = HMM_RayTraceAABB(MakeRay(-3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 2.0f));
    CHECK(FloatBitsEqual(Hit.Point.X, -1.0f));

    /* Miss: parallel ray outside the box. */
    Hit = HMM_RayTraceAABB(MakeRay(-3.0f, 5.0f, 0.0f, 1.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 0);

    /* Miss: box is behind the ray. */
    Hit = HMM_RayTraceAABB(MakeRay(-3.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 0);

    /* Tangent: ray grazes the y = 1 face exactly. */
    Hit = HMM_RayTraceAABB(MakeRay(-3.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 2.0f));
    CHECK(FloatBitsEqual(Hit.Point.Y, 1.0f));

    /* Origin inside the box reports an entry distance of zero. */
    Hit = HMM_RayTraceAABB(MakeRay(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 0.0f));

    /* Zero direction is an explicit miss. */
    Hit = HMM_RayTraceAABB(MakeRay(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
                           Box);
    CHECK(Hit.Hit == 0);
}

static void TestSphere(void)
{
    HMM_RayHit Hit;
    HMM_Vec3 Center = HMM_V3(0.0f, 0.0f, 0.0f);

    /* Head-on hit on a unit sphere from x = -3: t = 2. */
    Hit = HMM_RayTraceSphere(MakeRay(-3.0f, 0.0f, 0.0f,
                                     1.0f, 0.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 2.0f));
    CHECK(FloatBitsEqual(Hit.Point.X, -1.0f));

    /* Clear miss. */
    Hit = HMM_RayTraceSphere(MakeRay(-3.0f, 3.0f, 0.0f,
                                     1.0f, 0.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 0);

    /* Sphere behind the ray. */
    Hit = HMM_RayTraceSphere(MakeRay(-3.0f, 0.0f, 0.0f,
                                     -1.0f, 0.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 0);

    /* Tangent ray: discriminant exactly zero, one contact point. */
    Hit = HMM_RayTraceSphere(MakeRay(-3.0f, 1.0f, 0.0f,
                                     1.0f, 0.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 3.0f));
    CHECK(FloatBitsEqual(Hit.Point.Y, 1.0f));

    /* Ray starting inside the sphere exits through the far side. */
    Hit = HMM_RayTraceSphere(MakeRay(0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 1.0f));

    /* Zero direction is an explicit miss. */
    Hit = HMM_RayTraceSphere(MakeRay(0.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f),
                             Center, 1.0f);
    CHECK(Hit.Hit == 0);
}

static void TestPlane(void)
{
    HMM_Plane Plane;
    HMM_RayHit Hit;

    /* Horizontal plane y = 0, normal pointing up. */
    Plane.Normal = HMM_V3(0.0f, 1.0f, 0.0f);
    Plane.Point = HMM_V3(0.0f, 0.0f, 0.0f);

    Hit = HMM_RayTracePlane(MakeRay(0.0f, 5.0f, 0.0f,
                                    0.0f, -1.0f, 0.0f), Plane);
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 5.0f));
    CHECK(FloatBitsEqual(Hit.Point.Y, 0.0f));

    /* Pointing away: no hit even though the plane is behind the ray. */
    Hit = HMM_RayTracePlane(MakeRay(0.0f, 5.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f), Plane);
    CHECK(Hit.Hit == 0);

    /* Parallel ray never intersects. */
    Hit = HMM_RayTracePlane(MakeRay(0.0f, 1.0f, 0.0f,
                                    1.0f, 0.0f, 0.0f), Plane);
    CHECK(Hit.Hit == 0);

    /* Zero direction is an explicit miss. */
    Hit = HMM_RayTracePlane(MakeRay(0.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 0.0f), Plane);
    CHECK(Hit.Hit == 0);
}

/* ------------------------------------------------------------------------- */
/* BVH build and query tests                                                 */
/* ------------------------------------------------------------------------- */

static HMM_BVHConfig MakeConfig(int Broad, int LeafCap)
{
    HMM_BVHConfig C;
    C.EnableBroadphase = Broad;
    C.LeafCapacity = LeafCap;
    return C;
}

/* Small hand-made scene: two spheres, two boxes, one plane. */
static const int SMALL_COUNT = 5;

static void MakeSmallScene(HMM_BVHPrimitive* P)
{
    P[0] = MakeSphere(0.0f, 0.0f, 0.0f, 1.0f, 10);
    P[1] = MakeSphere(10.0f, 0.0f, 0.0f, 1.0f, 11);
    P[2] = MakeBox(-2.0f, 5.0f, -2.0f, -1.0f, 6.0f, 2.0f, 12);
    P[3] = MakeBox(20.0f, 20.0f, 20.0f, 21.0f, 21.0f, 21.0f, 13);
    P[4] = MakePlane(0.0f, 1.0f, 0.0f, 0.0f, -5.0f, 0.0f, 14);
}

static void TestBuildAndTree(void)
{
    HMM_BVHPrimitive P[8];
    HMM_BVH Tree;
    HMM_BVHMemoryInfo Info;
    int Leaves, Inner, I, LeafPrims;

    MakeSmallScene(P);

    /* Invalid leaf capacity falls back to the default. */
    Tree = HMM_BVHBuild(P, SMALL_COUNT, MakeConfig(1, 0));
    CHECK(Tree.Nodes != NULL);
    CHECK(Tree.Config.LeafCapacity == HMM_BVH_DEFAULT_LEAF_CAPACITY);
    CHECK(Tree.PlaneCount == 1);
    CHECK(Tree.PrimitiveCount == (unsigned int) SMALL_COUNT);
    Info = HMM_BVHGetMemoryInfo(&Tree);
    CHECK(Info.TotalBytes > 0);
    CHECK(Info.PlaneBytes == sizeof(unsigned int));

    /* Every tree primitive is reachable exactly once via the leaves. */
    {
        int Seen[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        Leaves = 0;
        Inner = 0;
        LeafPrims = 0;
        for (I = 0; I < Tree.NodeCount; I++)
        {
            int K;
            if (Tree.Nodes[I].Count > 0)
            {
                Leaves++;
                CHECK(Tree.Nodes[I].Count <= Tree.Config.LeafCapacity);
                for (K = 0; K < Tree.Nodes[I].Count; K++)
                {
                    unsigned int Idx =
                        Tree.PrimIndices[Tree.Nodes[I].First + K];
                    CHECK(Idx < (unsigned int) SMALL_COUNT);
                    CHECK(Seen[Idx] == 0);
                    Seen[Idx] = 1;
                    LeafPrims++;
                }
            }
            else
            {
                Inner++;
                CHECK(Tree.Nodes[I].Left >= 0);
                CHECK(Tree.Nodes[I].Right >= 0);
            }
        }
        /* Four bounded primitives in the tree, the plane stays outside. */
        CHECK(LeafPrims == 4);
        for (I = 0; I < 4; I++)
        {
            CHECK(Seen[I] == 1);
        }
        CHECK(Seen[4] == 0);
        CHECK(Inner == Leaves - 1);
    }
    HMM_BVHFree(&Tree);

    /* Leaf capacity 1 forces one leaf per bounded primitive. */
    Tree = HMM_BVHBuild(P, SMALL_COUNT, MakeConfig(1, 1));
    CHECK(Tree.Nodes != NULL);
    Leaves = 0;
    for (I = 0; I < Tree.NodeCount; I++)
    {
        if (Tree.Nodes[I].Count > 0)
        {
            Leaves++;
            CHECK(Tree.Nodes[I].Count == 1);
        }
    }
    CHECK(Leaves == 4);
    HMM_BVHFree(&Tree);
}

static void TestEmptyAndDegenerate(void)
{
    HMM_BVH Tree;
    HMM_BVHPrimitive OnlyPlane[1];
    HMM_RayHit Hit;
    HMM_BVHTraceStats Stats;
    HMM_BVHPrimitive Same[6];
    int I;

    /* Building over zero primitives succeeds and always misses. */
    Tree = HMM_BVHBuild(NULL, 0, MakeConfig(1, 4));
    CHECK(Tree.Nodes != NULL);
    CHECK(Tree.NodeCount == 0);
    Hit = HMM_BVHTrace(&Tree, MakeRay(0.0f, 0.0f, 0.0f,
                                      1.0f, 0.0f, 0.0f));
    CHECK(Hit.Hit == 0);
    Hit = HMM_BVHTraceDetailed(&Tree,
                               MakeRay(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
                               &Stats);
    CHECK(Hit.Hit == 0);
    CHECK(Stats.AABBTests == 0);
    HMM_BVHFree(&Tree);

    /* Plane-only scene: no tree nodes, planes are still tested. */
    OnlyPlane[0] = MakePlane(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7);
    Tree = HMM_BVHBuild(OnlyPlane, 1, MakeConfig(1, 4));
    CHECK(Tree.Nodes != NULL);
    CHECK(Tree.NodeCount == 0);
    CHECK(Tree.PlaneCount == 1);
    Hit = HMM_BVHTrace(&Tree, MakeRay(0.0f, 3.0f, 0.0f,
                                      0.0f, -1.0f, 0.0f));
    CHECK(Hit.Hit == 1);
    CHECK(FloatBitsEqual(Hit.T, 3.0f));
    CHECK(Hit.Primitive == 0);
    CHECK(Hit.UserIndex == 7);
    HMM_BVHFree(&Tree);

    /* All centroids identical: the median split must still finish and cover
       every primitive (the O(n log n) build's degenerate input). */
    for (I = 0; I < 6; I++)
    {
        Same[I] = MakeSphere(2.0f, 2.0f, 2.0f, 0.5f,
                             (unsigned short) (100 + I));
    }
    Tree = HMM_BVHBuild(Same, 6, MakeConfig(1, 2));
    CHECK(Tree.Nodes != NULL);
    {
        int Seen[6] = {0, 0, 0, 0, 0, 0};
        int Total = 0;
        for (I = 0; I < Tree.NodeCount; I++)
        {
            int K;
            for (K = 0; K < Tree.Nodes[I].Count; K++)
            {
                unsigned int Idx =
                    Tree.PrimIndices[Tree.Nodes[I].First + K];
                Seen[Idx] = 1;
                Total++;
            }
        }
        CHECK(Total == 6);
        for (I = 0; I < 6; I++)
        {
            CHECK(Seen[I] == 1);
        }
    }
    Hit = HMM_BVHTrace(&Tree, MakeRay(2.0f, 2.0f, -5.0f,
                                      0.0f, 0.0f, 1.0f));
    CHECK(Hit.Hit == 1);
    HMM_BVHFree(&Tree);
}

/* Compares the accelerated result against the per-primitive reference. */
static int HitsAgree(HMM_RayHit A, HMM_RayHit B)
{
    if (A.Hit != B.Hit)
    {
        return 0;
    }
    if (!A.Hit)
    {
        return 1;
    }
    return A.Primitive == B.Primitive &&
           FloatBitsEqual(A.T, B.T) &&
           FloatBitsEqual(A.Point.X, B.Point.X) &&
           FloatBitsEqual(A.Point.Y, B.Point.Y) &&
           FloatBitsEqual(A.Point.Z, B.Point.Z);
}

static void TestQueryConsistencyAndZeroVector(void)
{
    HMM_BVHPrimitive P[8];
    HMM_BVH Broad, Narrow;
    HMM_Ray Rays[12];
    HMM_Ray Zero;
    int R;

    MakeSmallScene(P);
    Broad = HMM_BVHBuild(P, SMALL_COUNT, MakeConfig(1, 2));
    Narrow = HMM_BVHBuild(P, SMALL_COUNT, MakeConfig(0, 2));
    CHECK(Broad.Nodes != NULL && Narrow.Nodes != NULL);

    /* A spread of directions exercising all three primitive kinds. */
    Rays[0] = MakeRay(-3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    Rays[1] = MakeRay(0.0f, 0.0f, -8.0f, 0.0f, 0.0f, 1.0f);
    Rays[2] = MakeRay(0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f);
    Rays[3] = MakeRay(30.0f, 30.0f, 30.0f, -1.0f, -1.0f, -1.0f);
    Rays[4] = MakeRay(-5.0f, -5.0f, -5.0f, 1.0f, 1.0f, 1.0f);
    Rays[5] = MakeRay(-3.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f); /* tangent */
    Rays[6] = MakeRay(-3.0f, 1.0001f, 0.0f, 1.0f, 0.0f, 0.0f);
    Rays[7] = MakeRay(0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    Rays[8] = MakeRay(-1.5f, 5.5f, 0.0f, 0.0f, 0.0f, 1.0f);
    Rays[9] = MakeRay(50.0f, 50.0f, 50.0f, 1.0f, 0.0f, 0.0f);
    Rays[10] = MakeRay(-3.0f, 0.0f, 0.0f, 0.7071f, 0.7071f, 0.0f);
    Rays[11] = MakeRay(0.0f, -20.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    for (R = 0; R < 12; R++)
    {
        HMM_RayHit Ref = HMM_BVHTraceBruteforce(&Broad, Rays[R]);
        HMM_RayHit Fast = HMM_BVHTrace(&Broad, Rays[R]);
        HMM_RayHit Off = HMM_BVHTrace(&Narrow, Rays[R]);
        CHECK(HitsAgree(Ref, Fast));
        CHECK(HitsAgree(Ref, Off));
    }

    /* Zero direction: every public entry point is an explicit miss. */
    Zero = MakeRay(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(HMM_BVHTrace(&Broad, Zero).Hit == 0);
    CHECK(HMM_BVHTrace(&Narrow, Zero).Hit == 0);
    {
        HMM_BVHTraceStats S;
        CHECK(HMM_BVHTraceDetailed(&Broad, Zero, &S).Hit == 0);
        CHECK(S.AABBTests == 0 && S.PrimitiveTests == 0);
        CHECK(HMM_BVHTraceDetailed(&Narrow, Zero, &S).Hit == 0);
        CHECK(S.PrimitiveTests == 0);
    }

    /* Broad phase demonstrably prunes on a ray that misses everything. */
    {
        HMM_BVHTraceStats S;
        HMM_Ray Miss = MakeRay(50.0f, 50.0f, 50.0f,
                               1.0f, 1.0f, 1.0f);
        HMM_BVHTraceDetailed(&Broad, Miss, &S);
        CHECK(S.AABBTests >= 1);
        CHECK(S.PrimitiveTests < 4); /* at least one subtree was pruned */

        HMM_BVHTraceDetailed(&Narrow, Miss, &S);
        CHECK(S.PrimitiveTests == SMALL_COUNT);
        CHECK(S.AABBTests == 0);
    }

    HMM_BVHFree(&Broad);
    HMM_BVHFree(&Narrow);
}

/* ------------------------------------------------------------------------- */
/* Benchmark scene and RNG                                                   */
/* ------------------------------------------------------------------------- */

#define BENCH_SPHERES 512
#define BENCH_BOXES 509
#define BENCH_PLANES 3
#define BENCH_COUNT (BENCH_SPHERES + BENCH_BOXES + BENCH_PLANES)
#define RAY_COUNT 10000
#define BENCH_REPEATS 3

static unsigned int G_Seed = 0x12345678u;

static float RandFloat(float Lo, float Hi)
{
    /* Deterministic LCG, no library RNG state. */
    G_Seed = G_Seed * 1103515245u + 12345u;
    {
        float U = (float) (G_Seed >> 8) / (float) (1u << 24);
        return Lo + U * (Hi - Lo);
    }
}

static HMM_Vec3 Normalize(HMM_Vec3 V)
{
    return HMM_NormV3(V);
}

static void BuildBenchScene(HMM_BVHPrimitive* P)
{
    int I;
    int Idx = 0;

    /* 8x8x8 grid of unit spheres spanning roughly [-14, 14] per axis. */
    for (I = 0; I < BENCH_SPHERES; I++)
    {
        int Gx = I % 8;
        int Gy = (I / 8) % 8;
        int Gz = I / 64;
        P[Idx] = MakeSphere(-14.0f + 4.0f * Gx,
                            -14.0f + 4.0f * Gy,
                            -14.0f + 4.0f * Gz,
                            1.0f, (unsigned short) (Idx + 1));
        Idx++;
    }

    /* 7x7x7-ish grid of small boxes in a separate region. */
    for (I = 0; I < BENCH_BOXES; I++)
    {
        int Gx = I % 7;
        int Gy = (I / 7) % 7;
        int Gz = I / 49;
        float Cx = 30.0f + 3.0f * Gx;
        float Cy = -9.0f + 3.0f * Gy;
        float Cz = -9.0f + 3.0f * Gz;
        P[Idx] = MakeBox(Cx - 0.75f, Cy - 0.75f, Cz - 0.75f,
                         Cx + 0.75f, Cy + 0.75f, Cz + 0.75f,
                         (unsigned short) (Idx + 1));
        Idx++;
    }

    /* Three large ground/wall planes around the scene. */
    P[Idx] = MakePlane(0.0f, 1.0f, 0.0f, 0.0f, -16.0f, 0.0f,
                       (unsigned short) (Idx + 1));
    Idx++;
    P[Idx] = MakePlane(0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -16.0f,
                       (unsigned short) (Idx + 1));
    Idx++;
    P[Idx] = MakePlane(1.0f, 0.0f, 0.0f, -16.0f, 0.0f, 0.0f,
                       (unsigned short) (Idx + 1));
    Idx++;
}

typedef enum Scenario
{
    SCENARIO_DENSE = 0,
    SCENARIO_SPARSE,
    SCENARIO_MISS,
    SCENARIO_TANGENT,
    SCENARIO_COUNT
} Scenario;

static const char* ScenarioName(Scenario S)
{
    switch (S)
    {
        case SCENARIO_DENSE: return "dense hits";
        case SCENARIO_SPARSE: return "sparse hits";
        case SCENARIO_MISS: return "all miss";
        case SCENARIO_TANGENT: return "tangent";
        default: return "?";
    }
}

static HMM_Ray MakeScenarioRay(Scenario S, int RayIndex)
{
    HMM_Ray R;

    switch (S)
    {
        case SCENARIO_DENSE:
            /* Rays aimed through the sphere field: nearly all hit. */
            R.Origin = HMM_V3(RandFloat(-12.0f, 12.0f),
                              RandFloat(-12.0f, 12.0f), -40.0f);
            R.Direction = Normalize(HMM_V3(
                RandFloat(-0.15f, 0.15f),
                RandFloat(-0.15f, 0.15f), 1.0f));
            break;
        case SCENARIO_SPARSE:
            /*
              One ray in twenty is deliberately aimed straight at a sphere.
              The rest travel through the empty band above the scene and
              toward open sky, so only the aimed subset (plus a few random
              grazing contacts) produces hits.
            */
            R.Origin = HMM_V3(RandFloat(-12.0f, 12.0f),
                              24.0f + RandFloat(0.0f, 4.0f),
                              RandFloat(-12.0f, 12.0f));
            if (RayIndex % 20 == 0)
            {
                int Which = (RayIndex / 20) % BENCH_SPHERES;
                int Gx = Which % 8;
                int Gy = (Which / 8) % 8;
                int Gz = Which / 64;
                HMM_Vec3 Target = HMM_V3(-14.0f + 4.0f * Gx,
                                         -14.0f + 4.0f * Gy,
                                         -14.0f + 4.0f * Gz);
                R.Direction = Normalize(HMM_SubV3(Target, R.Origin));
            }
            else
            {
                /*
                  Into the empty +x/+y/+z quadrant: bounded primitives stop
                  at x ~ 52 and y,z ~ 15, while the three planes sit on the
                  negative faces (-16), so these rays cannot meet anything.
                */
                R.Direction = Normalize(HMM_V3(
                    RandFloat(0.6f, 1.0f), RandFloat(0.6f, 1.0f),
                    RandFloat(0.6f, 1.0f)));
            }
            break;

        case SCENARIO_MISS:
            /*
              Start above the whole scene and travel strictly upward and
              toward +x/+z. Every bounded primitive is below y = 31 and the
              three planes sit at -16 on each axis, so a ray with all three
              direction components positive can never reach any of them.
            */
            R.Origin = HMM_V3(RandFloat(-8.0f, 8.0f),
                              80.0f + RandFloat(0.0f, 8.0f),
                              RandFloat(-8.0f, 8.0f));
            R.Direction = Normalize(HMM_V3(
                RandFloat(0.4f, 1.0f), 1.0f, RandFloat(0.4f, 1.0f)));
            break;

        case SCENARIO_TANGENT:
        default:
            /*
              Aim straight at a sphere's center height plus an offset:
              offset == radius is an exact tangent (one contact, reported as
              a hit), offset just over the radius is a near miss. Alternating
              the two gives roughly equal tangent hits and clean misses.
            */
            {
                int Which = RayIndex % BENCH_SPHERES;
                int Gx = Which % 8;
                int Gy = (Which / 8) % 8;
                int Gz = Which / 64;
                float Cx = -14.0f + 4.0f * Gx;
                float Cy = -14.0f + 4.0f * Gy;
                float Cz = -14.0f + 4.0f * Gz;
                float Offset = (RayIndex % 2 == 0) ? 1.0f : 1.005f;
                R.Origin = HMM_V3(Cx - 30.0f, Cy + Offset, Cz);
                R.Direction = HMM_V3(1.0f, 0.0f, 0.0f);
            }
            break;
    }

    return R;
}

static double Seconds(const struct timespec* Start,
                      const struct timespec* End)
{
    return (double) (End->tv_sec - Start->tv_sec) +
           (double) (End->tv_nsec - Start->tv_nsec) * 1e-9;
}

static double ElapsedMin(const HMM_BVH* Tree, const HMM_Ray* Rays,
                         int UseBrute, int* HitsOut,
                         double* AvgPrimTests)
{
    double Best = 1.0e30;
    int Rep;

    for (Rep = 0; Rep < BENCH_REPEATS; Rep++)
    {
        struct timespec Start, End;
        int Hits = 0;
        long long PrimTests = 0;
        int I;

        clock_gettime(CLOCK_MONOTONIC, &Start);
        for (I = 0; I < RAY_COUNT; I++)
        {
            HMM_RayHit Hit;
            HMM_BVHTraceStats Stats;
            if (UseBrute)
            {
                Hit = HMM_BVHTraceBruteforce(Tree, Rays[I]);
            }
            else
            {
                Hit = HMM_BVHTraceDetailed(Tree, Rays[I], &Stats);
                PrimTests += Stats.PrimitiveTests;
            }
            if (Hit.Hit)
            {
                Hits++;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &End);

        {
            double T = Seconds(&Start, &End);
            if (T < Best)
            {
                Best = T;
            }
        }
        *HitsOut = Hits;
        *AvgPrimTests = UseBrute
            ? (double) Tree->PrimitiveCount
            : (double) PrimTests / (double) RAY_COUNT;
    }

    return Best;
}

/* Sphere-only scene used by the tangent scenario: no planes or boxes can
   intercept a ray before it reaches the target sphere. */
static void BuildTangentScene(HMM_BVHPrimitive* P)
{
    int I;
    for (I = 0; I < BENCH_SPHERES; I++)
    {
        int Gx = I % 8;
        int Gy = (I / 8) % 8;
        int Gz = I / 64;
        P[I] = MakeSphere(-14.0f + 4.0f * Gx,
                          -14.0f + 4.0f * Gy,
                          -14.0f + 4.0f * Gz,
                          1.0f, (unsigned short) (I + 1));
    }
}

static void RunBenchmark(void)
{
    static HMM_BVHPrimitive Primitives[BENCH_COUNT];
    static HMM_BVHPrimitive TangentPrims[BENCH_SPHERES];
    static HMM_Ray Rays[SCENARIO_COUNT][RAY_COUNT];
    HMM_BVH Tree, TangentTree;
    HMM_BVHMemoryInfo Info;
    struct timespec BuildStart, BuildEnd;
    double BuildSeconds;
    Scenario S;
    int I;

    BuildBenchScene(Primitives);
    BuildTangentScene(TangentPrims);

    clock_gettime(CLOCK_MONOTONIC, &BuildStart);
    Tree = HMM_BVHBuild(Primitives, BENCH_COUNT, MakeConfig(1, 4));
    clock_gettime(CLOCK_MONOTONIC, &BuildEnd);
    BuildSeconds = Seconds(&BuildStart, &BuildEnd);
    CHECK(Tree.Nodes != NULL);
    CHECK(Tree.PlaneCount == BENCH_PLANES);

    TangentTree = HMM_BVHBuild(TangentPrims, BENCH_SPHERES,
                               MakeConfig(1, 4));
    CHECK(TangentTree.Nodes != NULL);
    CHECK(TangentTree.PlaneCount == 0);

    Info = HMM_BVHGetMemoryInfo(&Tree);

    /* Generate every scenario's ray set once so both paths see identical
       input; reset the RNG per scenario for reproducibility. */
    for (S = SCENARIO_DENSE; S < SCENARIO_COUNT; S = (Scenario) (S + 1))
    {
        G_Seed = 0x12345678u + (unsigned int) S * 2654435761u;
        for (I = 0; I < RAY_COUNT; I++)
        {
            Rays[S][I] = MakeScenarioRay(S, I);
        }
    }

    printf("\n=== Benchmark: %d primitives (%d spheres, %d boxes, %d planes), "
           "%d rays per scenario ===\n",
           BENCH_COUNT, BENCH_SPHERES, BENCH_BOXES, BENCH_PLANES, RAY_COUNT);
    printf("Build: O(n log n) median-split BVH, leaf capacity %d, "
           "broad phase %s\n",
           Tree.Config.LeafCapacity,
           Tree.Config.EnableBroadphase ? "on" : "off");
    printf("Build time: %.3f ms for %d primitives "
           "(%.0f ns/primitive)\n",
           BuildSeconds * 1000.0, BENCH_COUNT,
           BuildSeconds * 1e9 / (double) BENCH_COUNT);
    printf("Tree nodes: %d (of %d capacity), planes in side list: %u\n",
           Tree.NodeCount, Tree.NodeCap, Tree.PlaneCount);
    printf("Memory: nodes %zu B, primitives %zu B, indices %zu B, "
           "plane list %zu B, scratch peak %zu B, total %zu B "
           "(%.1f KiB)\n",
           Info.NodeBytes, Info.PrimitiveBytes, Info.IndexBytes,
           Info.PlaneBytes, Info.ScratchPeakBytes, Info.TotalBytes,
           (double) Info.TotalBytes / 1024.0);

    printf("\n%-12s | %8s | %-22s | %9s | %8s | %9s\n",
           "scenario", "hits", "brute vs BVH", "speedup",
           "avg exact", "timing");
    printf("-------------+----------+------------------------+-----------+"
           "----------+-----------\n");

    for (S = SCENARIO_DENSE; S < SCENARIO_COUNT; S = (Scenario) (S + 1))
    {
        int BruteHits = 0, FastHits = 0, Mismatches = 0;
        double TBrute, TFast, AvgBruteTests, AvgFastTests;
        HMM_BVH* ActiveTree =
            (S == SCENARIO_TANGENT) ? &TangentTree : &Tree;

        TBrute = ElapsedMin(ActiveTree, Rays[S], 1, &BruteHits,
                            &AvgBruteTests);
        TFast = ElapsedMin(ActiveTree, Rays[S], 0, &FastHits,
                           &AvgFastTests);

        for (I = 0; I < RAY_COUNT; I++)
        {
            HMM_RayHit A = HMM_BVHTraceBruteforce(ActiveTree, Rays[S][I]);
            HMM_RayHit B = HMM_BVHTrace(ActiveTree, Rays[S][I]);
            if (!HitsAgree(A, B))
            {
                Mismatches++;
            }
        }

        printf("%-12s | %8d | %-22s | %8.2fx | %8.1f | %6.2f ms\n",
               ScenarioName(S), FastHits,
               Mismatches == 0
                   ? "identical (bit-for-bit)"
                   : "MISMATCH",
               TBrute / TFast,
               AvgFastTests, TFast * 1000.0);
        CHECK(BruteHits == FastHits);
        CHECK(Mismatches == 0);
        (void) AvgBruteTests;
    }

    /* Explicit 10,000-ray timing summary across all scenarios. */
    {
        double TotalFast = 0.0, TotalBrute = 0.0;
        for (S = SCENARIO_DENSE; S < SCENARIO_COUNT; S = (Scenario) (S + 1))
        {
            int Dummy;
            double D;
            HMM_BVH* ActiveTree =
                (S == SCENARIO_TANGENT) ? &TangentTree : &Tree;
            TotalBrute += ElapsedMin(ActiveTree, Rays[S], 1, &Dummy, &D);
            TotalFast += ElapsedMin(ActiveTree, Rays[S], 0, &Dummy, &D);
        }
        printf("\n10,000-ray totals over all %d scenarios: "
               "brute %.2f ms, BVH %.2f ms, overall speedup %.2fx\n",
               SCENARIO_COUNT, TotalBrute * 1000.0, TotalFast * 1000.0,
               TotalBrute / TotalFast);
    }

    HMM_BVHFree(&Tree);
    HMM_BVHFree(&TangentTree);
}

/* Build-time scaling check: doubling input should roughly double-plus a
   log factor, never go quadratic; reported, and bounded by a loose ratio. */
static void RunBuildScaling(void)
{
    static HMM_BVHPrimitive Pool[4096];
    const int Sizes[] = {256, 512, 1024, 2048, 4096};
    int K;

    for (K = 0; K < 4096; K++)
    {
        Pool[K] = MakeSphere(RandFloat(-30.0f, 30.0f),
                             RandFloat(-30.0f, 30.0f),
                             RandFloat(-30.0f, 30.0f),
                             0.5f, (unsigned short) K);
    }

    printf("\n=== Build scaling (O(n log n) check) ===\n");
    printf("%8s | %12s | %12s | %10s\n",
           "n", "build (ms)", "ns/prim", "n*log2(n)");
    for (K = 0; K < (int) (sizeof(Sizes) / sizeof(Sizes[0])); K++)
    {
        struct timespec Start, End;
        double T;
        HMM_BVH T2;
        clock_gettime(CLOCK_MONOTONIC, &Start);
        T2 = HMM_BVHBuild(Pool, (unsigned int) Sizes[K], MakeConfig(1, 4));
        clock_gettime(CLOCK_MONOTONIC, &End);
        T = Seconds(&Start, &End);
        CHECK(T2.Nodes != NULL);
        printf("%8d | %9.3f | %10.1f | %10.0f\n",
               Sizes[K], T * 1000.0, T * 1e9 / (double) Sizes[K],
               (double) Sizes[K] *
                   (log((double) Sizes[K]) / log(2.0)));
        HMM_BVHFree(&T2);
    }
}

int main(void)
{
    printf("Running BVH ray-casting tests...\n");

    TestAABB();
    TestSphere();
    TestPlane();
    TestBuildAndTree();
    TestEmptyAndDegenerate();
    TestQueryConsistencyAndZeroVector();

    RunBenchmark();
    RunBuildScaling();

    printf("\n========================================\n");
    if (G_TestsFailed == 0)
    {
        printf("All %d checks PASSED\n", G_TestsRun);
        return 0;
    }
    printf("%d of %d checks FAILED\n", G_TestsFailed, G_TestsRun);
    return 1;
}
