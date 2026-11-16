#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "baseline.hpp"
#include "flip_strategy.hpp"
#include "multi_backtester.hpp"

// FlipStrategy<B> is always in the market, once per asset:
//   close above the baseline line -> long,  close below -> short,
//   and it reverses (close + open) whenever the close crosses to the other side.
// It never uses a stop or a target, and it sizes every trade as notional / close.
//
// The tests use two stand-in baselines instead of real indicators:
//   FixedLine    a flat line at a chosen price, ready from the first bar
//   LaggedClose  the previous bar's close, ready from the second bar

namespace
{
class FixedLine
{
public:
    explicit FixedLine(double level) : level_(level) {}

    void update(const Bar&) {}
    bool ready() const { return true; }
    double value() const { return level_; }

private:
    double level_;
};

class LaggedClose
{
public:
    void update(const Bar& bar)
    {
        if (hasPrev_)
        {
            value_ = prev_;
            ready_ = true;
        }
        prev_ = bar.close;
        hasPrev_ = true;
    }

    bool ready() const { return ready_; }

    double value() const
    {
        if (!ready_)
            throw std::logic_error("not ready");   // like Atr: asking too early is a bug
        return value_;
    }

private:
    double prev_{};
    double value_{};
    bool hasPrev_{false};
    bool ready_{false};
};

// Types that must NOT count as baselines.
struct NoValue      { void update(const Bar&) {} bool ready() const { return true; } };
struct NoReady      { void update(const Bar&) {} double value() const { return 1; } };
struct TextValue    { void update(const Bar&) {} bool ready() const { return true; } std::string value() const { return ""; } };

Bar barAt(double close, std::int64_t time)
{
    Bar b;
    b.timestamp = time;
    b.open = b.high = b.low = b.close = close;
    return b;
}

Snapshot snapshotOf(std::vector<std::optional<double>> closes, std::int64_t time = 0)
{
    Snapshot s;
    s.timestamp = time;
    for (const auto& c : closes)
        s.bars.push_back(c ? std::optional<Bar>(barAt(*c, time)) : std::nullopt);
    return s;
}

// rows[day][asset] = that asset's close on that day (nullopt = no bar).
AlignedData dataOf(std::vector<std::vector<std::optional<double>>> rows)
{
    AlignedData d;
    const std::size_t assets = rows.empty() ? 0 : rows[0].size();
    for (std::size_t a = 0; a < assets; ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < rows.size(); ++i)
        d.snapshots.push_back(snapshotOf(rows[i], static_cast<std::int64_t>(i)));
    return d;
}

// One asset, closes in order.
AlignedData oneAsset(std::vector<double> closes)
{
    std::vector<std::vector<std::optional<double>>> rows;
    for (double c : closes)
        rows.push_back({c});
    return dataOf(rows);
}

const double kCash = 100000.0;
}

static_assert(BaselineLine<FixedLine>);
static_assert(BaselineLine<LaggedClose>);

// ---------- BaselineLine concept ----------

TEST(BaselineLineConceptTest, AcceptsAnIndicatorWithAValue)
{
    EXPECT_TRUE(BaselineLine<FixedLine>);
    EXPECT_TRUE(BaselineLine<LaggedClose>);
}

TEST(BaselineLineConceptTest, RejectsATypeWithoutValue)
{
    EXPECT_FALSE(BaselineLine<NoValue>);
}

TEST(BaselineLineConceptTest, RejectsATypeThatIsNotABarIndicator)
{
    EXPECT_FALSE(BaselineLine<NoReady>);
}

TEST(BaselineLineConceptTest, RejectsAValueThatIsNotANumber)
{
    EXPECT_FALSE(BaselineLine<TextValue>);
}

TEST(BaselineLineConceptTest, RejectsPlainTypes)
{
    EXPECT_FALSE(BaselineLine<double>);
}

// ---------- constructor ----------

TEST(FlipStrategyTest, RejectsANotionalThatIsNotPositive)
{
    EXPECT_THROW(FlipStrategy<FixedLine>(1, FixedLine(100), 0.0), std::invalid_argument);
    EXPECT_THROW(FlipStrategy<FixedLine>(1, FixedLine(100), -5.0), std::invalid_argument);
}

// ---------- the orders it returns (one snapshot at a time) ----------

TEST(FlipStrategyOrdersTest, OpensALongWhenTheCloseIsAboveTheLine)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({105}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
    EXPECT_EQ(orders[0].direction, Direction::Long);
    EXPECT_EQ(orders[0].asset, 0);
}

TEST(FlipStrategyOrdersTest, OpensAShortWhenTheCloseIsBelowTheLine)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({95}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
    EXPECT_EQ(orders[0].direction, Direction::Short);
}

TEST(FlipStrategyOrdersTest, DoesNothingWhenFlatAndTheCloseIsExactlyOnTheLine)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);

    EXPECT_TRUE(s.onSnapshot(snapshotOf({100}), pf).empty());
}

