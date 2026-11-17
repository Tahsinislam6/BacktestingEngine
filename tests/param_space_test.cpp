#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "param_space.hpp"

// Params = one set of named values. expandGrid turns named ranges into every combination.

namespace
{
std::vector<double> column(const std::vector<Params>& grid, const std::string& name)
{
    std::vector<double> out;
    for (const Params& p : grid)
        out.push_back(p.get(name));
    return out;
}
}

// ---------- Params ----------

TEST(ParamsTest, StartsEmpty)
{
    Params p;

    EXPECT_TRUE(p.values().empty());
    EXPECT_FALSE(p.has("period"));
}

TEST(ParamsTest, CanBeBuiltFromAList)
{
    Params p{{"period", 20}, {"atrMultiple", 1.5}};

    EXPECT_TRUE(p.has("period"));
    EXPECT_DOUBLE_EQ(p.get("period"), 20);
    EXPECT_DOUBLE_EQ(p.get("atrMultiple"), 1.5);
}

TEST(ParamsTest, AnUnknownNameThrows)
{
    Params p{{"period", 20}};

    EXPECT_THROW(p.get("nope"), std::invalid_argument);
    EXPECT_THROW(p.getInt("nope"), std::invalid_argument);
}

TEST(ParamsTest, GetIntRoundsToTheNearestWholeNumber)
{
    Params p{{"a", 20.0}, {"b", 19.9999999}, {"c", 20.4}, {"d", 20.6}};

    EXPECT_EQ(p.getInt("a"), 20);
    EXPECT_EQ(p.getInt("b"), 20);     // grid arithmetic can be a hair off
    EXPECT_EQ(p.getInt("c"), 20);
    EXPECT_EQ(p.getInt("d"), 21);
}

TEST(ParamsTest, SetAddsOrReplaces)
{
    Params p;

    p.set("x", 1);
    p.set("x", 2);

    EXPECT_DOUBLE_EQ(p.get("x"), 2);
    EXPECT_EQ(p.values().size(), 1u);
}

TEST(ParamsTest, EqualWhenTheyHoldTheSameValues)
{
    EXPECT_EQ((Params{{"a", 1}, {"b", 2}}), (Params{{"b", 2}, {"a", 1}}));
    EXPECT_NE((Params{{"a", 1}}), (Params{{"a", 2}}));
    EXPECT_NE((Params{{"a", 1}}), (Params{{"b", 1}}));
}

// ---------- expandGrid: one range ----------

TEST(ExpandGridTest, ARangeIncludesBothEnds)
{
    const std::vector<Params> grid = expandGrid({{"period", 1, 3, 1}});

    EXPECT_EQ(column(grid, "period"), (std::vector<double>{1, 2, 3}));
}

TEST(ExpandGridTest, TheUpperEndIsLeftOutWhenItIsNotOnAStep)
{
    EXPECT_EQ(column(expandGrid({{"x", 1, 3, 1.5}}), "x"), (std::vector<double>{1, 2.5}));
    EXPECT_EQ(column(expandGrid({{"x", 5, 100, 10}}), "x").back(), 95);
}

TEST(ExpandGridTest, FractionalStepsDoNotLoseTheLastValue)
{
    const std::vector<double> v = column(expandGrid({{"x", 0, 1, 0.1}}), "x");

    ASSERT_EQ(v.size(), 11u);
    EXPECT_NEAR(v[3], 0.3, 1e-12);
    EXPECT_NEAR(v.back(), 1.0, 1e-12);
    EXPECT_LE(v.back(), 1.0);          // and never goes past `to`
}

TEST(ExpandGridTest, RoundingErrorDoesNotDropTheLastValue)
{
    // 0.3 / 0.1 is 2.9999999999999996 in floating point, but 0.3 is still a step
    const std::vector<double> v = column(expandGrid({{"x", 0, 0.3, 0.1}}), "x");

    ASSERT_EQ(v.size(), 4u);
    EXPECT_NEAR(v.back(), 0.3, 1e-12);
    EXPECT_LE(v.back(), 0.3);
}

TEST(ExpandGridTest, FromEqualToGivesOneValue)
{
    EXPECT_EQ(column(expandGrid({{"x", 7, 7, 1}}), "x"), (std::vector<double>{7}));
}

TEST(ExpandGridTest, ValuesAreComputedFromTheStartNotAccumulated)
{
    const std::vector<double> v = column(expandGrid({{"x", 0, 1000, 0.1}}), "x");

    EXPECT_EQ(v.size(), 10001u);
    EXPECT_NEAR(v[5000], 500.0, 1e-9);
}

// ---------- expandGrid: several ranges ----------

TEST(ExpandGridTest, GivesEveryCombination)
{
    const std::vector<Params> grid = expandGrid({{"a", 1, 2, 1}, {"b", 10, 30, 10}});

    EXPECT_EQ(grid.size(), 6u);
}

TEST(ExpandGridTest, TheLastRangeChangesFastest)
{
    const std::vector<Params> grid = expandGrid({{"a", 1, 2, 1}, {"b", 10, 30, 10}});

    EXPECT_EQ(column(grid, "a"), (std::vector<double>{1, 1, 1, 2, 2, 2}));
    EXPECT_EQ(column(grid, "b"), (std::vector<double>{10, 20, 30, 10, 20, 30}));
}

