/*
 * bvh_bench.c — BVH over tens of thousands of AABBs: SAH build, batch ray
 * traversal, benchmark against brute-force per-box intersection.
 *
 * Self-contained C99, no third-party deps. Build (from repo root):
 *   gcc -std=c99 -O2 -Wall -Wextra -o test/bvh_bench test/bvh_bench.c -lm
 *
 * The per-box intersection routine ray_aabb() below is THE original
 * one-at-a-time intersect function: it is used verbatim by both the
 * brute-force baseline and the BVH leaf evaluation, and the program
 * verifies that BVH traversal results are bit-identical to brute force.
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

typedef struct { float bmin[3], bmax[3]; } AABB;
typedef struct { float o[3], d[3]; }     Ray;
typedef struct { float t; int idx; }     Hit;   /* idx = -1 -> miss */

/* ------------------------------------------------------------------ */
/* The original per-box intersection function (unchanged, slab test).  */
/* Brute force AND BVH leaves both call exactly this function.         */
/* ------------------------------------------------------------------ */

static int ray_aabb(const Ray *r, const AABB *b, float *t_out)
{
    float tmin = 0.0f, tmax = FLT_MAX;
    int i;
    for (i = 0; i < 3; i++) {
        float inv = 1.0f / r->d[i];
        float t0 = (b->bmin[i] - r->o[i]) * inv;
        float t1 = (b->bmax[i] - r->o[i]) * inv;
        float tmp;
        if (t0 > t1) { tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return 0;
    }
    *t_out = tmin;
    return 1;
}

/* Node-culling variant: same slab math, but only a boolean against an
 * interval [0, tlimit] is needed for interior BVH nodes. */
static int ray_aabb_cull(const Ray *r, const AABB *b, float tlimit)
{
    float tmin = 0.0f, tmax = tlimit;
    int i;
    for (i = 0; i < 3; i++) {
        float inv = 1.0f / r->d[i];
        float t0 = (b->bmin[i] - r->o[i]) * inv;
        float t1 = (b->bmax[i] - r->o[i]) * inv;
        float tmp;
        if (t0 > t1) { tmp = t0; t0 = t1; t1 = tmp; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static unsigned long long g_rng = 0x9E3779B97F4A7C15ull;

static unsigned rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (unsigned)(g_rng >> 32);
}

static float frand(void) { return (rng_next() >> 8) * (1.0f / 16777216.0f); }
static float frange(float a, float b) { return a + (b - a) * frand(); }

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void aabb_grow(AABB *a, const AABB *b)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (b->bmin[i] < a->bmin[i]) a->bmin[i] = b->bmin[i];
        if (b->bmax[i] > a->bmax[i]) a->bmax[i] = b->bmax[i];
    }
}

static AABB aabb_empty(void)
{
    AABB a;
    int i;
    for (i = 0; i < 3; i++) { a.bmin[i] = FLT_MAX; a.bmax[i] = -FLT_MAX; }
    return a;
}

static float aabb_surface(const AABB *a)
{
    float dx = a->bmax[0] - a->bmin[0];
    float dy = a->bmax[1] - a->bmin[1];
    float dz = a->bmax[2] - a->bmin[2];
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

/* ------------------------------------------------------------------ */
/* BVH                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    AABB bounds;
    int left, right;   /* children for inner nodes, -1 for leaves */
    int start, count;  /* leaf: range in order[]; inner: count == 0 */
} Node;

typedef struct {
    Node *nodes;
    int nnodes;
    int *order;          /* primitive indices; leaves reference ranges */
    const AABB *boxes;
} BVH;

typedef struct {
    const AABB *boxes;
    float (*cent)[3];    /* centroids */
    int *order;
    int *tmp;            /* partition scratch, size n */
    Node *nodes;
    int nnodes, ncap;
    int leaf_cap;        /* configurable leaf capacity */
    int use_sah;         /* 1 = SAH split, 0 = median split */
} Builder;

#define SAH_BINS 16

typedef struct { int count; AABB b; } Bin;

static int alloc_node(Builder *bd)
{
    if (bd->nnodes == bd->ncap) {
        bd->ncap = bd->ncap ? bd->ncap * 2 : 1024;
        bd->nodes = (Node *)realloc(bd->nodes, (size_t)bd->ncap * sizeof(Node));
        if (!bd->nodes) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    return bd->nnodes++;
}

/* qsort context for median split (single-threaded benchmark). */
static const float (*g_qcent)[3];
static int g_qaxis;

static int cmp_cent(const void *pa, const void *pb)
{
    float ca = g_qcent[*(const int *)pa][g_qaxis];
    float cb = g_qcent[*(const int *)pb][g_qaxis];
    return (ca > cb) - (ca < cb);
}

static void median_split(Builder *bd, int start, int end, int *mid_out)
{
    float cmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
    float cmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    int i, k, axis = 0;
    for (i = start; i < end; i++) {
        const float *c = bd->cent[bd->order[i]];
        for (k = 0; k < 3; k++) {
            if (c[k] < cmin[k]) cmin[k] = c[k];
            if (c[k] > cmax[k]) cmax[k] = c[k];
        }
    }
    for (k = 1; k < 3; k++)
        if (cmax[k] - cmin[k] > cmax[axis] - cmin[axis]) axis = k;
    g_qcent = bd->cent;
    g_qaxis = axis;
    qsort(bd->order + start, (size_t)(end - start), sizeof(int), cmp_cent);
    *mid_out = start + (end - start) / 2;
}

/* Binned SAH: pick axis + split plane minimizing SA(left)*nL + SA(right)*nR.
 * Returns 1 and fills axis/splitbin if a split beats making a leaf. */
static int sah_choose(Builder *bd, int start, int end, const AABB *bounds,
                      int *axis_out, int *bin_out)
{
    int count = end - start;
    float leaf_cost = aabb_surface(bounds) * (float)count;
    float best = FLT_MAX;
    int best_axis = -1, best_bin = -1;
    int axis, i, k;

    for (axis = 0; axis < 3; axis++) {
        float cmin = FLT_MAX, cmax = -FLT_MAX, scale;
        Bin bins[SAH_BINS];
        int rcount[SAH_BINS];
        AABB rb[SAH_BINS];
        int lcount = 0;
        AABB lb = aabb_empty();

        for (i = start; i < end; i++) {
            float c = bd->cent[bd->order[i]][axis];
            if (c < cmin) cmin = c;
            if (c > cmax) cmax = c;
        }
        if (cmax - cmin < 1e-20f) continue;
        scale = (float)SAH_BINS / (cmax - cmin);

        for (k = 0; k < SAH_BINS; k++) { bins[k].count = 0; bins[k].b = aabb_empty(); }
        for (i = start; i < end; i++) {
            int pi = bd->order[i];
            int b = (int)((bd->cent[pi][axis] - cmin) * scale);
            if (b < 0) b = 0;
            if (b > SAH_BINS - 1) b = SAH_BINS - 1;
            bins[b].count++;
            aabb_grow(&bins[b].b, &bd->boxes[pi]);
        }

        rcount[SAH_BINS - 1] = bins[SAH_BINS - 1].count;
        rb[SAH_BINS - 1] = bins[SAH_BINS - 1].b;
        for (k = SAH_BINS - 2; k >= 0; k--) {
            rcount[k] = rcount[k + 1] + bins[k].count;
            rb[k] = rb[k + 1];
            aabb_grow(&rb[k], &bins[k].b);
        }

        for (k = 0; k < SAH_BINS - 1; k++) {
            float cost;
            lcount += bins[k].count;
            aabb_grow(&lb, &bins[k].b);
            if (lcount == 0 || rcount[k + 1] == 0) continue;
            cost = aabb_surface(&lb) * (float)lcount
                 + aabb_surface(&rb[k + 1]) * (float)rcount[k + 1];
            if (cost < best) { best = cost; best_axis = axis; best_bin = k; }
        }
    }

    if (best_axis < 0 || best >= leaf_cost) return 0;
    *axis_out = best_axis;
    *bin_out = best_bin;
    return 1;
}

/* Partition order[start,end) by SAH bin assignment; returns split index. */
static int sah_partition(Builder *bd, int start, int end, int axis, int splitbin)
{
    float cmin = FLT_MAX, cmax = -FLT_MAX, scale;
    int i, l = start, r = end - 1;

    for (i = start; i < end; i++) {
        float c = bd->cent[bd->order[i]][axis];
        if (c < cmin) cmin = c;
        if (c > cmax) cmax = c;
    }
    scale = (float)SAH_BINS / (cmax - cmin);

    for (i = start; i < end; i++) {
        int pi = bd->order[i];
        int b = (int)((bd->cent[pi][axis] - cmin) * scale);
        if (b < 0) b = 0;
        if (b > SAH_BINS - 1) b = SAH_BINS - 1;
        if (b <= splitbin) bd->tmp[l++] = pi;
        else               bd->tmp[r--] = pi;
    }
    memcpy(bd->order + start, bd->tmp + start, (size_t)(end - start) * sizeof(int));
    return l;
}

static int build_node(Builder *bd, int start, int end)
{
    int idx = alloc_node(bd);
    int count = end - start;
    int i, mid = -1;
    AABB bounds = aabb_empty();

    for (i = start; i < end; i++)
        aabb_grow(&bounds, &bd->boxes[bd->order[i]]);

    bd->nodes[idx].bounds = bounds;
    bd->nodes[idx].left = -1;
    bd->nodes[idx].right = -1;
    bd->nodes[idx].start = start;
    bd->nodes[idx].count = count;

    if (count <= bd->leaf_cap)
        return idx;

    if (bd->use_sah) {
        int axis, splitbin;
        if (sah_choose(bd, start, end, &bounds, &axis, &splitbin)) {
            mid = sah_partition(bd, start, end, axis, splitbin);
            if (mid == start || mid == end) mid = -1; /* degenerate: fall through */
        }
    }

    if (mid < 0)
        median_split(bd, start, end, &mid);

    {
        int left = build_node(bd, start, mid);
        int right = build_node(bd, mid, end);
        bd->nodes[idx].left = left;
        bd->nodes[idx].right = right;
        bd->nodes[idx].count = 0; /* mark inner */
    }
    return idx;
}

static void bvh_build(BVH *bvh, const AABB *boxes, int n, int leaf_cap, int use_sah)
{
    Builder bd;
    int i;

    memset(bvh, 0, sizeof(*bvh));
    bvh->boxes = boxes;
    if (n <= 0) return; /* empty tree: nnodes = 0, order = NULL */

    bd.boxes = boxes;
    bd.cent = (float (*)[3])malloc((size_t)n * sizeof(*bd.cent));
    bd.order = (int *)malloc((size_t)n * sizeof(int));
    bd.tmp = (int *)malloc((size_t)n * sizeof(int));
    bd.nodes = NULL;
    bd.nnodes = 0;
    bd.ncap = 0;
    bd.leaf_cap = leaf_cap > 0 ? leaf_cap : 1;
    bd.use_sah = use_sah;
    if (!bd.cent || !bd.order || !bd.tmp) { fprintf(stderr, "out of memory\n"); exit(1); }

    for (i = 0; i < n; i++) {
        int k;
        bd.order[i] = i;
        for (k = 0; k < 3; k++)
            bd.cent[i][k] = 0.5f * (boxes[i].bmin[k] + boxes[i].bmax[k]);
    }

    build_node(&bd, 0, n);

    bvh->nodes = bd.nodes;
    bvh->nnodes = bd.nnodes;
    bvh->order = bd.order;
    free(bd.cent);
    free(bd.tmp);
}

static void bvh_free(BVH *bvh)
{
    free(bvh->nodes);
    free(bvh->order);
    bvh->nodes = NULL;
    bvh->order = NULL;
    bvh->nnodes = 0;
}

/* ------------------------------------------------------------------ */
/* Traversal                                                           */
/* ------------------------------------------------------------------ */

static Hit bvh_nearest(const BVH *bvh, const Ray *r, long *visited)
{
    Hit h;
    int stack[128];
    int sp = 0;

    h.t = FLT_MAX;
    h.idx = -1;
    if (bvh->nnodes == 0) return h; /* empty tree */

    stack[sp++] = 0;
    while (sp > 0) {
        const Node *n = &bvh->nodes[stack[--sp]];
        (*visited)++;
        if (!ray_aabb_cull(r, &n->bounds, h.t)) continue;
        if (n->count > 0) { /* leaf: original per-box function, verbatim */
            int i;
            for (i = 0; i < n->count; i++) {
                int pi = bvh->order[n->start + i];
                float t;
                if (ray_aabb(r, &bvh->boxes[pi], &t) && t < h.t) {
                    h.t = t;
                    h.idx = pi;
                }
            }
        } else {
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }
    return h;
}

/* Batch traversal: one call for a whole ray buffer. */
static void bvh_intersect_batch(const BVH *bvh, const Ray *rays, Hit *out,
                                int nrays, long *total_visited)
{
    int i;
    long visited = 0;
    for (i = 0; i < nrays; i++)
        out[i] = bvh_nearest(bvh, &rays[i], &visited);
    if (total_visited) *total_visited = visited;
}

static Hit brute_nearest(const AABB *boxes, int n, const Ray *r)
{
    Hit h;
    int i;
    h.t = FLT_MAX;
    h.idx = -1;
    for (i = 0; i < n; i++) {
        float t;
        if (ray_aabb(r, &boxes[i], &t) && t < h.t) { h.t = t; h.idx = i; }
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Scenes                                                              */
/* ------------------------------------------------------------------ */

#define N_BOXES 30000
#define N_RAYS  10000

typedef void (*scene_fn)(AABB *, int);

static void gen_uniform(AABB *b, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        float c[3], half = frange(0.1f, 1.0f);
        for (k = 0; k < 3; k++) c[k] = frange(0.0f, 100.0f);
        for (k = 0; k < 3; k++) { b[i].bmin[k] = c[k] - half; b[i].bmax[k] = c[k] + half; }
    }
}

static void gen_clustered(AABB *b, int n)
{
    float centers[16][3];
    int i, j, k;
    for (j = 0; j < 16; j++)
        for (k = 0; k < 3; k++) centers[j][k] = frange(0.0f, 100.0f);
    for (i = 0; i < n; i++) {
        const float *cc = centers[rng_next() % 16];
        float half = frange(0.05f, 0.5f);
        for (k = 0; k < 3; k++) {
            /* approx-gaussian offset, spread ~4 */
            float off = (frand() + frand() + frand() - 1.5f) * 4.0f;
            b[i].bmin[k] = cc[k] + off - half;
            b[i].bmax[k] = cc[k] + off + half;
        }
    }
}

static void gen_elongated(AABB *b, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        float c[3], half[3];
        int axis = (int)(rng_next() % 3);
        for (k = 0; k < 3; k++) { c[k] = frange(0.0f, 100.0f); half[k] = frange(0.05f, 0.2f); }
        half[axis] = frange(5.0f, 25.0f); /* long thin boxes */
        for (k = 0; k < 3; k++) { b[i].bmin[k] = c[k] - half[k]; b[i].bmax[k] = c[k] + half[k]; }
    }
}

static void gen_overlap(AABB *b, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        float half = frange(2.0f, 8.0f);
        for (k = 0; k < 3; k++) {
            float c = 50.0f + (frand() + frand() + frand() - 1.5f) * 10.0f;
            b[i].bmin[k] = c - half;
            b[i].bmax[k] = c + half;
        }
    }
}

static void gen_rays(Ray *rays, int nrays, const AABB *boxes, int nboxes)
{
    int i, k;
    for (i = 0; i < nrays; i++) {
        /* aim each ray at a random box from a random direction */
        const AABB *t = &boxes[rng_next() % (unsigned)nboxes];
        float z = frange(-1.0f, 1.0f);
        float phi = frange(0.0f, 6.2831853f);
        float rr = sqrtf(1.0f - z * z);
        float dir[3], dist = frange(30.0f, 120.0f), len;
        dir[0] = rr * cosf(phi);
        dir[1] = rr * sinf(phi);
        dir[2] = z;
        len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (k = 0; k < 3; k++) {
            float tc = 0.5f * (t->bmin[k] + t->bmax[k]);
            rays[i].o[k] = tc + dir[k] * dist;
            rays[i].d[k] = -dir[k] / len;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Benchmark driver                                                    */
/* ------------------------------------------------------------------ */

static int g_failures = 0;

static void check(int cond, const char *msg)
{
    if (cond) {
        printf("  [PASS] %s\n", msg);
    } else {
        printf("  [FAIL] %s\n", msg);
        g_failures++;
    }
}

/* Verify BVH hits are bit-identical to brute force. Returns mismatch count. */
static int verify_hits(const Hit *ref, const Hit *got, int n, int *ties)
{
    int bad = 0, i;
    *ties = 0;
    for (i = 0; i < n; i++) {
        if (memcmp(&ref[i].t, &got[i].t, sizeof(float)) != 0) {
            bad++;
        } else if (ref[i].idx != got[i].idx) {
            (*ties)++; /* same t, different box: exact tie */
        }
    }
    return bad;
}

static void edge_case_tests(void)
{
    BVH bvh;
    Ray ray;
    Hit hb, hh;
    long visited;
    AABB one;
    Ray miss_ray;

    printf("== Edge cases ==\n");

    /* empty tree */
    bvh_build(&bvh, NULL, 0, 4, 1);
    check(bvh.nnodes == 0, "empty tree builds with 0 nodes");
    ray.o[0] = 0.0f; ray.o[1] = 0.0f; ray.o[2] = 0.0f;
    ray.d[0] = 1.0f; ray.d[1] = 0.0f; ray.d[2] = 0.0f;
    visited = 0;
    hh = bvh_nearest(&bvh, &ray, &visited);
    check(hh.idx == -1 && visited == 0, "empty tree traversal: miss, 0 nodes visited");
    bvh_free(&bvh);

    /* single element */
    one.bmin[0] = -1.0f; one.bmin[1] = -1.0f; one.bmin[2] = -1.0f;
    one.bmax[0] =  1.0f; one.bmax[1] =  1.0f; one.bmax[2] =  1.0f;
    bvh_build(&bvh, &one, 1, 4, 1);
    check(bvh.nnodes == 1, "single-element tree is exactly one leaf node");
    ray.o[0] = -5.0f; ray.o[1] = 0.0f; ray.o[2] = 0.0f;
    ray.d[0] =  1.0f; ray.d[1] = 0.0f; ray.d[2] = 0.0f;
    hb = brute_nearest(&one, 1, &ray);
    visited = 0;
    hh = bvh_nearest(&bvh, &ray, &visited);
    check(hh.idx == hb.idx && memcmp(&hh.t, &hb.t, sizeof(float)) == 0,
          "single-element hit matches brute force bit-for-bit");
    miss_ray.o[0] = -5.0f; miss_ray.o[1] = 5.0f; miss_ray.o[2] = 0.0f;
    miss_ray.d[0] =  1.0f; miss_ray.d[1] = 0.0f; miss_ray.d[2] = 0.0f;
    visited = 0;
    hh = bvh_nearest(&bvh, &miss_ray, &visited);
    check(hh.idx == -1, "single-element miss ray returns miss");
    bvh_free(&bvh);
    printf("\n");
}

int main(void)
{
    static const struct { const char *name; scene_fn gen; } scenes[] = {
        { "uniform",   gen_uniform   },
        { "clustered", gen_clustered },
        { "elongated", gen_elongated },
        { "overlap",   gen_overlap   },
    };
    AABB *boxes = (AABB *)malloc((size_t)N_BOXES * sizeof(AABB));
    Ray *rays = (Ray *)malloc((size_t)N_RAYS * sizeof(Ray));
    Hit *href = (Hit *)malloc((size_t)N_RAYS * sizeof(Hit));
    Hit *hout = (Hit *)malloc((size_t)N_RAYS * sizeof(Hit));
    int s, i;

    if (!boxes || !rays || !href || !hout) { fprintf(stderr, "out of memory\n"); return 1; }

    edge_case_tests();

    printf("== SAH vs median split, %d boxes, %d rays, leaf capacity 4 ==\n", N_BOXES, N_RAYS);
    printf("%-10s | %-6s | %7s | %9s | %10s | %12s | %9s | %8s\n",
           "scene", "split", "nodes", "build ms", "avg visited", "brute ms", "bvh ms", "speedup");
    printf("-----------+--------+---------+-----------+------------+--------------+----------+---------\n");

    for (s = 0; s < 4; s++) {
        BVH bvh_sah, bvh_med;
        double t0, t_build_sah, t_build_med, t_brute, t_sah, t_med;
        long vis_sah = 0, vis_med = 0;
        int ties = 0, bad;

        scenes[s].gen(boxes, N_BOXES);
        gen_rays(rays, N_RAYS, boxes, N_BOXES);

        t0 = now_ms();
        bvh_build(&bvh_sah, boxes, N_BOXES, 4, 1);
        t_build_sah = now_ms() - t0;

        t0 = now_ms();
        bvh_build(&bvh_med, boxes, N_BOXES, 4, 0);
        t_build_med = now_ms() - t0;

        t0 = now_ms();
        for (i = 0; i < N_RAYS; i++) href[i] = brute_nearest(boxes, N_BOXES, &rays[i]);
        t_brute = now_ms() - t0;

        t0 = now_ms();
        bvh_intersect_batch(&bvh_sah, rays, hout, N_RAYS, &vis_sah);
        t_sah = now_ms() - t0;
        bad = verify_hits(href, hout, N_RAYS, &ties);
        if (bad != 0) {
            printf("  [FAIL] %s/SAH: %d rays differ from brute force\n", scenes[s].name, bad);
            g_failures++;
        }

        t0 = now_ms();
        bvh_intersect_batch(&bvh_med, rays, hout, N_RAYS, &vis_med);
        t_med = now_ms() - t0;
        bad = verify_hits(href, hout, N_RAYS, &ties);
        if (bad != 0) {
            printf("  [FAIL] %s/median: %d rays differ from brute force\n", scenes[s].name, bad);
            g_failures++;
        }

        printf("%-10s | %-6s | %7d | %9.2f | %10.1f | %12.2f | %8.2f | %7.1fx\n",
               scenes[s].name, "SAH", bvh_sah.nnodes, t_build_sah,
               (double)vis_sah / N_RAYS, t_brute, t_sah, t_brute / t_sah);
        printf("%-10s | %-6s | %7d | %9.2f | %10.1f | %12.2f | %8.2f | %7.1fx\n",
               "", "median", bvh_med.nnodes, t_build_med,
               (double)vis_med / N_RAYS, t_brute, t_med, t_brute / t_med);

        bvh_free(&bvh_sah);
        bvh_free(&bvh_med);
    }

    /* Leaf-capacity sweep on the uniform scene (capacity is configurable). */
    printf("\n== Leaf capacity sweep (uniform scene, SAH) ==\n");
    printf("%-9s | %7s | %10s | %8s\n", "leaf cap", "nodes", "avg visited", "bvh ms");
    printf("----------+---------+------------+---------\n");
    gen_uniform(boxes, N_BOXES);
    gen_rays(rays, N_RAYS, boxes, N_BOXES);
    {
        int caps[] = { 1, 2, 4, 8, 16 };
        for (i = 0; i < 5; i++) {
            BVH bvh;
            long vis = 0;
            double t0, dt;
            bvh_build(&bvh, boxes, N_BOXES, caps[i], 1);
            t0 = now_ms();
            bvh_intersect_batch(&bvh, rays, hout, N_RAYS, &vis);
            dt = now_ms() - t0;
            printf("%-9d | %7d | %10.1f | %8.2f\n", caps[i], bvh.nnodes,
                   (double)vis / N_RAYS, dt);
            bvh_free(&bvh);
        }
    }

    printf("\n%s (failures: %d)\n", g_failures == 0 ? "ALL OK" : "FAILURES", g_failures);

    free(boxes);
    free(rays);
    free(href);
    free(hout);
    return g_failures == 0 ? 0 : 1;
}