TEST(FlipStrategyOrdersTest, SizesTheTradeAsNotionalOverClose)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));   // default notional is 10000
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({125}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 10000.0 / 125.0);
}

TEST(FlipStrategyOrdersTest, UsesTheNotionalItWasGiven)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100), 5000.0);
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({125}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 5000.0 / 125.0);
}

TEST(FlipStrategyOrdersTest, NeverSetsAStopOrATarget)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({105}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_FALSE(orders[0].stopLoss.has_value());
    EXPECT_FALSE(orders[0].takeProfit.has_value());
}

TEST(FlipStrategyOrdersTest, DoesNothingWhileTheBaselineIsNotReady)
{
    FlipStrategy<LaggedClose> s(1, LaggedClose{});
    Portfolio pf(kCash);

    // First bar: LaggedClose has no previous close yet. value() would throw if it were asked.
    EXPECT_NO_THROW({
        EXPECT_TRUE(s.onSnapshot(snapshotOf({50}), pf).empty());
    });
}

TEST(FlipStrategyOrdersTest, ClosesTheOpenTradeThenOpensTheOppositeOneOnACross)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);
    int id = pf.open(Direction::Long, 10, 105, 0);        // we are long

    std::vector<Order> orders = s.onSnapshot(snapshotOf({95}), pf);   // now below the line

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].type, OrderType::Close);          // close first ...
    EXPECT_EQ(orders[0].tradeId, id);
    EXPECT_EQ(orders[1].type, OrderType::Open);           // ... then open the other way
    EXPECT_EQ(orders[1].direction, Direction::Short);
}

TEST(FlipStrategyOrdersTest, DoesNothingWhileAlreadyOnTheRightSide)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);
    pf.open(Direction::Long, 10, 105, 0);

    EXPECT_TRUE(s.onSnapshot(snapshotOf({110}), pf).empty());
}

TEST(FlipStrategyOrdersTest, HoldsTheTradeWhenTheCloseIsExactlyOnTheLine)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);
    pf.open(Direction::Short, 10, 95, 0);

    EXPECT_TRUE(s.onSnapshot(snapshotOf({100}), pf).empty());
}

// ---------- several assets ----------

TEST(FlipStrategyOrdersTest, DecidesEachAssetSeparately)
{
    FlipStrategy<FixedLine> s(2, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({105, 95}), pf);

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].asset, 0);
    EXPECT_EQ(orders[0].direction, Direction::Long);
    EXPECT_EQ(orders[1].asset, 1);
    EXPECT_EQ(orders[1].direction, Direction::Short);
}

TEST(FlipStrategyOrdersTest, SkipsAnAssetThatHasNoBarToday)
{
    FlipStrategy<FixedLine> s(2, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({std::nullopt, 95}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].asset, 1);
}

TEST(FlipStrategyOrdersTest, ATradeOnAnAssetWithNoBarTodayIsLeftAlone)
{
    FlipStrategy<FixedLine> s(2, FixedLine(100));
    Portfolio pf(kCash);
    pf.open(Direction::Long, 10, 105, 0, std::nullopt, std::nullopt, 0);

    // asset 0 has no bar today, so even though it would be "below the line" nothing happens to it
    EXPECT_TRUE(s.onSnapshot(snapshotOf({std::nullopt, std::nullopt}), pf).empty());
}

TEST(FlipStrategyOrdersTest, AnOpenTradeIsOnlyMatchedToItsOwnAsset)
{
    FlipStrategy<FixedLine> s(2, FixedLine(100));
    Portfolio pf(kCash);
    pf.open(Direction::Long, 10, 105, 0, std::nullopt, std::nullopt, 1);   // long on asset 1 only

    // asset 0 is flat and above the line: it must open, not be confused by asset 1's trade
    std::vector<Order> orders = s.onSnapshot(snapshotOf({105, 110}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
    EXPECT_EQ(orders[0].asset, 0);
}

TEST(FlipStrategyOrdersTest, IgnoresExtraSlotsBeyondTheAssetsItWasBuiltFor)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));
    Portfolio pf(kCash);

    std::vector<Order> orders = s.onSnapshot(snapshotOf({105, 95}), pf);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].asset, 0);
}

TEST(FlipStrategyOrdersTest, EachAssetHasItsOwnBaselineState)
{
    FlipStrategy<LaggedClose> s(2, LaggedClose{});
    Portfolio pf(kCash);

    s.onSnapshot(snapshotOf({10, 20}), pf);                                  // warm-up bar
    std::vector<Order> orders = s.onSnapshot(snapshotOf({11, 19}), pf);      // up vs 10, down vs 20

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].direction, Direction::Long);
    EXPECT_EQ(orders[1].direction, Direction::Short);
}

