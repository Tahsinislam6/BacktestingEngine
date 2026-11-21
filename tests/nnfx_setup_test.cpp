#include <gtest/gtest.h>

#include <stdexcept>

#include "nnfx_setup.hpp"

// makeNnfxOptions turns the command-line options into the strategy options: it looks the named components up
// (with their default parameters) and copies the other settings across.
// Uses the real components rsi (a State) and atr_expansion (a Gate).

namespace
{
CliOptions cli()
{
    CliOptions o;
    o.inSampleDir = "x";
    return o;
}
}

TEST(NnfxSetupTest, WithNothingGivenTheOptionsAreTheDefaults)
{
    const NnfxOptions o = makeNnfxOptions(cli());

    EXPECT_TRUE(o.exitOnCross);
    EXPECT_EQ(o.components.confirmation, nullptr);
    EXPECT_EQ(o.components.exitIndicator, nullptr);
    EXPECT_EQ(o.components.filter, nullptr);
    EXPECT_EQ(o.confirmationWindow, 7);
    EXPECT_FALSE(o.oneCandleRule);
    EXPECT_FALSE(o.continuation);
    EXPECT_FALSE(o.maxOpenRisk.has_value());
    EXPECT_FALSE(o.maxPerCurrency.has_value());
}

TEST(NnfxSetupTest, NamedComponentsAreLookedUp)
{
    CliOptions c = cli();
    c.confirm = "rsi";
    c.exitIndicator = "rsi";
    c.filter = "atr_expansion";

    const NnfxOptions o = makeNnfxOptions(c);

    ASSERT_NE(o.components.confirmation, nullptr);
    ASSERT_NE(o.components.exitIndicator, nullptr);
    ASSERT_NE(o.components.filter, nullptr);
    EXPECT_EQ(o.components.confirmation->warmupBars(), 15u);          // RSI 14 needs 15 bars
    EXPECT_EQ(o.components.filter->warmupBars(), 50u);
    EXPECT_NE(o.components.confirmation, o.components.exitIndicator);  // separate instances
}

TEST(NnfxSetupTest, OtherSettingsAreCopied)
{
    CliOptions c = cli();
    c.noCrossExit = true;
    c.confirmWindow = 4;
    c.oneCandle = true;
    c.continuation = true;
    c.maxOpenRisk = 0.06;
    c.maxPerCurrency = 2;

    const NnfxOptions o = makeNnfxOptions(c);

    EXPECT_FALSE(o.exitOnCross);
    EXPECT_EQ(o.confirmationWindow, 4);
    EXPECT_TRUE(o.oneCandleRule);
    EXPECT_TRUE(o.continuation);
    EXPECT_DOUBLE_EQ(*o.maxOpenRisk, 0.06);
    EXPECT_EQ(*o.maxPerCurrency, 2);
}

TEST(NnfxSetupTest, NoCrossExitSwitchesTheCrossBackExitOff)
{
    CliOptions c = cli();
    c.noCrossExit = true;

    EXPECT_FALSE(makeNnfxOptions(c).exitOnCross);
}

TEST(NnfxSetupTest, AnUnknownComponentIsAnErrorThatListsTheAvailableOnes)
{
    CliOptions c = cli();
    c.confirm = "no_such_thing";

    try
    {
        makeNnfxOptions(c);
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("no_such_thing"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("rsi"), std::string::npos);
    }
}

TEST(NnfxSetupTest, AComponentOfTheWrongKindIsRejected)
{
    CliOptions gateAsConfirm = cli();
    gateAsConfirm.confirm = "atr_expansion";
    EXPECT_THROW(makeNnfxOptions(gateAsConfirm), std::invalid_argument);

    CliOptions gateAsExit = cli();
    gateAsExit.exitIndicator = "atr_expansion";
    EXPECT_THROW(makeNnfxOptions(gateAsExit), std::invalid_argument);

    CliOptions stateAsFilter = cli();
    stateAsFilter.filter = "rsi";
    EXPECT_THROW(makeNnfxOptions(stateAsFilter), std::invalid_argument);
}

TEST(NnfxSetupTest, ScalingOutIsOnUnlessSwitchedOff)
{
    EXPECT_TRUE(makeNnfxOptions(cli()).entry.scaleOut);

    CliOptions c = cli();
    c.noScaling = true;
    const NnfxOptions o = makeNnfxOptions(c);
    EXPECT_FALSE(o.entry.scaleOut);
    EXPECT_FALSE(o.breakeven);                    // there is no target to arm it
    ASSERT_TRUE(o.trailAtr.has_value());
}

TEST(NnfxSetupTest, TrailAfterMeansOneTradeNoBreakevenAndAStartThreshold)
{
    EXPECT_FALSE(makeNnfxOptions(cli()).trailAfterAtr.has_value());

    CliOptions c = cli();
    c.trailAfter = 2.0;
    const NnfxOptions o = makeNnfxOptions(c);
    ASSERT_TRUE(o.trailAfterAtr.has_value());
    EXPECT_DOUBLE_EQ(*o.trailAfterAtr, 2.0);
    EXPECT_FALSE(o.entry.scaleOut);
    EXPECT_FALSE(o.breakeven);
    ASSERT_TRUE(o.trailAtr.has_value());
    EXPECT_DOUBLE_EQ(*o.trailAtr, 1.5);
}

TEST(NnfxSetupTest, TheStopAtrPeriodRiskAndTrailFollowTheCommandLine)
{
    const NnfxOptions plain = makeNnfxOptions(cli());
    EXPECT_DOUBLE_EQ(plain.entry.stopAtr, 1.5);
    EXPECT_EQ(plain.atrPeriod, 14);
    EXPECT_DOUBLE_EQ(plain.entry.riskPerHalf, 0.01);
    EXPECT_TRUE(plain.trailAtr.has_value());

    CliOptions c = cli();
    c.stopAtr = 2.5;
    c.atrPeriod = 20;
    c.risk = 0.0025;
    c.noTrail = true;
    const NnfxOptions o = makeNnfxOptions(c);
    EXPECT_DOUBLE_EQ(o.entry.stopAtr, 2.5);
    EXPECT_EQ(o.atrPeriod, 20);
    EXPECT_DOUBLE_EQ(o.entry.riskPerHalf, 0.00125);          // the risk of the whole signal, split in two halves
    EXPECT_FALSE(o.trailAtr.has_value());
    EXPECT_FALSE(o.breakeven);
}

TEST(NnfxSetupTest, TheChaseLimitCanBeChangedOrRemoved)
{
    EXPECT_EQ(makeNnfxOptions(cli()).maxDistanceAtr, std::optional<double>(1.0));      // the default

    CliOptions wide = cli();
    wide.maxDistance = 4.0;
    EXPECT_EQ(makeNnfxOptions(wide).maxDistanceAtr, std::optional<double>(4.0));

    CliOptions off = cli();
    off.noChaseLimit = true;
    EXPECT_FALSE(makeNnfxOptions(off).maxDistanceAtr.has_value());
}

TEST(NnfxSetupTest, TheRiskIsTheWholeTradesWithNoScaling)
{
    CliOptions c = cli();
    c.risk = 0.0025;
    c.noScaling = true;
    const NnfxOptions o = makeNnfxOptions(c);
    const std::vector<Order> orders = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, o.entry);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_NEAR(orders[0].quantity * (o.entry.stopAtr * 2.0), 0.0025 * 100000, 1e-9);     // loses 0.25% if the stop is hit
}