TEST(ExpandGridTest, EveryCombinationIsDifferent)
{
    const std::vector<Params> grid = expandGrid({{"a", 1, 4, 1}, {"b", 1, 5, 1}, {"c", 0, 1, 1}});

    EXPECT_EQ(grid.size(), 40u);
    for (std::size_t i = 0; i < grid.size(); ++i)
        for (std::size_t j = i + 1; j < grid.size(); ++j)
            EXPECT_NE(grid[i], grid[j]);
}

TEST(ExpandGridTest, NoRangesGivesOneEmptyParamSet)
{
    const std::vector<Params> grid = expandGrid({});

    ASSERT_EQ(grid.size(), 1u);
    EXPECT_TRUE(grid[0].values().empty());
}

// ---------- expandGrid: bad input ----------

TEST(ExpandGridTest, RejectsAStepThatIsNotPositive)
{
    EXPECT_THROW(expandGrid({{"x", 1, 3, 0}}), std::invalid_argument);
    EXPECT_THROW(expandGrid({{"x", 1, 3, -1}}), std::invalid_argument);
    EXPECT_THROW(expandGrid({{"x", 1, 3, std::nan("")}}), std::invalid_argument);
}

TEST(ExpandGridTest, RejectsAnUpperEndBelowTheLowerEnd)
{
    EXPECT_THROW(expandGrid({{"x", 3, 1, 1}}), std::invalid_argument);
}

TEST(ExpandGridTest, RejectsAnEmptyName)
{
    EXPECT_THROW(expandGrid({{"", 1, 3, 1}}), std::invalid_argument);
}

TEST(ExpandGridTest, RejectsANameUsedTwice)
{
    EXPECT_THROW(expandGrid({{"x", 1, 3, 1}, {"x", 5, 6, 1}}), std::invalid_argument);
}

// ---------- parseParams: the parameters given on the command line ----------
// "period=26,mult=2.5" -> one Params. Every parameter of the baseline must be named once, no others, each inside its range.

TEST(ParseParamsTest, ReadsNamesAndValues)
{
    const Params p = parseParams("period=26,mult=2.5", {{"period", 5, 200, 1}, {"mult", 1, 5, 0.5}});

    EXPECT_DOUBLE_EQ(p.get("period"), 26.0);
    EXPECT_DOUBLE_EQ(p.get("mult"), 2.5);
}

TEST(ParseParamsTest, ToleratesSpacesAndTheOrderOfNames)
{
    const Params p = parseParams(" mult = 2.5 , period=26 ", {{"period", 5, 200, 1}, {"mult", 1, 5, 0.5}});

    EXPECT_DOUBLE_EQ(p.get("period"), 26.0);
    EXPECT_DOUBLE_EQ(p.get("mult"), 2.5);
}

TEST(ParseParamsTest, TheEndsOfTheRangeAreAllowed)
{
    EXPECT_NO_THROW(parseParams("period=5", {{"period", 5, 200, 1}}));
    EXPECT_NO_THROW(parseParams("period=200", {{"period", 5, 200, 1}}));
}

TEST(ParseParamsTest, ARangeThatDoesNotLandOnTheStepStillAllowsItsEnd)
{
    EXPECT_NO_THROW(parseParams("x=2.9", {{"x", 1, 2.9, 0.5}}));
}

TEST(ParseParamsTest, ABaselineWithoutParametersTakesNoText)
{
    EXPECT_TRUE(parseParams("", {}).values().empty());
    EXPECT_THROW(parseParams("period=3", {}), std::invalid_argument);
}

TEST(ParseParamsTest, EveryParameterMustBeGiven)
{
    try
    {
        parseParams("period=26", {{"period", 5, 200, 1}, {"mult", 1, 5, 0.5}});
        FAIL() << "expected an exception";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("mult"), std::string::npos);      // names what is missing
    }
    EXPECT_THROW(parseParams("", {{"period", 5, 200, 1}}), std::invalid_argument);
}

TEST(ParseParamsTest, AnUnknownNameIsRejectedAndNamed)
{
    try
    {
        parseParams("period=26,speed=2", {{"period", 5, 200, 1}});
        FAIL() << "expected an exception";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("speed"), std::string::npos);
    }
}

TEST(ParseParamsTest, ANameGivenTwiceIsRejected)
{
    EXPECT_THROW(parseParams("period=26,period=27", {{"period", 5, 200, 1}}), std::invalid_argument);
}

TEST(ParseParamsTest, AValueOutsideTheRangeIsRejected)
{
    EXPECT_THROW(parseParams("period=4", {{"period", 5, 200, 1}}), std::invalid_argument);
    EXPECT_THROW(parseParams("period=201", {{"period", 5, 200, 1}}), std::invalid_argument);
}

TEST(ParseParamsTest, MalformedTextIsRejected)
{
    const std::vector<ParamRange> r{{"period", 5, 200, 1}};
    for (const char* bad : {"period", "period=", "=26", "period=abc", "period=26x", "period=26,", ",period=26",
                            "period==26", "period=nan", "period=inf"})
        EXPECT_THROW(parseParams(bad, r), std::invalid_argument) << bad;
}
