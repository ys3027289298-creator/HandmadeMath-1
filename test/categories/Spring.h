#include "../HandmadeTest.h"

TEST(Spring, NullState)
{
    HMM_SpringConfig Config = { 200.0f, 10.0f, 5.0f };
    EXPECT_TRUE(HMM_SpringStep(0, Config, 1.0f / 60.0f) == HMM_SPRING_ERR_NULL);
}

TEST(Spring, BadParameters)
{
    HMM_Spring State = { 0.0f, 0.0f };
    HMM_Spring Original = State;
    HMM_SpringConfig Config = { 200.0f, 10.0f, 5.0f };
    HMM_SpringConfig NaNConfig;
    float NaN;

    /* NaN payload: (x != x) is only true for NaN. */
    NaN = HMM_SqrtF(-1.0f);
    NaNConfig.Stiffness = NaN;
    NaNConfig.Damping = 10.0f;
    NaNConfig.Rest = 5.0f;

    EXPECT_TRUE(HMM_SpringStep(&State, NaNConfig, 1.0f / 60.0f) == HMM_SPRING_ERR_BAD_PARAM);

    Config.Stiffness = -1.0f;
    EXPECT_TRUE(HMM_SpringStep(&State, Config, 1.0f / 60.0f) == HMM_SPRING_ERR_BAD_PARAM);
    Config.Stiffness = 200.0f;

    Config.Damping = -1.0f;
    EXPECT_TRUE(HMM_SpringStep(&State, Config, 1.0f / 60.0f) == HMM_SPRING_ERR_BAD_PARAM);
    Config.Damping = 10.0f;

    EXPECT_TRUE(HMM_SpringStep(&State, Config, -1.0f / 60.0f) == HMM_SPRING_ERR_BAD_PARAM);
    EXPECT_TRUE(HMM_SpringStep(&State, Config, NaN) == HMM_SPRING_ERR_BAD_PARAM);

    State.P = NaN;
    EXPECT_TRUE(HMM_SpringStep(&State, Config, 1.0f / 60.0f) == HMM_SPRING_ERR_BAD_PARAM);

    /* On error the state must be left untouched. */
    State = Original;
    State.P = NaN;
    HMM_SpringStep(&State, Config, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(State.V, Original.V);
}

TEST(Spring, ZeroDeltaTimeIsNoop)
{
    HMM_Spring State = { 1.0f, -2.0f };
    HMM_SpringConfig Config = { 200.0f, 10.0f, 5.0f };

    EXPECT_TRUE(HMM_SpringStep(&State, Config, 0.0f) == HMM_SPRING_OK);
    EXPECT_FLOAT_EQ(State.P, 1.0f);
    EXPECT_FLOAT_EQ(State.V, -2.0f);
}

TEST(Spring, UnstableConfigurationIsRejected)
{
    HMM_Spring State = { 0.0f, 0.0f };
    HMM_Spring Original = State;
    /* Stiffness high enough that no sub-step count up to the cap is stable. */
    HMM_SpringConfig Config = { 1.0e10f, 0.0f, 1.0f };

    EXPECT_TRUE(HMM_SpringStep(&State, Config, 1.0f / 30.0f) == HMM_SPRING_ERR_UNSTABLE);

    /* Divergent runs must report an error, never NaN, and not mutate state. */
    EXPECT_FLOAT_EQ(State.P, Original.P);
    EXPECT_FLOAT_EQ(State.V, Original.V);
    EXPECT_TRUE(HMM_SpringIsFinite(State.P));
    EXPECT_TRUE(HMM_SpringIsFinite(State.V));
}

TEST(Spring, ConvergesAcrossFrameRates)
{
    /* k = 5000 needs adaptive sub-stepping at 1/30 (one step violates the
       stability condition), while 1/240 integrates without sub-stepping. */
    HMM_SpringConfig Config = { 5000.0f, 20.0f, 10.0f };
    const float Duration = 2.0f;
    const float Epsilon = 0.05f;
    HMM_Spring State30 = { 0.0f, 0.0f };
    HMM_Spring State60 = { 0.0f, 0.0f };
    HMM_Spring State240 = { 0.0f, 0.0f };
    int Frames30 = (int)(Duration * 30.0f);
    int Frames60 = (int)(Duration * 60.0f);
    int Frames240 = (int)(Duration * 240.0f);
    int Index;

    for (Index = 0; Index < Frames30; ++Index)
    {
        EXPECT_TRUE(HMM_SpringStep(&State30, Config, 1.0f / 30.0f) == HMM_SPRING_OK);
    }
    for (Index = 0; Index < Frames60; ++Index)
    {
        EXPECT_TRUE(HMM_SpringStep(&State60, Config, 1.0f / 60.0f) == HMM_SPRING_OK);
    }
    for (Index = 0; Index < Frames240; ++Index)
    {
        EXPECT_TRUE(HMM_SpringStep(&State240, Config, 1.0f / 240.0f) == HMM_SPRING_OK);
    }

    /* All frame rates settle at the same steady state, namely the target. */
    EXPECT_NEAR(State30.P, Config.Rest, Epsilon);
    EXPECT_NEAR(State60.P, Config.Rest, Epsilon);
    EXPECT_NEAR(State240.P, Config.Rest, Epsilon);

    EXPECT_NEAR(State30.V, 0.0f, Epsilon);
    EXPECT_NEAR(State60.V, 0.0f, Epsilon);
    EXPECT_NEAR(State240.V, 0.0f, Epsilon);

    /* Pairwise deviation between the three integration paths. */
    EXPECT_NEAR(State30.P, State60.P, Epsilon);
    EXPECT_NEAR(State60.P, State240.P, Epsilon);
    EXPECT_NEAR(State30.P, State240.P, Epsilon);
}

TEST(Spring, OverdampedApproachesRest)
{
    HMM_Spring State = { 0.0f, 0.0f };
    HMM_SpringConfig Config = { 100.0f, 100.0f, 4.0f };
    int Index;

    for (Index = 0; Index < 600; ++Index)
    {
        EXPECT_TRUE(HMM_SpringStep(&State, Config, 1.0f / 60.0f) == HMM_SPRING_OK);
    }

    EXPECT_NEAR(State.P, Config.Rest, 0.01f);
    EXPECT_NEAR(State.V, 0.0f, 0.01f);
}
