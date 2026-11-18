/*
 * batch_ortho_demo.c
 *
 * Standalone demo / test for the batch quaternion re-orthogonalization API
 * added to HandmadeMath.h:
 *
 *     HMM_OrthogonalizeM3      - stage 1, Gram-Schmidt basis correction
 *     HMM_OrthoM3ToQ           - stage 2, quaternion rebuilt from the basis
 *     HMM_OrthogonalizeQ       - all three stages, single quaternion
 *     HMM_OrthogonalizeBatchQ  - batched entry point
 *
 * Four small input sets are exercised:
 *   1. ideal        - an already orthogonal quaternion
 *   2. slight drift - a few percent of basis distortion
 *   3. severe drift - heavily distorted but still invertible basis
 *   4. singular     - an all-zero quaternion / collapsed basis, must error
 *
 * For every repairable set it prints:
 *   - orthogonality error before and after correction
 *   - attitude deviation between pre-rebuild and corrected attitude
 *   - batch timing for several configurable batch sizes
 *
 * Build (no new dependencies, no build-script changes):
 *   cc -std=c11 -Wall -Wextra -Wpedantic -I.. -o batch_ortho_demo batch_ortho_demo.c -lm
 */

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "../HandmadeMath.h"

#define DEMO_COUNT 8
#define DEMO_ITERS 2000

typedef struct
{
    const char *Name;
    HMM_Quat Input;
    HMM_Bool Singular;
} DemoCase;

/* Determinant of the 3x3 rotation part reconstructed from a quaternion. */
static float Det3(HMM_Mat3 M)
{
    return M.Elements[0][0] *
               (M.Elements[1][1] * M.Elements[2][2] - M.Elements[2][1] * M.Elements[1][2])
         - M.Elements[1][0] *
               (M.Elements[0][1] * M.Elements[2][2] - M.Elements[2][1] * M.Elements[0][2])
         + M.Elements[2][0] *
               (M.Elements[0][1] * M.Elements[1][2] - M.Elements[1][1] * M.Elements[0][2]);
}

/*
 * Orthogonality error of the rotation matrix a quaternion represents.
 * The matrix is built from the raw, unnormalized quaternion components with
 * the standard bilinear construction (R = I + 2w[v]x + 2[v]x^2), so drift in
 * the accumulator shows up as non-unit, non-orthogonal columns: this is the
 * state before repair. (HMM_QToM4 could not be used because it normalizes
 * internally and would hide the drift.) Returns the max of the off-diagonal
 * column dot products and the column-norm deviations; zero means a perfectly
 * orthonormal SO(3) frame.
 */
static float OrthoError(HMM_Quat Q)
{
    HMM_Mat3 M;
    HMM_Vec3 C0, C1, C2;
    float XX, YY, ZZ, XY, XZ, YZ, WX, WY, WZ;
    float D01, D02, D12, N0, N1, N2, Error;

    XX = Q.X * Q.X;
    YY = Q.Y * Q.Y;
    ZZ = Q.Z * Q.Z;
    XY = Q.X * Q.Y;
    XZ = Q.X * Q.Z;
    YZ = Q.Y * Q.Z;
    WX = Q.W * Q.X;
    WY = Q.W * Q.Y;
    WZ = Q.W * Q.Z;

    M.Elements[0][0] = 1.0f - 2.0f * (YY + ZZ);
    M.Elements[0][1] = 2.0f * (XY + WZ);
    M.Elements[0][2] = 2.0f * (XZ - WY);

    M.Elements[1][0] = 2.0f * (XY - WZ);
    M.Elements[1][1] = 1.0f - 2.0f * (XX + ZZ);
    M.Elements[1][2] = 2.0f * (YZ + WX);

    M.Elements[2][0] = 2.0f * (XZ + WY);
    M.Elements[2][1] = 2.0f * (YZ - WX);
    M.Elements[2][2] = 1.0f - 2.0f * (XX + YY);

    C0 = M.Columns[0];
    C1 = M.Columns[1];
    C2 = M.Columns[2];

    D01 = HMM_ABS(HMM_DotV3(C0, C1));
    D02 = HMM_ABS(HMM_DotV3(C0, C2));
    D12 = HMM_ABS(HMM_DotV3(C1, C2));
    N0 = HMM_ABS(1.0f - HMM_LenV3(C0));
    N1 = HMM_ABS(1.0f - HMM_LenV3(C1));
    N2 = HMM_ABS(1.0f - HMM_LenV3(C2));

    Error = D01;
    Error = HMM_MAX(Error, D02);
    Error = HMM_MAX(Error, D12);
    Error = HMM_MAX(Error, N0);
    Error = HMM_MAX(Error, N1);
    Error = HMM_MAX(Error, N2);

    return Error;
}

