/*
  quickhull_demo.c

  Exercise and benchmark the 3D Quickhull implementation in HandmadeMath.h.
  It builds four small point clouds, reports vertex/face counts, volume
  error, a comparison against an independent brute-force hull, and timing;
  it also checks the degenerate inputs (too few points, all coplanar,
  duplicates) and the configurable tolerance / coplanar-point options.

  The file is plain C99 and also compiles unmodified as C++98, so the same
  checks run against both language bindings of the single-header library.
  It depends only on the C standard library and HandmadeMath itself.

  Build (see run_quickhull_demo.sh):
    cc  -std=c99   -I.. quickhull_demo.c -lm -o quickhull_demo_c
    c++ -std=c++98 -I.. -x c++ quickhull_demo.c -lm -o quickhull_demo_cpp
*/

#include "HandmadeMath.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Small fixed-size scaffolding, sized for the tiny demo point clouds. */
/* ------------------------------------------------------------------ */

#define MAX_POINTS 64
#define MAX_TRIS   16384
#define MAX_PLANES 512

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                          \
    do {                                                          \
        g_checks++;                                               \
        if (!(cond)) {                                            \
            g_failures++;                                         \
            printf("    [FAIL] %s (line %d)\n", (msg), __LINE__); \
        }                                                         \
    } while (0)

static int nearly_equal(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= tol;
}

/* ------------------------------------------------------------------ */
/* Brute-force convex hull.                                           */
/*                                                                     */
/* Every triple of points is tested: it is a supporting triangle if   */
/* all other points lie on one side of its plane. Supporting planes    */
/* are deduplicated into planar hull faces, whose polygon area gives   */
/* the exact hull volume through the divergence theorem. O(n^4).       */
/* ------------------------------------------------------------------ */

typedef struct BrutePlane
{
    HMM_Vec3 Normal;       /* unit, outward */
    float D;               /* plane: Normal . x = D */
    int Members[MAX_POINTS];
    int MemberCount;
} BrutePlane;

typedef struct BruteTri
{
    int A, B, C;
} BruteTri;

typedef struct BruteHull
{
    int VertexSet[MAX_POINTS]; /* 1 if the point is on the hull */
    int VertexCount;
    int PlaneCount;            /* planar hull faces */
    int TriCount;              /* supporting triangles found */
    float Volume;
    BrutePlane Planes[MAX_PLANES];
    BruteTri Tris[MAX_TRIS];
} BruteHull;

