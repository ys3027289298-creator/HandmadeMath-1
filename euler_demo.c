/*
 * euler_demo.c — demo/verification for the euler <-> quaternion <-> matrix
 * conversions added to HandmadeMath.h.
 *
 * Build:  gcc -std=c99 -Wall -Wextra -O2 euler_demo.c -lm -o euler_demo
 * Run:    ./euler_demo   (prints everything in one run)
 */

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "HandmadeMath.h"

static const char *OrderNames[6] = { "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX" };

/* Rotation distance between two quaternions, in radians. The dot product
   and both norms are computed in double (normalizing by the measured norms,
   not assuming unit length): with float quaternions |q| is only known to
   ~1e-7, and acos() would amplify that to a ~4e-4 rad floor otherwise. */
static double QuatAngleDiff(HMM_Quat A, HMM_Quat B)
{
    double Dot = (double)A.X * B.X + (double)A.Y * B.Y
               + (double)A.Z * B.Z + (double)A.W * B.W;
    double NA  = (double)A.X * A.X + (double)A.Y * A.Y
               + (double)A.Z * A.Z + (double)A.W * A.W;
    double NB  = (double)B.X * B.X + (double)B.Y * B.Y
               + (double)B.Z * B.Z + (double)B.W * B.W;
    double Cos = Dot / sqrt(NA * NB);
    if (Cos < 0.0) Cos = -Cos;
    if (Cos > 1.0) Cos = 1.0;
    return 2.0 * acos(Cos);
}

