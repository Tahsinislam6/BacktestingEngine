#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "indicators/core.hpp"

// These tests cover the building blocks in include/indicators/core.hpp:
//
//   BarIndicator   a concept: a type needs update(const Bar&) and a ready() that converts to bool
//   WarmUp         counts bars; ready() once `barsNeeded` bars have been counted
//   PerAsset<Ind>  one Ind per asset; update(snapshot) feeds each asset only the bar it has today

namespace
{
Bar closeBar(double close)
{
    Bar b;
    b.open = b.high = b.low = b.close = close;
    return b;
}

// Snapshot with one slot per entry; std::nullopt = that asset has no bar today.
Snapshot snapshotOf(std::vector<std::optional<Bar>> bars)
{
    Snapshot s;
    s.bars = std::move(bars);
    return s;
}

// A minimal indicator: remembers every close it was given. Ready after two bars.
struct Recorder
{
    std::vector<double> closes;

    void update(const Bar& bar) { closes.push_back(bar.close); }
    bool ready() const { return closes.size() >= 2; }
};

// An indicator that uses WarmUp, the way Atr will.
class ThreeBarIndicator
{
public:
    ThreeBarIndicator() : warm_(3) {}

    void update(const Bar&) { warm_.tick(); }
    bool ready() const { return warm_.ready(); }

private:
    WarmUp warm_;
};

// Types that must NOT count as indicators.
struct NoReady { void update(const Bar&) {} };
struct NoUpdate { bool ready() const { return true; } };
struct WrongUpdateArgument { void update(int) {} bool ready() const { return true; } };
struct ReadyReturnsVector { void update(const Bar&) {} std::vector<int> ready() const { return {}; } };

// A type whose ready() returns int is still fine: int converts to bool.
struct ReadyReturnsInt { void update(const Bar&) {} int ready() const { return 1; } };
}

// ---------- BarIndicator concept ----------

TEST(BarIndicatorConceptTest, AcceptsAnythingWithUpdateAndReady)
{
    EXPECT_TRUE(BarIndicator<Recorder>);
    EXPECT_TRUE(BarIndicator<ThreeBarIndicator>);
}

TEST(BarIndicatorConceptTest, AcceptsAReadyThatConvertsToBool)
{
    EXPECT_TRUE(BarIndicator<ReadyReturnsInt>);
}

TEST(BarIndicatorConceptTest, RejectsATypeWithoutReady)
{
    EXPECT_FALSE(BarIndicator<NoReady>);
}

TEST(BarIndicatorConceptTest, RejectsATypeWithoutUpdate)
{
    EXPECT_FALSE(BarIndicator<NoUpdate>);
}

TEST(BarIndicatorConceptTest, RejectsAnUpdateThatDoesNotTakeABar)
{
    EXPECT_FALSE(BarIndicator<WrongUpdateArgument>);
}

TEST(BarIndicatorConceptTest, RejectsAReadyThatDoesNotConvertToBool)
{
    EXPECT_FALSE(BarIndicator<ReadyReturnsVector>);   // a vector cannot be turned into a bool
}

TEST(BarIndicatorConceptTest, RejectsPlainTypes)
{
    EXPECT_FALSE(BarIndicator<int>);
    EXPECT_FALSE(BarIndicator<double>);
}

// ---------- WarmUp ----------

TEST(WarmUpTest, IsNotReadyAtTheStart)
{
    WarmUp w(3);

    EXPECT_FALSE(w.ready());
}

TEST(WarmUpTest, IsNotReadyBeforeEnoughBars)
{
    WarmUp w(3);

    w.tick();
    EXPECT_FALSE(w.ready());
    w.tick();
    EXPECT_FALSE(w.ready());
}

TEST(WarmUpTest, IsReadyAfterExactlyTheBarsNeeded)
{
    WarmUp w(3);

    w.tick();
    w.tick();
    w.tick();

    EXPECT_TRUE(w.ready());
}

TEST(WarmUpTest, StaysReadyAfterMoreTicks)
{
    WarmUp w(3);

    for (int i = 0; i < 10; ++i)
        w.tick();

    EXPECT_TRUE(w.ready());
}

TEST(WarmUpTest, OneBarIsEnoughWhenOneIsNeeded)
{
    WarmUp w(1);

    EXPECT_FALSE(w.ready());
    w.tick();
    EXPECT_TRUE(w.ready());
}

TEST(WarmUpTest, ZeroBarsNeededIsReadyImmediately)
{
    WarmUp w(0);

    EXPECT_TRUE(w.ready());
}

TEST(WarmUpTest, LongWarmUpsWork)
{
    WarmUp w(34);   // e.g. MACD(12, 26, 9)

    for (int i = 0; i < 33; ++i)
        w.tick();
    EXPECT_FALSE(w.ready());

    w.tick();
    EXPECT_TRUE(w.ready());
}

TEST(WarmUpTest, CopiesCountSeparately)
{
    WarmUp a(2);
    a.tick();

    WarmUp b = a;       // b starts with a's one tick
    b.tick();           // b is now ready

    EXPECT_TRUE(b.ready());
    EXPECT_FALSE(a.ready());   // a is unaffected
}

// ---------- PerAsset ----------

TEST(PerAssetTest, CreatesOneIndicatorPerAsset)
{
    PerAsset<Recorder> p(3, Recorder{});

    EXPECT_TRUE(p[0].closes.empty());
    EXPECT_TRUE(p[1].closes.empty());
    EXPECT_TRUE(p[2].closes.empty());
}