static BruteHull brute_force(const HMM_Vec3* pts, int n, float eps)
{
    BruteHull out;
    int i, j, k, p, q;
    int plane_count = 0;
    int tri_count = 0;

    for (i = 0; i < n; i++) out.VertexSet[i] = 0;
    out.VertexCount = 0;
    out.PlaneCount = 0;
    out.TriCount = 0;
    out.Volume = 0.0f;

    for (i = 0; i < n; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            for (k = j + 1; k < n; k++)
            {
                HMM_Vec3 raw = HMM_Cross(HMM_SubV3(pts[j], pts[i]),
                                         HMM_SubV3(pts[k], pts[i]));
                float len = HMM_LenV3(raw);
                HMM_Vec3 normal;
                float max_s = -1.0e30f;
                float min_s = 1.0e30f;
                float d;
                int duplicate = 0;
                int slot;

                if (len <= eps)
                {
                    continue; /* degenerate triple */
                }
                normal = HMM_MulV3F(raw, 1.0f / len);

                for (p = 0; p < n; p++)
                {
                    float s = HMM_DotV3(HMM_SubV3(pts[p], pts[i]), normal);
                    if (s > max_s) max_s = s;
                    if (s < min_s) min_s = s;
                }

                if (max_s <= eps)
                {
                    /* All points on/below: outward normal is the negation. */
                    normal = HMM_MulV3F(normal, -1.0f);
                }
                else if (min_s < -eps)
                {
                    continue; /* points on both sides: not supporting */
                }

                d = HMM_DotV3(normal, pts[i]);

                /* Record the supporting triangle (dedup exact triples). */
                {
                    int t;
                    for (t = 0; t < tri_count; t++)
                    {
                        if (out.Tris[t].A == i && out.Tris[t].B == j
                            && out.Tris[t].C == k)
                        {
                            duplicate = 1;
                            break;
                        }
                    }
                    if (!duplicate && tri_count < MAX_TRIS)
                    {
                        out.Tris[tri_count].A = i;
                        out.Tris[tri_count].B = j;
                        out.Tris[tri_count].C = k;
                        tri_count++;
                    }
                }

                /* Merge into an existing supporting plane. */
                slot = -1;
                for (q = 0; q < plane_count; q++)
                {
                    HMM_Vec3 dn = HMM_SubV3(out.Planes[q].Normal, normal);
                    if (HMM_LenV3(dn) <= 1.0e-4f
                        && nearly_equal(out.Planes[q].D, d, 1.0e-4f))
                    {
                        slot = q;
                        break;
                    }
                }
                if (slot == -1 && plane_count < MAX_PLANES)
                {
                    slot = plane_count;
                    out.Planes[slot].Normal = normal;
                    out.Planes[slot].D = d;
                    out.Planes[slot].MemberCount = 0;
                    plane_count++;
                }
                if (slot >= 0)
                {
                    int m;
                    int trio[3] = { i, j, k };
                    for (m = 0; m < 3; m++)
                    {
                        int v = trio[m];
                        int found = 0;
                        int e;
                        for (e = 0; e < out.Planes[slot].MemberCount; e++)
                        {
                            if (out.Planes[slot].Members[e] == v)
                            {
                                found = 1;
                            }
                        }
                        if (!found
                            && out.Planes[slot].MemberCount < MAX_POINTS)
                        {
                            out.Planes[slot].Members[
                                out.Planes[slot].MemberCount++] = v;
                        }
                    }
                }
            }
        }
    }

    /* Volume and boundary: build the 2D convex hull on each supporting
       plane, so coplanar interior points never corrupt the polygon. */
    for (q = 0; q < plane_count; q++)
    {
        BrutePlane* pl = &out.Planes[q];
        HMM_Vec3 axis;
        HMM_Vec3 u, v;
        HMM_Vec3 center = HMM_V3(0.0f, 0.0f, 0.0f);
        float proj[MAX_POINTS][2];
        int order[MAX_POINTS];
        int hull2[MAX_POINTS];
        int m, hcount;
        int a, b;
        float area = 0.0f;

        for (m = 0; m < pl->MemberCount; m++)
        {
            center = HMM_AddV3(center, pts[pl->Members[m]]);
        }
        center = HMM_MulV3F(center, 1.0f / (float) pl->MemberCount);

        /* Pick the smallest normal component as the projection axis. */
        {
            HMM_Vec3 an = HMM_V3(HMM_ABS(pl->Normal.X),
                                 HMM_ABS(pl->Normal.Y),
                                 HMM_ABS(pl->Normal.Z));
            if (an.X <= an.Y && an.X <= an.Z)
            {
                axis = HMM_V3(1.0f, 0.0f, 0.0f);
            }
            else if (an.Y <= an.Z)
            {
                axis = HMM_V3(0.0f, 1.0f, 0.0f);
            }
            else
            {
                axis = HMM_V3(0.0f, 0.0f, 1.0f);
            }
        }
        u = HMM_NormV3(HMM_Cross(axis, pl->Normal));
        v = HMM_Cross(pl->Normal, u);

        for (m = 0; m < pl->MemberCount; m++)
        {
            HMM_Vec3 rel = HMM_SubV3(pts[pl->Members[m]], center);
            proj[m][0] = HMM_DotV3(rel, u);
            proj[m][1] = HMM_DotV3(rel, v);
            order[m] = m;
        }

        /* Selection sort the members by (x, y) for monotone chaining. */
        for (a = 0; a < pl->MemberCount; a++)
        {
            int best = a;
            for (b = a + 1; b < pl->MemberCount; b++)
            {
                if (proj[order[b]][0] < proj[order[best]][0]
                    || (proj[order[b]][0] == proj[order[best]][0]
                        && proj[order[b]][1] < proj[order[best]][1]))
                {
                    best = b;
                }
            }
            { int tmp = order[a]; order[a] = order[best]; order[best] = tmp; }
        }

        /* Cross product of three projected points (indexed into members). */
        #define CROSS2(I, J, K)                                            \
            ((proj[order[J]][0] - proj[order[I]][0])                      \
                 * (proj[order[K]][1] - proj[order[I]][1])                \
             - (proj[order[J]][1] - proj[order[I]][1])                    \
                   * (proj[order[K]][0] - proj[order[I]][0]))

        /* Lower hull. */
        hcount = 0;
        for (m = 0; m < pl->MemberCount; m++)
        {
            while (hcount >= 2
                   && CROSS2(hull2[hcount - 2], hull2[hcount - 1], m) <= 0.0f)
            {
                hcount--;
            }
            hull2[hcount++] = m;
        }
        /* Upper hull. */
        {
            int lower = hcount + 1;
            for (m = pl->MemberCount - 2; m >= 0; m--)
            {
                while (hcount >= lower
                       && CROSS2(hull2[hcount - 2], hull2[hcount - 1], m)
                              <= 0.0f)
                {
                    hcount--;
                }
                hull2[hcount++] = m;
            }
            hcount--; /* the last point repeats the first */
        }
        #undef CROSS2

        /* Signed area of the convex polygon along the outward normal. */
        for (m = 0; m < hcount; m++)
        {
            HMM_Vec3 p0 = pts[pl->Members[order[hull2[m]]]];
            HMM_Vec3 p1 = pts[pl->Members[
                order[hull2[(m + 1) % hcount]]]];
            area += HMM_DotV3(pl->Normal, HMM_Cross(p0, p1)) * 0.5f;
        }
        if (area < 0.0f) area = -area;
        out.Volume += pl->D * area / 3.0f;
    }

    if (out.Volume < 0.0f) out.Volume = -out.Volume;

    /* A hull vertex lies on at least two distinct supporting planes. */
    {
        int membership[MAX_POINTS];
        int m;
        for (i = 0; i < n; i++) membership[i] = 0;
        for (q = 0; q < plane_count; q++)
        {
            for (m = 0; m < out.Planes[q].MemberCount; m++)
            {
                membership[out.Planes[q].Members[m]]++;
            }
        }
        for (i = 0; i < n; i++)
        {
            if (membership[i] >= 2)
            {
                out.VertexSet[i] = 1;
                out.VertexCount++;
            }
        }
    }
    out.PlaneCount = plane_count;
    out.TriCount = tri_count;
    return out;
}

