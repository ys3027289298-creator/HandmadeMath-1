/*
 * svd_sample.c — small self-contained demo/benchmark for HMM_SVDM3 / HMM_SVDM4.
 * Build: gcc -std=c99 -Wall -Wextra -I. svd_sample.c -lm -o svd_sample
 */
#include "HandmadeMath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* column-major helpers for the demo */
static void mat_identity(float *M, int N)
{
    int i, j;
    for (j = 0; j < N; ++j)
        for (i = 0; i < N; ++i)
            M[i + j * N] = (i == j) ? 1.0f : 0.0f;
}

static void mat_mul(const float *A, const float *B, float *Out, int N)
{
    int i, j, k;
    float Tmp[16];
    for (j = 0; j < N; ++j)
        for (i = 0; i < N; ++i)
        {
            float Sum = 0.0f;
            for (k = 0; k < N; ++k)
                Sum += A[i + k * N] * B[k + j * N];
            Tmp[i + j * N] = Sum;
        }
    for (i = 0; i < N * N; ++i)
        Out[i] = Tmp[i];
}

static void mat_transpose(const float *A, float *Out, int N)
{
    int i, j;
    float Tmp[16];
    for (j = 0; j < N; ++j)
        for (i = 0; i < N; ++i)
            Tmp[i + j * N] = A[j + i * N];
    for (i = 0; i < N * N; ++i)
        Out[i] = Tmp[i];
}

/* rotation in the (p,q) plane, N x N */
static void mat_givens(float *M, int N, int P, int Q, float Angle)
{
    float C = cosf(Angle), S = sinf(Angle);
    mat_identity(M, N);
    M[P + P * N] = C;
    M[Q + Q * N] = C;
    M[P + Q * N] = -S;
    M[Q + P * N] = S;
}

static float max_ortho_error(const float *M, int N)
{
    /* max |M^T M - I| */
    float MT[16], MTM[16];
    float Err = 0.0f;
    int i, j;
    mat_transpose(M, MT, N);
    mat_mul(MT, M, MTM, N);
    for (j = 0; j < N; ++j)
        for (i = 0; i < N; ++i)
        {
            float D = fabsf(MTM[i + j * N] - ((i == j) ? 1.0f : 0.0f));
            if (D > Err) Err = D;
        }
    return Err;
}

static float max_recon_error(const float *A, const float *U, const float *S,
                             const float *V, int N)
{
    float Sigma[16], US[16], VT[16], R[16];
    float Err = 0.0f;
    int i, j;
    for (i = 0; i < 16; ++i) Sigma[i] = 0.0f;
    for (i = 0; i < N; ++i) Sigma[i + i * N] = S[i];
    mat_mul(U, Sigma, US, N);
    mat_transpose(V, VT, N);
    mat_mul(US, VT, R, N);
    for (j = 0; j < N; ++j)
        for (i = 0; i < N; ++i)
        {
            float D = fabsf(R[i + j * N] - A[i + j * N]);
            if (D > Err) Err = D;
        }
    return Err;
}

static float max_sigma_error(const float *S, const float *Exact, int N)
{
    float Err = 0.0f;
    int i;
    for (i = 0; i < N; ++i)
    {
        float D = fabsf(S[i] - Exact[i]);
        if (D > Err) Err = D;
    }
    return Err;
}

static void run_case(const char *Name, const float *U, const float *V,
                     const float *Sigma, int N)
{
    float S[16], US[16], VT[16], A[16];
    float OutU[16], OutS[4], OutV[16];
    int i, Status;

    for (i = 0; i < 16; ++i) S[i] = 0.0f;
    for (i = 0; i < N; ++i) S[i + i * N] = Sigma[i];
    mat_mul(U, S, US, N);
    mat_transpose(V, VT, N);
    mat_mul(US, VT, A, N); /* A = U Sigma V^T, analytic singular values = Sigma */

    if (N == 3)
    {
        HMM_Mat3 M;
        HMM_SVD3 R;
        memcpy(&M, A, sizeof(M));
        Status = HMM_SVDM3(M, 64, 1e-7f, &R);
        for (i = 0; i < 9; ++i) { OutU[i] = R.U.Elements[i / 3][i % 3]; OutV[i] = R.V.Elements[i / 3][i % 3]; }
        for (i = 0; i < 3; ++i) OutS[i] = R.S.Elements[i];
    }
    else
    {
        HMM_Mat4 M;
        HMM_SVD4 R;
        memcpy(&M, A, sizeof(M));
        Status = HMM_SVDM4(M, 64, 1e-7f, &R);
        for (i = 0; i < 16; ++i) { OutU[i] = R.U.Elements[i / 4][i % 4]; OutV[i] = R.V.Elements[i / 4][i % 4]; }
        for (i = 0; i < 4; ++i) OutS[i] = R.S.Elements[i];
    }

    printf("%s: status=%d\n", Name, Status);
    printf("  sigma = [");
    for (i = 0; i < N; ++i) printf("%s%.7f", i ? ", " : "", (double)OutS[i]);
    printf("]\n");
    printf("  max |sigma - exact|      = %.3e\n", (double)max_sigma_error(OutS, Sigma, N));
    printf("  max |A - U S V^T|        = %.3e\n", (double)max_recon_error(A, OutU, OutS, OutV, N));
    printf("  max |U^T U - I|          = %.3e\n", (double)max_ortho_error(OutU, N));
    printf("  max |V^T V - I|          = %.3e\n", (double)max_ortho_error(OutV, N));
}