/* Geodesic angle (radians) between two unit quaternions, i.e. the rotation
 * that separates the two attitudes. */
static float AttitudeAngle(HMM_Quat A, HMM_Quat B)
{
    float Dot = HMM_ABS(HMM_DotQ(HMM_NormQ(A), HMM_NormQ(B)));
    if (Dot > 1.0f) {
        Dot = 1.0f;
    }
    return 2.0f * HMM_ACosF(Dot);
}

/*
 * Synthesize a drifted attitude the way batched integration leaves it: the
 * quaternion accumulator itself accumulates small per-step errors, so its
 * components no longer describe an orthonormal frame. The perturbation is
 * additive on every component and deterministic; no renormalization happens,
 * which is exactly the state HMM_OrthogonalizeQ is meant to repair.
 */
static HMM_Quat DriftedQuat(HMM_Quat Base, float Noise)
{
    static const float Pattern[4] = { 1.0f, -0.7f, 0.3f, -0.5f };

    return HMM_Q(
        Base.X + Noise * Pattern[0],
        Base.Y + Noise * Pattern[1],
        Base.Z + Noise * Pattern[2],
        Base.W + Noise * Pattern[3]
    );
}

static double RunBatch(const HMM_Quat *In, int Count, int BatchSize,
    HMM_Bool PreserveHandedness, HMM_Bool SingularPolicy, HMM_Quat *Out)
{
    clock_t Start;
    int Iter, Written = 0;

    Start = clock();
    for (Iter = 0; Iter < DEMO_ITERS; ++Iter) {
        Written = HMM_OrthogonalizeBatchQ(In, Count, BatchSize,
            PreserveHandedness, SingularPolicy, Out);
    }

    if (Written != Count) {
        printf("    (unexpected return count %d vs %d)\n", Written, Count);
    }

    return ((double)(clock() - Start) / CLOCKS_PER_SEC) * 1000.0;
}