/* ------------------------------------------------------------------ */
/* Validation of a Quickhull result against the brute-force hull.      */
/* ------------------------------------------------------------------ */

static int triangle_supporting(const HMM_Vec3* pts, int n,
                               int a, int b, int c, float eps)
{
    HMM_Vec3 raw = HMM_Cross(HMM_SubV3(pts[b], pts[a]),
                             HMM_SubV3(pts[c], pts[a]));
    float len = HMM_LenV3(raw);
    HMM_Vec3 normal;
    float max_s = -1.0e30f;
    float min_s = 1.0e30f;
    int p;

    if (len <= eps) return 0;
    normal = HMM_MulV3F(raw, 1.0f / len);
    for (p = 0; p < n; p++)
    {
        float s = HMM_DotV3(HMM_SubV3(pts[p], pts[a]), normal);
        if (s > max_s) max_s = s;
        if (s < min_s) min_s = s;
    }
    return max_s <= eps || min_s >= -eps;
}

static void validate_hull(const char* name, const HMM_Vec3* pts, int n,
                          const HMM_Hull* hull, const BruteHull* brute,
                          float eps)
{
    int f, i;
    int qh_vertices[MAX_POINTS];
    int qh_vertex_count = 0;
    int edge_face_count[MAX_POINTS * MAX_POINTS];
    int planar_regions = 0;

    for (i = 0; i < n; i++) qh_vertices[i] = 0;
    for (i = 0; i < n * n; i++) edge_face_count[i] = 0;

    CHECK(hull->Status == HMM_HULL_OK, "expected OK status");

    /* Every Quickhull triangle is a supporting plane of the point set. */
    for (f = 0; f < hull->FaceCount; f++)
    {
        int v[3];
        int s;
        HMM_HullFaceVertices(hull, f, v);
        CHECK(triangle_supporting(pts, n, v[0], v[1], v[2], eps),
              "hull face must be a supporting triangle");
        for (s = 0; s < 3; s++)
        {
            if (!qh_vertices[v[s]])
            {
                qh_vertices[v[s]] = 1;
                qh_vertex_count++;
            }
        }
        /* Undirected edge key for the manifold check. */
        for (s = 0; s < 3; s++)
        {
            int x = v[s];
            int y = v[(s + 1) % 3];
            int lo = x < y ? x : y;
            int hi = x < y ? y : x;
            edge_face_count[lo * n + hi]++;
        }
    }

    /* Same hull vertices as brute force. */
    CHECK(qh_vertex_count == brute->VertexCount,
          "vertex count must match brute force");
    for (i = 0; i < n; i++)
    {
        CHECK(qh_vertices[i] == brute->VertexSet[i],
              "vertex set must match brute force");
    }

    /* Every edge belongs to exactly two faces: a closed 2-manifold. */
    {
        int lo, hi;
        for (lo = 0; lo < n; lo++)
        {
            for (hi = lo + 1; hi < n; hi++)
            {
                int c = edge_face_count[lo * n + hi];
                CHECK(c == 0 || c == 2,
                      "every hull edge must be shared by exactly two faces");
            }
        }
    }

    /* Group Quickhull faces by supporting plane: must match brute planes. */
    {
        float plane_n[MAX_PLANES * 3];
        float plane_d[MAX_PLANES];
        for (f = 0; f < hull->FaceCount; f++)
        {
            int v[3];
            HMM_Vec3 raw, normal;
            float d, len;
            int found = 0;
            int q;
            HMM_HullFaceVertices(hull, f, v);
            raw = HMM_Cross(HMM_SubV3(pts[v[1]], pts[v[0]]),
                            HMM_SubV3(pts[v[2]], pts[v[0]]));
            len = HMM_LenV3(raw);
            normal = HMM_MulV3F(raw, 1.0f / len);
            d = HMM_DotV3(normal, pts[v[0]]);
            for (q = 0; q < planar_regions; q++)
            {
                HMM_Vec3 cand = HMM_V3(plane_n[q * 3],
                                       plane_n[q * 3 + 1],
                                       plane_n[q * 3 + 2]);
                if (HMM_LenV3(HMM_SubV3(cand, normal)) <= 1.0e-4f
                    && nearly_equal(plane_d[q], d, 1.0e-4f))
                {
                    found = 1;
                    break;
                }
            }
            if (!found && planar_regions < MAX_PLANES)
            {
                plane_n[planar_regions * 3] = normal.X;
                plane_n[planar_regions * 3 + 1] = normal.Y;
                plane_n[planar_regions * 3 + 2] = normal.Z;
                plane_d[planar_regions] = d;
                planar_regions++;
            }
        }
        CHECK(planar_regions == brute->PlaneCount,
              "planar face count must match brute force");
    }

    /* All input points are on or inside the Quickhull. */
    for (i = 0; i < n; i++)
    {
        float worst = 0.0f;
        for (f = 0; f < hull->FaceCount; f++)
        {
            int v[3];
            HMM_Vec3 a, b, c, raw, normal;
            float len, d;
            HMM_HullFaceVertices(hull, f, v);
            a = hull->Points[v[0]];
            b = hull->Points[v[1]];
            c = hull->Points[v[2]];
            raw = HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(c, a));
            len = HMM_LenV3(raw);
            normal = HMM_MulV3F(raw, 1.0f / len);
            d = HMM_DotV3(HMM_SubV3(pts[i], a), normal);
            if (d > worst) worst = d;
        }
        CHECK(worst <= eps * 10.0f + 1.0e-5f,
              "no input point may lie outside the hull");
    }

    /* Volume matches brute force. */
    {
        float qv = HMM_HullVolume(hull);
        float rel = nearly_equal(brute->Volume, 0.0f, 1.0e-12f)
            ? qv : (qv - brute->Volume) / brute->Volume;
        if (rel < 0.0f) rel = -rel;
        CHECK(rel <= 2.0e-4f, "volume must match brute force");
    }

    (void)name;
}