static void run_timing(int N, int Reps)
{
    float A[16];
    volatile float Sink = 0.0f;
    clock_t Start, End;
    int i, Rep;

    for (i = 0; i < N * N; ++i)
        A[i] = 0.5f + 0.25f * (float)((i * 7 + 3) % 11) - 1.0f;

    Start = clock();
    for (Rep = 0; Rep < Reps; ++Rep)
    {
        if (N == 3)
        {
            HMM_Mat3 M;
            HMM_SVD3 R;
            memcpy(&M, A, sizeof(M));
            HMM_SVDM3(M, 64, 1e-7f, &R);
            Sink += R.S.X;
        }
        else
        {
            HMM_Mat4 M;
            HMM_SVD4 R;
            memcpy(&M, A, sizeof(M));
            HMM_SVDM4(M, 64, 1e-7f, &R);
            Sink += R.S.X;
        }
    }
    End = clock();

    printf("timing %dx%d: %d reps in %.3f s -> %.3f us/call\n", N, N, Reps,
           (double)(End - Start) / CLOCKS_PER_SEC,
           1e6 * (double)(End - Start) / CLOCKS_PER_SEC / Reps);
    (void)Sink;
}

static void run_error_cases(void)
{
    HMM_Mat3 A = HMM_M3D(1.0f);
    HMM_SVD3 R;
    float DummyU[9], DummyS[3], DummyV[9];
    float Zero = 0.0f;
    int S1, S2, S3, S4;

    S1 = HMM_SVDM3(A, 64, 1e-7f, 0);            /* NULL output */
    S2 = HMM_SVDM3(A, 0, 1e-7f, &R);            /* zero iterations */
    A.Elements[1][1] = 0.0f / Zero;             /* NaN input */
    S3 = HMM_SVDM3(A, 64, 1e-7f, &R);
    S4 = HMM__SVDCore(DummyU, 0, 64, 1e-7f, DummyU, DummyS, DummyV); /* empty matrix */

    printf("error cases: null_out=%d zero_iters=%d nan_input=%d empty_matrix=%d\n",
           S1, S2, S3, S4);
}

int main(void)
{
    float U3[16], V3[16], T[16], T2[16];
    float U4[16], V4[16];
    float Sigma3[3] = {4.0f, 2.0f, 0.5f};
    float Sigma4[4] = {5.0f, 3.0f, 1.0f, 0.25f};
    float Sigma3RankDef[3] = {3.0f, 1.0f, 0.0f};
    float Sigma4RankDef[4] = {4.0f, 2.0f, 0.5f, 0.0f};

    /* 3x3: U = Rz(0.4) Rx(-0.7), V = Ry(0.9) Rz(0.2) */
    mat_givens(T, 3, 0, 1, 0.4f);
    mat_givens(T2, 3, 1, 2, -0.7f);
    mat_mul(T, T2, U3, 3);
    mat_givens(T, 3, 0, 2, 0.9f);
    mat_givens(T2, 3, 0, 1, 0.2f);
    mat_mul(T, T2, V3, 3);

    /* 4x4: U = G01(0.3) G23(-0.5) G02(0.8), V = G12(0.6) G03(-0.4) G13(0.25) */
    mat_givens(T, 4, 0, 1, 0.3f);
    mat_givens(T2, 4, 2, 3, -0.5f);
    mat_mul(T, T2, U4, 4);
    mat_givens(T, 4, 0, 2, 0.8f);
    mat_mul(U4, T, T2, 4);
    for (int i = 0; i < 16; ++i) U4[i] = T2[i];
    mat_givens(T, 4, 1, 2, 0.6f);
    mat_givens(T2, 4, 0, 3, -0.4f);
    mat_mul(T, T2, V4, 4);
    mat_givens(T, 4, 1, 3, 0.25f);
    mat_mul(V4, T, T2, 4);
    for (int i = 0; i < 16; ++i) V4[i] = T2[i];

    run_case("3x3 full-rank", U3, V3, Sigma3, 3);
    run_case("4x4 full-rank", U4, V4, Sigma4, 4);
    run_case("3x3 rank-deficient (zero sigma)", U3, V3, Sigma3RankDef, 3);
    run_case("4x4 rank-deficient (zero sigma)", U4, V4, Sigma4RankDef, 4);

    run_error_cases();

    run_timing(3, 200000);
    run_timing(4, 200000);

    return 0;
}
