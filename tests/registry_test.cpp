#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>

#include "registry.hpp"

// The registry is the table of strategies the job runner (and later the web app) can use: a name, a description, the
// parameters with their limits, and a function that builds a fresh strategy from a set of parameter values.
//
// Rules:
//   1. add() rejects an empty name, a repeated name, a missing builder, and a repeated or empty parameter name, or a
//      parameter whose default is outside its own min..max (all std::invalid_argument).
//   2. resolveParams() fills in the default of every parameter the caller left out, rejects an unknown name, a value
//      outside min..max, a value that is not a number, and a fractional value for an integer parameter.
//   3. create() resolves the parameters, then calls the builder with the full set; an unknown strategy throws.

namespace
{
class NullStrategy : public Strategy
{
public:
    std::vector<Order> onBar(const Bar&, const Portfolio&) override { return {}; }
};

StrategyInfo twoParamInfo()
{
    StrategyInfo info;
    info.name = "demo";
    info.description = "a demo";
    info.params = {
        {"lookback", 20, 2, 200, true, "bars to look back"},
        {"fraction", 0.5, 0, 1, false, "share of equity"},
    };
    return info;
}

StrategyBuilder nullBuilder()
{
    return [](const NamedParams&) { return std::make_unique<NullStrategy>(); };
}
}

TEST(Registry, AddedStrategyIsListedAndFound)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());

    ASSERT_EQ(registry.list().size(), 1u);
    EXPECT_EQ(registry.list()[0].name, "demo");
    EXPECT_TRUE(registry.has("demo"));
    EXPECT_FALSE(registry.has("other"));
}

TEST(Registry, AddRejectsEmptyName)
{
    StrategyRegistry registry;
    StrategyInfo info = twoParamInfo();
    info.name = "";
    EXPECT_THROW(registry.add(info, nullBuilder()), std::invalid_argument);
}

TEST(Registry, AddRejectsRepeatedName)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    EXPECT_THROW(registry.add(twoParamInfo(), nullBuilder()), std::invalid_argument);
}

TEST(Registry, AddRejectsMissingBuilder)
{
    StrategyRegistry registry;
    EXPECT_THROW(registry.add(twoParamInfo(), StrategyBuilder{}), std::invalid_argument);
}

TEST(Registry, AddRejectsRepeatedParameterName)
{
    StrategyRegistry registry;
    StrategyInfo info = twoParamInfo();
    info.params.push_back(info.params[0]);
    EXPECT_THROW(registry.add(info, nullBuilder()), std::invalid_argument);
}

TEST(Registry, AddRejectsEmptyParameterName)
{
    StrategyRegistry registry;
    StrategyInfo info = twoParamInfo();
    info.params[0].name = "";
    EXPECT_THROW(registry.add(info, nullBuilder()), std::invalid_argument);
}

TEST(Registry, AddRejectsDefaultOutsideLimits)
{
    StrategyRegistry registry;
    StrategyInfo info = twoParamInfo();
    info.params[1].defaultValue = 1.5;   // limits are 0..1
    EXPECT_THROW(registry.add(info, nullBuilder()), std::invalid_argument);
}

TEST(Registry, AddRejectsMinAboveMax)
{
    StrategyRegistry registry;
    StrategyInfo info = twoParamInfo();
    info.params[1].min = 2;
    info.params[1].max = 1;
    EXPECT_THROW(registry.add(info, nullBuilder()), std::invalid_argument);
}

TEST(Registry, ResolveFillsDefaultsForMissingParameters)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());

    const NamedParams p = registry.resolveParams("demo", {{"lookback", 50}});

    ASSERT_EQ(p.size(), 2u);
    EXPECT_DOUBLE_EQ(p.at("lookback"), 50);
    EXPECT_DOUBLE_EQ(p.at("fraction"), 0.5);
}

TEST(Registry, ResolveWithNothingGivenReturnsAllDefaults)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());

    const NamedParams p = registry.resolveParams("demo", {});

    EXPECT_DOUBLE_EQ(p.at("lookback"), 20);
    EXPECT_DOUBLE_EQ(p.at("fraction"), 0.5);
}

TEST(Registry, ResolveRejectsUnknownParameter)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    EXPECT_THROW(registry.resolveParams("demo", {{"lookbak", 50}}), std::invalid_argument);   // a typo is an error
}

TEST(Registry, ResolveRejectsValueOutsideLimits)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    EXPECT_THROW(registry.resolveParams("demo", {{"lookback", 1}}), std::invalid_argument);
    EXPECT_THROW(registry.resolveParams("demo", {{"lookback", 201}}), std::invalid_argument);
    EXPECT_THROW(registry.resolveParams("demo", {{"fraction", -0.1}}), std::invalid_argument);
}

TEST(Registry, ResolveAcceptsValuesExactlyOnTheLimits)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    EXPECT_NO_THROW(registry.resolveParams("demo", {{"lookback", 2}, {"fraction", 1}}));
    EXPECT_NO_THROW(registry.resolveParams("demo", {{"lookback", 200}, {"fraction", 0}}));
}

TEST(Registry, ResolveRejectsNotANumber)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(registry.resolveParams("demo", {{"fraction", nan}}), std::invalid_argument);
}

TEST(Registry, ResolveRejectsFractionForIntegerParameter)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());
    EXPECT_THROW(registry.resolveParams("demo", {{"lookback", 20.5}}), std::invalid_argument);
    EXPECT_NO_THROW(registry.resolveParams("demo", {{"lookback", 21.0}}));
}

TEST(Registry, ResolveRejectsUnknownStrategy)
{
    StrategyRegistry registry;
    EXPECT_THROW(registry.resolveParams("nope", {}), std::invalid_argument);
}

TEST(Registry, CreateCallsBuilderWithFullResolvedParameters)
{
    StrategyRegistry registry;
    NamedParams seen;
    registry.add(twoParamInfo(), [&seen](const NamedParams& p)
    {
        seen = p;
        return std::make_unique<NullStrategy>();
    });

    auto strategy = registry.create("demo", {{"lookback", 30}});

    EXPECT_NE(strategy, nullptr);
    EXPECT_DOUBLE_EQ(seen.at("lookback"), 30);
    EXPECT_DOUBLE_EQ(seen.at("fraction"), 0.5);
}

TEST(Registry, CreateGivesAFreshStrategyEachTime)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), nullBuilder());

    auto a = registry.create("demo", {});
    auto b = registry.create("demo", {});

    EXPECT_NE(a.get(), b.get());
}

TEST(Registry, CreateRejectsUnknownStrategy)
{
    StrategyRegistry registry;
    EXPECT_THROW(registry.create("nope", {}), std::invalid_argument);
}

TEST(Registry, CreateRejectsBuilderThatReturnsNothing)
{
    StrategyRegistry registry;
    registry.add(twoParamInfo(), [](const NamedParams&) { return std::unique_ptr<Strategy>(); });
    EXPECT_THROW(registry.create("demo", {}), std::runtime_error);
}