/* ------------------------------------------------------------------ */
/* Point cloud construction.                                           */
/* ------------------------------------------------------------------ */

static HMM_Vec3 fibonacci_sphere(int n, int i)
{
    static const float golden = 2.399963229728653f; /* pi * (3 - sqrt(5)) */
    float y = 1.0f - (2.0f * (float) i) / ((float) n - 1.0f);
    float radius = sqrtf(HMM_MAX(0.0f, 1.0f - y * y));
    float theta = golden * (float) i;
    HMM_Vec3 p = HMM_V3(radius * cosf(theta), y, radius * sinf(theta));
    return HMM_NormV3(p); /* guarantee unit length despite float rounding */
}

/* ------------------------------------------------------------------ */
/* Timing helper.                                                      */
/* ------------------------------------------------------------------ */

static double seconds_since(clock_t start)
{
    return (double) (clock() - start) / (double) CLOCKS_PER_SEC;
}

/* ------------------------------------------------------------------ */
/* Per-cloud reporting.                                                */
/* ------------------------------------------------------------------ */

typedef struct CloudReport
{
    const char* Name;
    int InputCount;
    int UniqueCount;
    int QhVertices;
    int QhFaces;
    int BruteVertices;
    int BrutePlanes;
    float QhVolume;
    float BruteVolume;
    float ExactVolume;
    float RelErrorExact;
    float RelErrorBrute;
    double QhMillis;
    double BruteMillis;
} CloudReport;