int main(void)
{
    HMM_Quat Base = HMM_QFromAxisAngle_RH(HMM_NormV3(HMM_V3(1.0f, 2.0f, 3.0f)), 0.9f);
    DemoCase Cases[4];
    HMM_Quat In[DEMO_COUNT];
    HMM_Quat Out[DEMO_COUNT];
    int I;
    int Failures = 0;

    Cases[0].Name = "ideal";
    Cases[0].Input = Base;
    Cases[0].Singular = 0;

    Cases[1].Name = "slight drift";
    Cases[1].Input = DriftedQuat(Base, 0.02f);
    Cases[1].Singular = 0;

    Cases[2].Name = "severe drift";
    Cases[2].Input = DriftedQuat(Base, 0.20f);
    Cases[2].Singular = 0;

    Cases[3].Name = "singular";
    Cases[3].Input = HMM_Q(0.0f, 0.0f, 0.0f, 0.0f);
    Cases[3].Singular = 1;

    printf("=== Batch quaternion re-orthogonalization demo ===\n\n");

    for (I = 0; I < 4; ++I) {
        HMM_Quat Single;
        HMM_Bool Ok;

        printf("[%s]\n", Cases[I].Name);
        printf("  input quat        = (%.6f, %.6f, %.6f, %.6f)\n",
            Cases[I].Input.X, Cases[I].Input.Y, Cases[I].Input.Z, Cases[I].Input.W);
        if (Cases[I].Singular) {
            printf("  ortho error in    = n/a (singular)\n");
        } else {
            printf("  ortho error in    = %.3e\n", OrthoError(Cases[I].Input));
        }

        Ok = HMM_OrthogonalizeQ(Cases[I].Input, HMM_ORTHO_PRESERVE_HANDEDNESS, &Single);

        if (Cases[I].Singular) {
            if (Ok) {
                printf("  FAIL: singular input was not rejected\n\n");
                ++Failures;
            } else {
                printf("  singular rejected by single-quat API: OK\n\n");
            }
            continue;
        }

        if (!Ok) {
            printf("  FAIL: repairable input rejected\n\n");
            ++Failures;
            continue;
        }

        printf("  corrected quat    = (%.6f, %.6f, %.6f, %.6f)\n",
            Single.X, Single.Y, Single.Z, Single.W);
        printf("  ortho error out   = %.3e\n", OrthoError(Single));
        printf("  attitude deviation= %.3e rad\n", AttitudeAngle(Cases[I].Input, Single));
        printf("  output |q|        = %.6f\n",
            HMM_SqrtF(HMM_DotQ(Single, Single)));

        if (OrthoError(Single) > 1.0e-5f) {
            printf("  FAIL: corrected frame not orthonormal\n");
            ++Failures;
        }
        if (HMM_ABS(HMM_SqrtF(HMM_DotQ(Single, Single)) - 1.0f) > 1.0e-6f) {
            printf("  FAIL: corrected quaternion not unit length\n");
            ++Failures;
        }
        if (I == 0 && AttitudeAngle(Cases[I].Input, Single) > 1.0e-5f) {
            printf("  FAIL: ideal attitude changed\n");
            ++Failures;
        }
        printf("\n");
    }

    /* Replicate the three repairable cases into one batch. */
    for (I = 0; I < DEMO_COUNT; ++I) {
        In[I] = Cases[I % 3].Input;
    }

    printf("=== Batch of %d quaternions (%d iters, times in ms) ===\n",
        DEMO_COUNT, DEMO_ITERS);
    printf("%-12s %-12s %s\n", "batch size", "handedness", "total ms");
    {
        const int BatchSizes[3] = { 1, 4, 0 }; /* 0 means one chunk */
        const char *Labels[3] = { "1", "4", "all" };
        int B;
        for (B = 0; B < 3; ++B) {
            double Ms;
            Ms = RunBatch(In, DEMO_COUNT, BatchSizes[B],
                HMM_ORTHO_PRESERVE_HANDEDNESS, HMM_ORTHO_REPORT_AND_ABORT, Out);
            printf("%-12s %-12s %.3f\n", Labels[B], "preserve", Ms);
        }
        {
            double Ms = RunBatch(In, DEMO_COUNT, 4,
                HMM_ORTHO_RELAX_HANDEDNESS, HMM_ORTHO_REPORT_AND_ABORT, Out);
            printf("%-12s %-12s %.3f\n", "4", "relax", Ms);
        }
    }
    printf("\n");

    /* Batch-size invariance: every chunking must produce identical output. */
    {
        HMM_Quat Ref[DEMO_COUNT];
        int B, K, Same = 1;

        HMM_OrthogonalizeBatchQ(In, DEMO_COUNT, 0, HMM_ORTHO_PRESERVE_HANDEDNESS,
            HMM_ORTHO_REPORT_AND_ABORT, Ref);
        for (B = 1; B <= DEMO_COUNT && Same; ++B) {
            HMM_OrthogonalizeBatchQ(In, DEMO_COUNT, B, HMM_ORTHO_PRESERVE_HANDEDNESS,
                HMM_ORTHO_REPORT_AND_ABORT, Out);
            for (K = 0; K < DEMO_COUNT; ++K) {
                if (HMM_ABS(Out[K].X - Ref[K].X) > 1.0e-6f ||
                    HMM_ABS(Out[K].Y - Ref[K].Y) > 1.0e-6f ||
                    HMM_ABS(Out[K].Z - Ref[K].Z) > 1.0e-6f ||
                    HMM_ABS(Out[K].W - Ref[K].W) > 1.0e-6f) {
                    Same = 0;
                    break;
                }
            }
        }
        printf("batch-size invariance: %s\n", Same ? "OK" : "FAIL");
        if (!Same) {
            ++Failures;
        }
    }

    /* Handedness: preserve mode restores determinant +1 on a reflected
     * basis; relax mode leaves a negative determinant in place. */
    {
        HMM_Mat3 Reflected = {0};
        HMM_Mat3 Fixed;
        HMM_Bool OkP, OkR;

        Reflected.Columns[0] = HMM_V3(-1.0f, 0.0f, 0.0f);
        Reflected.Columns[1] = HMM_V3(0.0f, 1.0f, 0.0f);
        Reflected.Columns[2] = HMM_V3(0.0f, 0.0f, 1.0f);

        OkP = HMM_OrthogonalizeM3(Reflected, HMM_ORTHO_PRESERVE_HANDEDNESS, &Fixed);
        if (!OkP || HMM_ABS(Det3(Fixed) - 1.0f) > 1.0e-6f) {
            printf("handedness preserve: FAIL\n");
            ++Failures;
        } else {
            printf("handedness preserve (det -> %+.1f): OK\n", Det3(Fixed));
        }

        OkR = HMM_OrthogonalizeM3(Reflected, HMM_ORTHO_RELAX_HANDEDNESS, &Fixed);
        if (!OkR || HMM_ABS(Det3(Fixed) - (-1.0f)) > 1.0e-6f) {
            printf("handedness relax: FAIL\n");
            ++Failures;
        } else {
            printf("handedness relax    (det -> %+.1f): OK\n", Det3(Fixed));
        }
    }

    /* Singular batch handling: abort vs skip. */
    {
        HMM_Quat Bad[3];
        HMM_Quat GoodOut[3];
        int NAbort, NSkip;

        Bad[0] = Cases[1].Input;
        Bad[1] = HMM_Q(0.0f, 0.0f, 0.0f, 0.0f);
        Bad[2] = Cases[2].Input;

        NAbort = HMM_OrthogonalizeBatchQ(Bad, 3, 2, HMM_ORTHO_PRESERVE_HANDEDNESS,
            HMM_ORTHO_REPORT_AND_ABORT, GoodOut);
        NSkip = HMM_OrthogonalizeBatchQ(Bad, 3, 2, HMM_ORTHO_PRESERVE_HANDEDNESS,
            HMM_ORTHO_SKIP_SINGULAR, GoodOut);

        printf("singular abort returns %d (expect 1), skip returns %d (expect 2): %s\n",
            NAbort, NSkip, (NAbort == 1 && NSkip == 2) ? "OK" : "FAIL");
        if (!(NAbort == 1 && NSkip == 2)) {
            ++Failures;
        }
    }

    /* A truly collapsed basis (two parallel columns) must also fail. */
    {
        HMM_Mat3 Collapsed = {0};
        HMM_Mat3 Fixed;
        HMM_Bool Ok;

        Collapsed.Columns[0] = HMM_V3(1.0f, 0.0f, 0.0f);
        Collapsed.Columns[1] = HMM_V3(2.0f, 0.0f, 0.0f);
        Collapsed.Columns[2] = HMM_V3(0.0f, 1.0f, 0.0f);

        Ok = HMM_OrthogonalizeM3(Collapsed, HMM_ORTHO_PRESERVE_HANDEDNESS, &Fixed);
        printf("collapsed basis rejected: %s\n", Ok ? "FAIL" : "OK");
        if (Ok) {
            ++Failures;
        }
    }

    printf("\n=== %s (%d failure%s) ===\n",
        Failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
        Failures, Failures == 1 ? "" : "s");

    return Failures == 0 ? 0 : 1;
}