TEST(FlipStrategyOrdersTest, AssetsBecomeReadyIndependently)
{
    FlipStrategy<LaggedClose> s(2, LaggedClose{});
    Portfolio pf(kCash);

    s.onSnapshot(snapshotOf({10, std::nullopt}), pf);
    std::vector<Order> orders = s.onSnapshot(snapshotOf({11, 20}), pf);   // asset 1 has only 1 bar so far

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].asset, 0);
}

// ---------- running it through the real engine ----------

TEST(FlipStrategyRunTest, ReversesOnEveryCrossAndStaysInTheMarket)
{
    // line 100; closes: above, above, below, below, above
    FlipStrategy<FixedLine> s(1, FixedLine(100));

    BacktestResult r = runMultiBacktest(oneAsset({101, 102, 99, 98, 103}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 2u);
    ASSERT_EQ(r.openTrades.size(), 1u);

    const ClosedTrade& first = r.closedTrades[0];
    EXPECT_EQ(first.trade.direction, Direction::Long);
    EXPECT_DOUBLE_EQ(first.trade.entryPrice, 101);
    EXPECT_DOUBLE_EQ(first.exitPrice, 99);
    EXPECT_EQ(first.reason, ExitReason::Signal);
    EXPECT_NEAR(first.pnl, (99 - 101) * (10000.0 / 101), 1e-9);

    const ClosedTrade& second = r.closedTrades[1];
    EXPECT_EQ(second.trade.direction, Direction::Short);
    EXPECT_DOUBLE_EQ(second.trade.entryPrice, 99);
    EXPECT_DOUBLE_EQ(second.exitPrice, 103);
    EXPECT_NEAR(second.pnl, -(103 - 99) * (10000.0 / 99), 1e-9);

    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 103);
}

TEST(FlipStrategyRunTest, FinalEquityIsCashPlusClosedPnl)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));

    BacktestResult r = runMultiBacktest(oneAsset({101, 102, 99, 98, 103}), s, kCash);

    // the last trade was opened at the last close, so it has no profit or loss yet
    const double expected = kCash + (99 - 101) * (10000.0 / 101) - (103 - 99) * (10000.0 / 99);
    EXPECT_NEAR(r.finalEquity, expected, 1e-9);
}

TEST(FlipStrategyRunTest, NeverHoldsMoreThanOneTradePerAsset)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));

    BacktestResult r = runMultiBacktest(oneAsset({101, 99, 101, 99, 101, 99}), s, kCash);

    EXPECT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades.size(), 5u);
}

TEST(FlipStrategyRunTest, ACloseExactlyOnTheLineKeepsTheCurrentTrade)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));

    BacktestResult r = runMultiBacktest(oneAsset({101, 100, 101}), s, kCash);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(FlipStrategyRunTest, StartsTradingOnlyOnceTheBaselineIsReady)
{
    // LaggedClose is ready from bar 2. Closes 10, 11, 12, 10, 9:
    //   bar 2: 11 > 10 -> long @ 11      bar 3: 12 > 11 -> hold
    //   bar 4: 10 < 12 -> close long @ 10, open short @ 10      bar 5: 9 < 10 -> hold
    FlipStrategy<LaggedClose> s(1, LaggedClose{});

    BacktestResult r = runMultiBacktest(oneAsset({10, 11, 12, 10, 9}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].trade.entryPrice, 11);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 10);
    EXPECT_NEAR(r.closedTrades[0].pnl, -(10000.0 / 11), 1e-9);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);

    // short 1000 units from 10, marked at 9: +1000
    EXPECT_NEAR(r.finalEquity, kCash - 10000.0 / 11 + 1000.0, 1e-9);
}

TEST(FlipStrategyRunTest, TwoAssetsTradeIndependently)
{
    // asset 0 rises above 100, asset 1 falls below 100
    FlipStrategy<FixedLine> s(2, FixedLine(100));

    BacktestResult r = runMultiBacktest(dataOf({{105, 95}, {110, 90}}), s, kCash);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 0);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_EQ(r.openTrades[1].asset, 1);
    EXPECT_EQ(r.openTrades[1].direction, Direction::Short);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(FlipStrategyRunTest, AnAssetThatMissesADayKeepsItsTrade)
{
    FlipStrategy<FixedLine> s(2, FixedLine(100));

    BacktestResult r = runMultiBacktest(
        dataOf({{105, 105}, {std::nullopt, 95}, {105, 95}}), s, kCash);

    // asset 1 flips on day 2; asset 0 missed day 2 but is still long
    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].trade.asset, 1);
    ASSERT_EQ(r.openTrades.size(), 2u);
}

TEST(FlipStrategyRunTest, NoDataMeansNoTrades)
{
    FlipStrategy<FixedLine> s(1, FixedLine(100));

    BacktestResult r = runMultiBacktest(oneAsset({}), s, kCash);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_TRUE(r.openTrades.empty());
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash);
}