static CloudReport run_cloud(const char* name, const HMM_Vec3* pts, int n,
                             float exact_volume, int repeats)
{
    CloudReport rep;
    HMM_HullOptions opts = HMM_DefaultHullOptions();
    HMM_Hull hull;
    BruteHull brute;
    clock_t t0;
    int r;
    float diag;
    HMM_Vec3 mn = pts[0], mx = pts[0];
    int i;

    for (i = 1; i < n; i++)
    {
        if (pts[i].X < mn.X) mn.X = pts[i].X;
        if (pts[i].Y < mn.Y) mn.Y = pts[i].Y;
        if (pts[i].Z < mn.Z) mn.Z = pts[i].Z;
        if (pts[i].X > mx.X) mx.X = pts[i].X;
        if (pts[i].Y > mx.Y) mx.Y = pts[i].Y;
        if (pts[i].Z > mx.Z) mx.Z = pts[i].Z;
    }
    diag = HMM_LenV3(HMM_SubV3(mx, mn));

    t0 = clock();
    for (r = 0; r < repeats; r++)
    {
        hull = HMM_QuickHull3(pts, n, opts);
        if (r + 1 < repeats) HMM_FreeHull(&hull);
    }
    rep.QhMillis = seconds_since(t0) * 1000.0 / (double) repeats;

    t0 = clock();
    for (r = 0; r < 3; r++)
    {
        /* Brute force over the same compacted (deduplicated) points the hull
           indices reference, so the two index spaces match exactly. */
        brute = brute_force(hull.Points, hull.PointCount,
                            diag * 1.0e-5f);
    }
    rep.BruteMillis = seconds_since(t0) * 1000.0 / 3.0;

    validate_hull(name, hull.Points, hull.PointCount, &hull, &brute,
                  diag * 1.0e-5f);

    rep.Name = name;
    rep.InputCount = n;
    rep.UniqueCount = hull.PointCount;
    rep.QhVertices = HMM_HullVertexCount(&hull);
    rep.QhFaces = HMM_HullFaceCount(&hull);
    rep.BruteVertices = brute.VertexCount;
    rep.BrutePlanes = brute.PlaneCount;
    rep.QhVolume = HMM_HullVolume(&hull);
    rep.BruteVolume = brute.Volume;
    rep.ExactVolume = exact_volume;
    rep.RelErrorExact = exact_volume > 0.0f
        ? (rep.QhVolume - exact_volume) / exact_volume : 0.0f;
    if (rep.RelErrorExact < 0.0f) rep.RelErrorExact = -rep.RelErrorExact;
    rep.RelErrorBrute = brute.Volume > 0.0f
        ? (rep.QhVolume - brute.Volume) / brute.Volume : 0.0f;
    if (rep.RelErrorBrute < 0.0f) rep.RelErrorBrute = -rep.RelErrorBrute;

    HMM_FreeHull(&hull);
    return rep;
}