int main(void)
{
    /* Five poses (radians, stored per-axis as X, Y, Z):
       P1, P2: ordinary poses.
       P3:     exact gimbal lock for orders whose middle axis is Y.
       P4:     just inside the lock epsilon (0.0005 rad off 90 deg).
       P5:     exact gimbal lock at -90 deg. */
    HMM_Vec3 Poses[5];
    const char *PoseNames[5] = {
        "P1 (0.3, -0.5, 0.8)",
        "P2 (-1.2, 0.7, 2.1)",
        "P3 (0.4, +pi/2, -0.9) exact lock",
        "P4 (0.1, pi/2-5e-4, 0.2) near lock",
        "P5 (2.5, -pi/2, 1.3) exact lock"
    };
    HMM_EulerOrder Orders[6] = {
        HMM_EulerOrderXYZ, HMM_EulerOrderXZY, HMM_EulerOrderYXZ,
        HMM_EulerOrderYZX, HMM_EulerOrderZXY, HMM_EulerOrderZYX
    };

    int P, O;
    int LockCount = 0;

    Poses[0] = HMM_V3( 0.3f, -0.5f,                 0.8f);
    Poses[1] = HMM_V3(-1.2f,  0.7f,                 2.1f);
    Poses[2] = HMM_V3( 0.4f,  (float)(HMM_PI / 2), -0.9f);
    Poses[3] = HMM_V3( 0.1f,  (float)(HMM_PI / 2) - 5e-4f, 0.2f);
    Poses[4] = HMM_V3( 2.5f, -(float)(HMM_PI / 2),  1.3f);

    printf("=== 1. Round-trip error per pose (max over all 6 orders) ===\n");
    printf("%-36s %14s %14s %14s\n", "pose", "E->Q->E (rad)", "E->M4->E (rad)", "Q->M4->Q (rad)");
    for (P = 0; P < 5; ++P)
    {
        double MaxErrQ = 0.0, MaxErrM = 0.0, MaxErrQM = 0.0;
        for (O = 0; O < 6; ++O)
        {
            HMM_EulerOrder Order = Orders[O];
            HMM_Quat Q  = HMM_QFromEuler(Poses[P], Order);
            HMM_Mat4 M  = HMM_M4FromEuler(Poses[P], Order);

            HMM_Vec3 E2 = HMM_EulerFromQ(Q, Order);
            HMM_Vec3 E3 = HMM_EulerFromM4(M, Order);
            HMM_Quat Q2 = HMM_QFromEuler(E2, Order);
            HMM_Quat Q3 = HMM_QFromEuler(E3, Order);
            HMM_Quat Q4 = HMM_M4ToQ_RH(HMM_QToM4(Q));

            double ErrQ  = QuatAngleDiff(Q, Q2);
            double ErrM  = QuatAngleDiff(Q, Q3);
            double ErrQM = QuatAngleDiff(Q, Q4);

            if (ErrQ  > MaxErrQ)  MaxErrQ  = ErrQ;
            if (ErrM  > MaxErrM)  MaxErrM  = ErrM;
            if (ErrQM > MaxErrQM) MaxErrQM = ErrQM;
        }
        printf("%-36s %14.3e %14.3e %14.3e\n", PoseNames[P], MaxErrQ, MaxErrM, MaxErrQM);
    }

    printf("\n=== 2. Gimbal lock detection (epsilon = %.0e rad on cos(middle)) ===\n",
           (double)HMM_GIMBAL_LOCK_EPSILON);
    printf("%-36s", "pose");
    for (O = 0; O < 6; ++O) printf(" %5s", OrderNames[O]);
    printf("\n");
    for (P = 0; P < 5; ++P)
    {
        printf("%-36s", PoseNames[P]);
        for (O = 0; O < 6; ++O)
        {
            int Locked = HMM_EulerIsGimbalLock(Poses[P], Orders[O]);
            /* cross-check: detection via quaternion and matrix must agree */
            HMM_Quat Q = HMM_QFromEuler(Poses[P], Orders[O]);
            HMM_Mat4 M = HMM_M4FromEuler(Poses[P], Orders[O]);
            if (Locked != HMM_QIsGimbalLock(Q, Orders[O]) ||
                Locked != HMM_M4IsGimbalLock(M, Orders[O]))
            {
                printf(" MISMCH");
            }
            else
            {
                printf(" %5s", Locked ? "LOCK" : "-");
            }
            LockCount += Locked;
        }
        printf("\n");
    }
    printf("detected gimbal locks: %d (5 poses x 6 orders = 30 checks)\n", LockCount);

    printf("\n=== 3. Sample output, pose P1 under all six orders ===\n");
    for (O = 0; O < 6; ++O)
    {
        HMM_EulerOrder Order = Orders[O];
        HMM_Quat Q  = HMM_QFromEuler(Poses[0], Order);
        HMM_Vec3 E2 = HMM_EulerFromQ(Q, Order);
        printf("%s: E=(% .4f, % .4f, % .4f) -> Q=(% .4f, % .4f, % .4f, % .4f) -> E'=(% .4f, % .4f, % .4f)\n",
               OrderNames[O],
               Poses[0].X, Poses[0].Y, Poses[0].Z,
               Q.X, Q.Y, Q.Z, Q.W,
               E2.X, E2.Y, E2.Z);
    }

    printf("\n=== 4. Gimbal lock behavior (P3, order XYZ): first angle zeroed ===\n");
    {
        HMM_Vec3 E2 = HMM_EulerFromQ(HMM_QFromEuler(Poses[2], HMM_EulerOrderXYZ), HMM_EulerOrderXYZ);
        printf("in : X=% .4f Y=% .4f Z=% .4f\n", Poses[2].X, Poses[2].Y, Poses[2].Z);
        printf("out: X=% .4f Y=% .4f Z=% .4f  (rotation preserved, X folded into Z)\n", E2.X, E2.Y, E2.Z);
    }

    printf("\n=== 5. Invalid poses ===\n");
    {
        HMM_Vec3 BadEuler = HMM_V3(0.0f, NAN, 0.0f);
        HMM_Quat ZeroQ    = HMM_Q(0.0f, 0.0f, 0.0f, 0.0f);
        HMM_Quat NanQ     = HMM_Q(NAN, 0.0f, 0.0f, 1.0f);
        HMM_EulerOrder BadOrder = (HMM_EulerOrder)42;

        if (!HMM_EulerIsValid(BadEuler))
            printf("ERROR: euler pose with non-finite angle rejected\n");
        if (!HMM_QIsValid(ZeroQ))
            printf("ERROR: zero-norm quaternion rejected (cannot be normalized)\n");
        if (!HMM_QIsValid(NanQ))
            printf("ERROR: non-finite quaternion rejected\n");
        if (!HMM_EulerOrderIsValid(BadOrder))
            printf("ERROR: euler order 42 rejected (out of range)\n");
        printf("(conversion functions additionally HMM_ASSERT on such inputs in debug builds)\n");
    }

    printf("\n=== 6. Batch timing: 10000 full conversion cycles ===\n");
    {
        enum { N = 10000 };
        HMM_Vec3 E = Poses[0];
        volatile float Sink = 0.0f;
        clock_t Start, End;
        int I, K;

        Start = clock();
        for (I = 0; I < N; ++I)
        {
            for (K = 0; K < 6; ++K)
            {
                HMM_Quat Q  = HMM_QFromEuler(E, Orders[K]);
                HMM_Mat4 M  = HMM_QToM4(Q);
                HMM_Quat Q2 = HMM_M4ToQ_RH(M);
                HMM_Vec3 E2 = HMM_EulerFromQ(Q2, Orders[K]);
                HMM_Mat4 M2 = HMM_M4FromEuler(E2, Orders[K]);
                HMM_Vec3 E3 = HMM_EulerFromM4(M2, Orders[K]);
                Sink += E3.X;
            }
        }
        End = clock();

        printf("10000 iterations x 6 orders x 6 conversions = %d conversions\n", N * 6 * 6);
        printf("total: %.3f ms, per conversion: %.3f us\n",
               1000.0 * (double)(End - Start) / CLOCKS_PER_SEC,
               1e6 * (double)(End - Start) / CLOCKS_PER_SEC / (N * 6 * 6));
        (void)Sink;
    }

    return 0;
}
