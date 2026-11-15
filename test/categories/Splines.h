#include "../HandmadeTest.h"

TEST(Splines, CatmullRomScalar)
{
    // Hand-computed from C(t) = 0.5*(2 + 3t + 3t^2 - 2t^3) for P = (0, 1, 3, 4).
    EXPECT_FLOAT_EQ(HMM_CatmullRomF(0.0f, 1.0f, 3.0f, 4.0f, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(HMM_CatmullRomF(0.0f, 1.0f, 3.0f, 4.0f, 0.25f), 1.453125f);
    EXPECT_FLOAT_EQ(HMM_CatmullRomF(0.0f, 1.0f, 3.0f, 4.0f, 0.5f), 2.0f);
    EXPECT_FLOAT_EQ(HMM_CatmullRomF(0.0f, 1.0f, 3.0f, 4.0f, 0.75f), 2.546875f);
    EXPECT_FLOAT_EQ(HMM_CatmullRomF(0.0f, 1.0f, 3.0f, 4.0f, 1.0f), 3.0f);
}

TEST(Splines, BSplineScalar)
{
    // Hand-computed from B(t) = (7 + 9t + 3t^2 - 2t^3) / 6 for P = (0, 1, 3, 4).
    const float absError = 0.0001f;

    EXPECT_NEAR(HMM_BSplineF(0.0f, 1.0f, 3.0f, 4.0f, 0.0f), 7.0f / 6.0f, absError);
    EXPECT_NEAR(HMM_BSplineF(0.0f, 1.0f, 3.0f, 4.0f, 0.25f), 9.40625f / 6.0f, absError);
    EXPECT_FLOAT_EQ(HMM_BSplineF(0.0f, 1.0f, 3.0f, 4.0f, 0.5f), 2.0f);
    EXPECT_NEAR(HMM_BSplineF(0.0f, 1.0f, 3.0f, 4.0f, 0.75f), 14.59375f / 6.0f, absError);
    EXPECT_NEAR(HMM_BSplineF(0.0f, 1.0f, 3.0f, 4.0f, 1.0f), 17.0f / 6.0f, absError);
}

TEST(Splines, CatmullRomV3Clamp)
{
    HMM_Vec3 points[4];
    HMM_Vec3 out;

    points[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    points[1] = HMM_V3(1.0f, 2.0f, 0.0f);
    points[2] = HMM_V3(3.0f, 2.0f, 0.0f);
    points[3] = HMM_V3(4.0f, 0.0f, 0.0f);

    // Catmull-Rom interpolates the control points at segment boundaries.
    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 0.0f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 0.0f);
    EXPECT_FLOAT_EQ(out.Y, 0.0f);
    EXPECT_FLOAT_EQ(out.Z, 0.0f);

    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 1.0f / 3.0f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 1.0f);
    EXPECT_FLOAT_EQ(out.Y, 2.0f);
    EXPECT_FLOAT_EQ(out.Z, 0.0f);

    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 1.0f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 4.0f);
    EXPECT_FLOAT_EQ(out.Y, 0.0f);
    EXPECT_FLOAT_EQ(out.Z, 0.0f);

    // Global T = 0.5 is local t = 0.5 on the middle segment; hand-computed.
    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 2.0f);
    EXPECT_FLOAT_EQ(out.Y, 2.25f);
    EXPECT_FLOAT_EQ(out.Z, 0.0f);
}