static void print_report_row(const CloudReport* r)
{
    printf("  %-18s in=%-3d uniq=%-3d | QH V=%-3d F=%-3d vol=%.6f | "
           "brute V=%-3d planes=%-3d vol=%.6f | errExact=%8.3f%% "
           "errBrute=%9.2e | qh=%8.3fms brute=%8.3fms\n",
           r->Name, r->InputCount, r->UniqueCount,
           r->QhVertices, r->QhFaces, r->QhVolume,
           r->BruteVertices, r->BrutePlanes, r->BruteVolume,
           r->RelErrorExact * 100.0f, r->RelErrorBrute,
           r->QhMillis, r->BruteMillis);
}

/* ------------------------------------------------------------------ */
/* Main.                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    HMM_Vec3 sphere[30];
    HMM_Vec3 cube_cloud[15];   /* 8 corners + 6 face centers + 1 center */
    HMM_Vec3 duplicate_cloud[19];
    HMM_Vec3 coplanar_cloud[7];
    HMM_Vec3 collinear[5];
    HMM_Vec3 three_points[3];
    HMM_Vec3 one_point[1];
    HMM_Vec3 dup_too_few[4];
    CloudReport reports[3];
    HMM_Hull hull;
    HMM_HullOptions opts;
    int i, c;
    int total_coplanar;

    printf("=== HandmadeMath 3D Quickhull demo ===\n\n");

    /* Cloud 1: uniform points on the unit sphere (Fibonacci lattice). */
    for (i = 0; i < 30; i++)
    {
        sphere[i] = fibonacci_sphere(30, i);
    }

    /* Cloud 2: the eight cube corners plus coplanar face centers. */
    c = 0;
    {
        int x, y, z;
        for (x = 0; x < 2; x++)
            for (y = 0; y < 2; y++)
                for (z = 0; z < 2; z++)
                    cube_cloud[c++] = HMM_V3((float) x - 0.5f,
                                             (float) y - 0.5f,
                                             (float) z - 0.5f);
    }
    cube_cloud[c++] = HMM_V3(0.0f, 0.0f, 0.5f);
    cube_cloud[c++] = HMM_V3(0.0f, 0.0f, -0.5f);
    cube_cloud[c++] = HMM_V3(0.5f, 0.0f, 0.0f);
    cube_cloud[c++] = HMM_V3(-0.5f, 0.0f, 0.0f);
    cube_cloud[c++] = HMM_V3(0.0f, 0.5f, 0.0f);
    cube_cloud[c++] = HMM_V3(0.0f, -0.5f, 0.0f);
    cube_cloud[c++] = HMM_V3(0.0f, 0.0f, 0.0f); /* interior */

    /* Cloud 4: cube corners duplicated, plus a repeated interior point. */
    c = 0;
    for (i = 0; i < 8; i++)
    {
        duplicate_cloud[c++] = cube_cloud[i];
        duplicate_cloud[c++] = cube_cloud[i];
    }
    duplicate_cloud[c++] = HMM_V3(0.0f, 0.0f, 0.0f);
    duplicate_cloud[c++] = HMM_V3(0.0f, 0.0f, 0.0f);
    duplicate_cloud[c++] = HMM_V3(0.0f, 0.0f, 0.0f);

    printf("Clouds (volume, counts, brute-force comparison, timing):\n");
    reports[0] = run_cloud("unit sphere n=30", sphere, 30,
                           4.0f * HMM_PI32 / 3.0f, 300);
    reports[1] = run_cloud("cube+face centers", cube_cloud, 15, 1.0f, 300);
    reports[2] = run_cloud("duplicated cube", duplicate_cloud, 19, 1.0f, 300);
    for (i = 0; i < 3; i++) print_report_row(&reports[i]);

    /* The inscribed sphere polyhedron must be close to the sphere volume. */
    CHECK(reports[0].RelErrorExact < 0.25f,
          "inscribed sphere hull volume within 25% of the sphere");
    CHECK(reports[1].QhVertices == 8 && reports[1].QhFaces == 12,
          "cube hull has 8 vertices and 12 triangular faces");
    CHECK(reports[2].UniqueCount == 9 && reports[2].QhVertices == 8,
          "duplicates are removed before hull construction");

    printf("\nDegenerate inputs (must report status, never crash):\n");

    /* Cloud 3: all points coplanar. */
    coplanar_cloud[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    coplanar_cloud[1] = HMM_V3(1.0f, 0.0f, 0.0f);
    coplanar_cloud[2] = HMM_V3(1.0f, 1.0f, 0.0f);
    coplanar_cloud[3] = HMM_V3(0.0f, 1.0f, 0.0f);
    coplanar_cloud[4] = HMM_V3(0.5f, 0.5f, 0.0f);
    coplanar_cloud[5] = HMM_V3(0.25f, 0.75f, 0.0f);
    coplanar_cloud[6] = HMM_V3(0.75f, 0.25f, 0.0f);
    hull = HMM_QuickHull3(coplanar_cloud, 7, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_ALL_COPLANAR, "coplanar cloud detected");
    CHECK(HMM_HullFaceCount(&hull) == 0 && HMM_HullVertexCount(&hull) == 0,
          "coplanar cloud produces no hull");
    CHECK(HMM_HullVolume(&hull) == 0.0f, "coplanar cloud has zero volume");
    printf("  coplanar square+interior (7 pts): status=%d "
           "HMM_HULL_ALL_COPLANAR, faces=0, volume=0\n", (int) hull.Status);
    HMM_FreeHull(&hull);

    for (i = 0; i < 5; i++)
    {
        collinear[i] = HMM_V3((float) i, 0.0f, 0.0f);
    }
    hull = HMM_QuickHull3(collinear, 5, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_ALL_COPLANAR, "collinear cloud detected");
    printf("  collinear points (5 pts):          status=%d "
           "HMM_HULL_ALL_COPLANAR\n", (int) hull.Status);
    HMM_FreeHull(&hull);

    three_points[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    three_points[1] = HMM_V3(1.0f, 0.0f, 0.0f);
    three_points[2] = HMM_V3(0.0f, 1.0f, 0.0f);
    hull = HMM_QuickHull3(three_points, 3, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_TOO_FEW_POINTS, "three points rejected");
    printf("  triangle (3 pts):                  status=%d "
           "HMM_HULL_TOO_FEW_POINTS\n", (int) hull.Status);
    HMM_FreeHull(&hull);

    one_point[0] = HMM_V3(1.0f, 2.0f, 3.0f);
    hull = HMM_QuickHull3(one_point, 1, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_TOO_FEW_POINTS, "one point rejected");
    HMM_FreeHull(&hull);

    hull = HMM_QuickHull3(0, 0, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_TOO_FEW_POINTS, "null/empty input rejected");
    printf("  null / zero-count input:           status=%d "
           "HMM_HULL_TOO_FEW_POINTS\n", (int) hull.Status);
    HMM_FreeHull(&hull);

    dup_too_few[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    dup_too_few[1] = HMM_V3(0.0f, 0.0f, 0.0f);
    dup_too_few[2] = HMM_V3(1.0f, 0.0f, 0.0f);
    dup_too_few[3] = HMM_V3(0.0f, 1.0f, 0.0f);
    hull = HMM_QuickHull3(dup_too_few, 4, HMM_DefaultHullOptions());
    CHECK(hull.Status == HMM_HULL_TOO_FEW_POINTS,
          "four inputs with one duplicate rejected");
    printf("  4 pts with a duplicate (3 unique): status=%d "
           "HMM_HULL_TOO_FEW_POINTS\n", (int) hull.Status);
    HMM_FreeHull(&hull);

    printf("\nConfigurable tolerance and coplanar-point retention:\n");

    /* KeepCoplanarPoints = 1: the six face centers are reported per face. */
    opts = HMM_DefaultHullOptions();
    opts.KeepCoplanarPoints = 1;
    hull = HMM_QuickHull3(cube_cloud, 15, opts);
    CHECK(hull.Status == HMM_HULL_OK, "coplanar-retention run succeeds");
    total_coplanar = 0;
    for (i = 0; i < hull.FaceCount; i++)
    {
        total_coplanar += HMM_HullCoplanarCount(&hull, i);
    }
    CHECK(total_coplanar == 6,
          "six face centers are merged onto the hull faces");
    CHECK(HMM_HullVertexCount(&hull) == 8,
          "coplanar points are never counted as vertices");
    printf("  KeepCoplanarPoints=1: %d coplanar points attached to faces, "
           "vertices still %d\n", total_coplanar,
           HMM_HullVertexCount(&hull));
    HMM_FreeHull(&hull);

    /* KeepCoplanarPoints = 0: the same hull, but no coplanar data kept. */
    opts.KeepCoplanarPoints = 0;
    hull = HMM_QuickHull3(cube_cloud, 15, opts);
    total_coplanar = 0;
    for (i = 0; i < hull.FaceCount; i++)
    {
        total_coplanar += HMM_HullCoplanarCount(&hull, i);
    }
    CHECK(total_coplanar == 0, "coplanar points can be discarded");
    CHECK(HMM_HullFaceCount(&hull) == 12
          && HMM_HullVertexCount(&hull) == 8,
          "discarding coplanar points leaves the hull unchanged");
    printf("  KeepCoplanarPoints=0: %d coplanar points retained, "
           "faces=%d vertices=%d\n", total_coplanar,
           HMM_HullFaceCount(&hull), HMM_HullVertexCount(&hull));
    HMM_FreeHull(&hull);

    /* A custom explicit epsilon is accepted and keeps an exact cube exact. */
    opts = HMM_DefaultHullOptions();
    opts.Epsilon = 1.0e-6f;
    hull = HMM_QuickHull3(cube_cloud, 15, opts);
    CHECK(hull.Status == HMM_HULL_OK && hull.FaceCount == 12
          && HMM_HullVertexCount(&hull) == 8,
          "custom epsilon produces the same cube hull");
    printf("  Epsilon=1e-6 (custom): faces=%d vertices=%d volume=%.6f\n",
           hull.FaceCount, HMM_HullVertexCount(&hull),
           HMM_HullVolume(&hull));
    HMM_FreeHull(&hull);

    printf("\nRandomized fuzz: Quickhull vs brute force on 25 random clouds\n");
    {
        unsigned int seed = 12345u;
        int trial;
        for (trial = 0; trial < 25; trial++)
        {
            HMM_Vec3 rand_pts[24];
            int n = 8 + (trial % 17); /* 8..24 points */
            int q;
            BruteHull brute;
            for (q = 0; q < n; q++)
            {
                seed = seed * 1103515245u + 12345u;
                float rx = (float) (seed >> 8) / 16777216.0f;
                seed = seed * 1103515245u + 12345u;
                float ry = (float) (seed >> 8) / 16777216.0f;
                seed = seed * 1103515245u + 12345u;
                float rz = (float) (seed >> 8) / 16777216.0f;
                rand_pts[q] = HMM_V3(rx, ry, rz);
            }
            hull = HMM_QuickHull3(rand_pts, n, HMM_DefaultHullOptions());
            brute = brute_force(rand_pts, n, hull.Epsilon);
            if (brute.Volume > 0.0f)
            {
                validate_hull("fuzz", rand_pts, n, &hull, &brute,
                              hull.Epsilon);
            }
            else
            {
                CHECK(hull.Status == HMM_HULL_ALL_COPLANAR
                      || hull.Status == HMM_HULL_TOO_FEW_POINTS,
                      "degenerate random cloud reports a status");
            }
            HMM_FreeHull(&hull);
        }
        printf("  25 trials checked against brute force\n");
    }

    printf("\n=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