TEST(PerAssetTest, FeedsEachAssetItsOwnBar)
{
    PerAsset<Recorder> p(3, Recorder{});

    p.update(snapshotOf({closeBar(10), closeBar(20), closeBar(30)}));

    ASSERT_EQ(p[0].closes.size(), 1u);
    ASSERT_EQ(p[1].closes.size(), 1u);
    ASSERT_EQ(p[2].closes.size(), 1u);
    EXPECT_DOUBLE_EQ(p[0].closes[0], 10);
    EXPECT_DOUBLE_EQ(p[1].closes[0], 20);
    EXPECT_DOUBLE_EQ(p[2].closes[0], 30);
}

TEST(PerAssetTest, SkipsAssetsWithNoBarToday)
{
    PerAsset<Recorder> p(3, Recorder{});

    p.update(snapshotOf({closeBar(10), std::nullopt, closeBar(30)}));

    EXPECT_EQ(p[0].closes.size(), 1u);
    EXPECT_EQ(p[1].closes.size(), 0u);
    EXPECT_EQ(p[2].closes.size(), 1u);
}

TEST(PerAssetTest, UpdatesAccumulateOverSnapshotsInOrder)
{
    PerAsset<Recorder> p(2, Recorder{});

    p.update(snapshotOf({closeBar(1), closeBar(100)}));
    p.update(snapshotOf({closeBar(2), std::nullopt}));
    p.update(snapshotOf({closeBar(3), closeBar(300)}));

    EXPECT_EQ(p[0].closes, (std::vector<double>{1, 2, 3}));
    EXPECT_EQ(p[1].closes, (std::vector<double>{100, 300}));
}

TEST(PerAssetTest, AssetsAreReadyIndependently)
{
    PerAsset<ThreeBarIndicator> p(2, ThreeBarIndicator{});

    // Asset 0 gets a bar every day; asset 1 only gets one on the first day.
    p.update(snapshotOf({closeBar(1), closeBar(1)}));
    p.update(snapshotOf({closeBar(2), std::nullopt}));
    p.update(snapshotOf({closeBar(3), std::nullopt}));

    EXPECT_TRUE(p[0].ready());
    EXPECT_FALSE(p[1].ready());
}

TEST(PerAssetTest, ADayWithoutABarDoesNotCountTowardWarmUp)
{
    PerAsset<ThreeBarIndicator> p(1, ThreeBarIndicator{});

    p.update(snapshotOf({closeBar(1)}));
    p.update(snapshotOf({std::nullopt}));
    p.update(snapshotOf({std::nullopt}));
    p.update(snapshotOf({closeBar(2)}));
    EXPECT_FALSE(p[0].ready());          // only two real bars so far

    p.update(snapshotOf({closeBar(3)}));
    EXPECT_TRUE(p[0].ready());
}

TEST(PerAssetTest, EveryAssetStartsFromTheSamePrototypeState)
{
    Recorder prototype;
    prototype.closes = {5, 6};

    PerAsset<Recorder> p(3, prototype);

    for (std::size_t a = 0; a < 3; ++a)
        EXPECT_EQ(p[a].closes, (std::vector<double>{5, 6}));
}

TEST(PerAssetTest, UpdatingOneAssetDoesNotChangeTheOthers)
{
    PerAsset<Recorder> p(2, Recorder{});

    p.update(snapshotOf({closeBar(1), std::nullopt}));

    EXPECT_EQ(p[0].closes.size(), 1u);
    EXPECT_TRUE(p[1].closes.empty());
}

TEST(PerAssetTest, ASnapshotWithFewerSlotsLeavesTheRestUntouched)
{
    PerAsset<Recorder> p(3, Recorder{});

    p.update(snapshotOf({closeBar(1)}));   // only asset 0 is described

    EXPECT_EQ(p[0].closes.size(), 1u);
    EXPECT_EQ(p[1].closes.size(), 0u);
    EXPECT_EQ(p[2].closes.size(), 0u);
}

TEST(PerAssetTest, ASnapshotWithMoreSlotsIgnoresTheExtraOnes)
{
    PerAsset<Recorder> p(1, Recorder{});

    p.update(snapshotOf({closeBar(1), closeBar(2), closeBar(3)}));

    ASSERT_EQ(p[0].closes.size(), 1u);
    EXPECT_DOUBLE_EQ(p[0].closes[0], 1);
}

TEST(PerAssetTest, ZeroAssetsIsFine)
{
    PerAsset<Recorder> p(0, Recorder{});

    EXPECT_NO_THROW(p.update(snapshotOf({closeBar(1)})));
}

TEST(PerAssetTest, ConstAccessCanReadReady)
{
    PerAsset<Recorder> p(1, Recorder{});
    p.update(snapshotOf({closeBar(1)}));
    p.update(snapshotOf({closeBar(2)}));

    const PerAsset<Recorder>& cp = p;

    EXPECT_TRUE(cp[0].ready());
}

TEST(PerAssetTest, IndexingGivesAReferenceNotACopy)
{
    PerAsset<Recorder> p(1, Recorder{});

    p[0].update(closeBar(7));

    ASSERT_EQ(p[0].closes.size(), 1u);
    EXPECT_DOUBLE_EQ(p[0].closes[0], 7);
}