TEST(Splines, BSplineV3Clamp)
{
    HMM_Vec3 points[4];
    HMM_Vec3 out;

    points[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    points[1] = HMM_V3(1.0f, 2.0f, 0.0f);
    points[2] = HMM_V3(3.0f, 2.0f, 0.0f);
    points[3] = HMM_V3(4.0f, 0.0f, 0.0f);

    // Global T = 0.5 is local t = 0.5 on the middle segment; hand-computed.
    EXPECT_TRUE(HMM_BSplineV3(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 2.0f);
    EXPECT_NEAR(out.Y, 11.5f / 6.0f, 0.0001f);
    EXPECT_FLOAT_EQ(out.Z, 0.0f);
}

TEST(Splines, CatmullRomV2Wrap)
{
    HMM_Vec2 points[3];
    HMM_Vec2 outStart, outEnd;

    points[0] = HMM_V2(0.0f, 0.0f);
    points[1] = HMM_V2(2.0f, 0.0f);
    points[2] = HMM_V2(1.0f, 2.0f);

    // A wrapped curve is closed: T = 0 and T = 1 evaluate to the same point.
    EXPECT_TRUE(HMM_CatmullRomV2(points, 3, 0.0f, HMM_SPLINE_ENDPOINT_WRAP, &outStart) == HMM_SPLINE_OK);
    EXPECT_TRUE(HMM_CatmullRomV2(points, 3, 1.0f, HMM_SPLINE_ENDPOINT_WRAP, &outEnd) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(outStart.X, points[0].X);
    EXPECT_FLOAT_EQ(outStart.Y, points[0].Y);
    EXPECT_FLOAT_EQ(outStart.X, outEnd.X);
    EXPECT_FLOAT_EQ(outStart.Y, outEnd.Y);
}

TEST(Splines, BSplineV2Wrap)
{
    HMM_Vec2 points[3];
    HMM_Vec2 outStart, outEnd;

    points[0] = HMM_V2(0.0f, 0.0f);
    points[1] = HMM_V2(2.0f, 0.0f);
    points[2] = HMM_V2(1.0f, 2.0f);

    EXPECT_TRUE(HMM_BSplineV2(points, 3, 0.0f, HMM_SPLINE_ENDPOINT_WRAP, &outStart) == HMM_SPLINE_OK);
    EXPECT_TRUE(HMM_BSplineV2(points, 3, 1.0f, HMM_SPLINE_ENDPOINT_WRAP, &outEnd) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(outStart.X, outEnd.X);
    EXPECT_FLOAT_EQ(outStart.Y, outEnd.Y);
}

TEST(Splines, CatmullRomV4Clamp)
{
    HMM_Vec4 points[4];
    HMM_Vec4 out;

    points[0] = HMM_V4(0.0f, 0.0f, 0.0f, 0.0f);
    points[1] = HMM_V4(1.0f, 1.0f, 1.0f, 1.0f);
    points[2] = HMM_V4(2.0f, 2.0f, 2.0f, 2.0f);
    points[3] = HMM_V4(3.0f, 3.0f, 3.0f, 3.0f);

    // Hand-computed: C(0.5) for P = (0, 1, 2, 3) is 1.5 in every component.
    EXPECT_TRUE(HMM_CatmullRomV4(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 1.5f);
    EXPECT_FLOAT_EQ(out.Y, 1.5f);
    EXPECT_FLOAT_EQ(out.Z, 1.5f);
    EXPECT_FLOAT_EQ(out.W, 1.5f);
}

TEST(Splines, BSplineV4Clamp)
{
    HMM_Vec4 points[4];
    HMM_Vec4 out;

    points[0] = HMM_V4(0.0f, 0.0f, 0.0f, 0.0f);
    points[1] = HMM_V4(1.0f, 1.0f, 1.0f, 1.0f);
    points[2] = HMM_V4(2.0f, 2.0f, 2.0f, 2.0f);
    points[3] = HMM_V4(3.0f, 3.0f, 3.0f, 3.0f);

    // Hand-computed: B(0.5) for P = (0, 1, 2, 3) is 1.5 in every component.
    EXPECT_TRUE(HMM_BSplineV4(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_OK);
    EXPECT_FLOAT_EQ(out.X, 1.5f);
    EXPECT_FLOAT_EQ(out.Y, 1.5f);
    EXPECT_FLOAT_EQ(out.Z, 1.5f);
    EXPECT_FLOAT_EQ(out.W, 1.5f);
}

TEST(Splines, ErrorPaths)
{
    HMM_Vec3 points[4];
    HMM_Vec3 onePoint[1];
    HMM_Vec3 twoPoints[2];
    HMM_Vec3 out;
    float nan;

    points[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    points[1] = HMM_V3(1.0f, 2.0f, 0.0f);
    points[2] = HMM_V3(3.0f, 2.0f, 0.0f);
    points[3] = HMM_V3(4.0f, 0.0f, 0.0f);
    onePoint[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    twoPoints[0] = HMM_V3(0.0f, 0.0f, 0.0f);
    twoPoints[1] = HMM_V3(1.0f, 1.0f, 1.0f);
    nan = HMM_SqrtF(-1.0f);

    EXPECT_TRUE(HMM_CatmullRomV3(0, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_NULL);
    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, 0) == HMM_SPLINE_ERR_NULL);
    EXPECT_TRUE(HMM_BSplineV3(0, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_NULL);

    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 0.5f, 7, &out) == HMM_SPLINE_ERR_BAD_ENDPOINT_MODE);

    EXPECT_TRUE(HMM_CatmullRomV3(onePoint, 1, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_TOO_FEW_POINTS);
    EXPECT_TRUE(HMM_BSplineV3(twoPoints, 2, 0.5f, HMM_SPLINE_ENDPOINT_WRAP, &out) == HMM_SPLINE_ERR_TOO_FEW_POINTS);

    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, nan, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_NAN_T);

    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, -0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_T_OUT_OF_RANGE);
    EXPECT_TRUE(HMM_BSplineV3(points, 4, 1.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_T_OUT_OF_RANGE);

    points[2].Y = nan;
    EXPECT_TRUE(HMM_CatmullRomV3(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_NAN_POINT);
    EXPECT_TRUE(HMM_BSplineV3(points, 4, 0.5f, HMM_SPLINE_ENDPOINT_CLAMP, &out) == HMM_SPLINE_ERR_NAN_POINT);
}
