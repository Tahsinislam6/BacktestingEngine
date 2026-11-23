#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "density.hpp"

// These tests define the density helpers. Every expected value was worked out by hand on tiny grids.
//
// A grid is stored flat, in row-major order: the LAST axis changes fastest. For shape {3, 3} the cell at row r, column c
// is index r * 3 + c, so the indexes are laid out like this:
//
//     0 1 2
//     3 4 5
//     6 7 8
//
// A cell's neighbourhood is every cell at most `radius` steps away on EVERY axis (a square / cube), including the cell
// itself. Near an edge it is cut off (cells that do not exist are not counted).
//
// passDensity     = (valid cells with value > threshold) / (all cells)
// plateauDensity  = (cells whose whole neighbourhood is valid and > threshold) / (all cells)
// peakOverNeighbours = (best valid value) - (median of the valid values around it, not counting itself)

namespace
{
// The 3x3 grid used in most tests:  1 2 3 / 4 5 6 / 7 8 9
DensityGrid ninesGrid()
{
    DensityGrid g;
    g.shape = {3, 3};
    g.values = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    g.valid = std::vector<bool>(9, true);
    return g;
}
}

// ---------- shape helpers ----------

TEST(DensityShape, CellCountIsTheProductOfTheAxisLengths)
{
    EXPECT_EQ(cellCount({3, 3}), 9u);
    EXPECT_EQ(cellCount({5}), 5u);
    EXPECT_EQ(cellCount({2, 3, 4}), 24u);
}

TEST(DensityShape, EmptyShapeOrAnEmptyAxisIsRejected)
{
    EXPECT_THROW(cellCount({}), std::invalid_argument);
    EXPECT_THROW(cellCount({3, 0}), std::invalid_argument);
}

TEST(DensityShape, IndexAndCoordinatesAreInverses)
{
    // shape {2,3,4}: coordinates (1,2,3) -> 1*12 + 2*4 + 3 = 23, the last cell
    const std::vector<std::size_t> shape = {2, 3, 4};
    EXPECT_EQ(indexOf(shape, {1, 2, 3}), 23u);
    EXPECT_EQ(indexOf(shape, {0, 0, 0}), 0u);
    EXPECT_EQ(indexOf(shape, {0, 1, 2}), 6u);   // 0*12 + 1*4 + 2
    EXPECT_EQ(coordsOf(shape, 23), (std::vector<std::size_t>{1, 2, 3}));
    EXPECT_EQ(coordsOf(shape, 6), (std::vector<std::size_t>{0, 1, 2}));
    for (std::size_t i = 0; i < 24; ++i)
        EXPECT_EQ(indexOf(shape, coordsOf(shape, i)), i);
}

TEST(DensityShape, CoordinatesOutOfRangeAreRejected)
{
    EXPECT_THROW(indexOf({3, 3}, {3, 0}), std::invalid_argument);
    EXPECT_THROW(indexOf({3, 3}, {0}), std::invalid_argument);
    EXPECT_THROW(coordsOf({3, 3}, 9), std::invalid_argument);
}

// ---------- neighbourhoods ----------

TEST(DensityNeighbourhood, RadiusZeroIsJustTheCell)
{
    EXPECT_EQ(neighbourhood({3, 3}, 4, 0), (std::vector<std::size_t>{4}));
}

TEST(DensityNeighbourhood, CentreOfA3x3GridWithRadiusOneIsEveryCell)
{
    EXPECT_EQ(neighbourhood({3, 3}, 4, 1), (std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(DensityNeighbourhood, CornersAndEdgesAreCutOff)
{
    EXPECT_EQ(neighbourhood({3, 3}, 0, 1), (std::vector<std::size_t>{0, 1, 3, 4}));   // top-left corner
    EXPECT_EQ(neighbourhood({3, 3}, 8, 1), (std::vector<std::size_t>{4, 5, 7, 8}));   // bottom-right corner
    EXPECT_EQ(neighbourhood({3, 3}, 1, 1), (std::vector<std::size_t>{0, 1, 2, 3, 4, 5}));   // top edge
}

TEST(DensityNeighbourhood, OneAxis)
{
    EXPECT_EQ(neighbourhood({5}, 0, 1), (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(neighbourhood({5}, 2, 1), (std::vector<std::size_t>{1, 2, 3}));
    EXPECT_EQ(neighbourhood({5}, 2, 2), (std::vector<std::size_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(neighbourhood({5}, 4, 2), (std::vector<std::size_t>{2, 3, 4}));
}

TEST(DensityNeighbourhood, ThreeAxes)
{
    // shape {2,2,2}: every cell is within 1 step of every other
    EXPECT_EQ(neighbourhood({2, 2, 2}, 0, 1).size(), 8u);
    EXPECT_EQ(neighbourhood({2, 2, 2}, 0, 0), (std::vector<std::size_t>{0}));
    // shape {3,3,3}: the centre (index 13) sees all 27 cells, a corner (index 0) sees 2*2*2 = 8
    EXPECT_EQ(neighbourhood({3, 3, 3}, 13, 1).size(), 27u);
    EXPECT_EQ(neighbourhood({3, 3, 3}, 0, 1), (std::vector<std::size_t>{0, 1, 3, 4, 9, 10, 12, 13}));
}

TEST(DensityNeighbourhood, NegativeRadiusOrBadIndexIsRejected)
{
    EXPECT_THROW(neighbourhood({3, 3}, 4, -1), std::invalid_argument);
    EXPECT_THROW(neighbourhood({3, 3}, 9, 1), std::invalid_argument);
}

// ---------- passDensity ----------

TEST(PassDensity, CountsCellsAboveTheThreshold)
{
    // 1..9 with threshold 4.5: 5,6,7,8,9 pass -> 5/9
    EXPECT_NEAR(passDensity(ninesGrid(), 4.5), 5.0 / 9.0, 1e-12);
}

TEST(PassDensity, TheThresholdItselfDoesNotPass)
{
    // threshold 5: 6,7,8,9 pass (5 is not above 5) -> 4/9
    EXPECT_NEAR(passDensity(ninesGrid(), 5.0), 4.0 / 9.0, 1e-12);
}

TEST(PassDensity, AllOrNothing)
{
    EXPECT_DOUBLE_EQ(passDensity(ninesGrid(), 0.0), 1.0);
    EXPECT_DOUBLE_EQ(passDensity(ninesGrid(), 100.0), 0.0);
}

TEST(PassDensity, InvalidCellsNeverPassButStayInTheDenominator)
{
    DensityGrid g = ninesGrid();
    g.valid[8] = false;   // the 9 no longer counts: 5,6,7,8 pass -> 4/9, not 4/8
    EXPECT_NEAR(passDensity(g, 4.5), 4.0 / 9.0, 1e-12);
}

TEST(PassDensity, ANaNValueNeverPasses)
{
    DensityGrid g = ninesGrid();
    g.values[8] = std::nan("");
    EXPECT_NEAR(passDensity(g, 4.5), 4.0 / 9.0, 1e-12);
}

TEST(PassDensity, MismatchedOrEmptyGridsAreRejected)
{
    DensityGrid g = ninesGrid();
    g.values.pop_back();
    EXPECT_THROW(passDensity(g, 0.0), std::invalid_argument);

    DensityGrid h = ninesGrid();
    h.valid.pop_back();
    EXPECT_THROW(passDensity(h, 0.0), std::invalid_argument);

    EXPECT_THROW(passDensity(DensityGrid{}, 0.0), std::invalid_argument);
}

// ---------- plateauDensity ----------

TEST(PlateauDensity, RadiusZeroIsThePassDensity)
{
    EXPECT_NEAR(plateauDensity(ninesGrid(), 4.5, 0), passDensity(ninesGrid(), 4.5), 1e-12);
}

TEST(PlateauDensity, OnlyCellsWhoseWholeNeighbourhoodPassCount)
{
    // threshold 4.5, radius 1. Passing cells are 5,6,7,8,9 (indexes 4..8). Checking each:
    //   idx 8 (the 9): neighbourhood 5,6,8,9 -> all above 4.5 -> counts
    //   idx 7 (the 8): neighbourhood includes the 4 -> no
    //   idx 6 (the 7): neighbourhood 4,5,7,8 -> includes the 4 -> no
    //   idx 5 (the 6): neighbourhood includes 2,3 -> no
    //   idx 4 (the 5): neighbourhood is everything, includes 1 -> no
    // so 1/9
    EXPECT_NEAR(plateauDensity(ninesGrid(), 4.5, 1), 1.0 / 9.0, 1e-12);
}

TEST(PlateauDensity, ALowerThresholdGrowsThePlateau)
{
    // threshold 3.5: cells 4..9 pass. Whole-neighbourhood passes:
    //   idx 6 (the 7): neighbourhood 4,5,7,8 -> yes
    //   idx 7 (the 8): neighbourhood 4,5,6,7,8,9 -> yes
    //   idx 8 (the 9): neighbourhood 5,6,8,9 -> yes
    //   idx 3 (the 4): neighbourhood includes 1 -> no; idx 4, 5 include 1 / 2 / 3 -> no
    // so 3/9
    EXPECT_NEAR(plateauDensity(ninesGrid(), 3.5, 1), 3.0 / 9.0, 1e-12);
}

TEST(PlateauDensity, ALoneSpikeHasNoPlateau)
{
    DensityGrid g;
    g.shape = {3, 3};
    g.values = {0, 0, 0, 0, 10, 0, 0, 0, 0};
    g.valid = std::vector<bool>(9, true);
    EXPECT_NEAR(passDensity(g, 1.0), 1.0 / 9.0, 1e-12);   // looks like something...
    EXPECT_DOUBLE_EQ(plateauDensity(g, 1.0, 1), 0.0);     // ...but it is not
}

TEST(PlateauDensity, AnInvalidNeighbourBreaksThePlateau)
{
    // All cells are 10 (all pass), but the top-left is invalid.
    DensityGrid g;
    g.shape = {3, 3};
    g.values = std::vector<double>(9, 10.0);
    g.valid = std::vector<bool>(9, true);
    g.valid[0] = false;
    // The cells whose neighbourhood contains idx 0 are idx 0, 1, 3, 4 -> 4 cells fail. The other 5 pass.
    EXPECT_NEAR(plateauDensity(g, 1.0, 1), 5.0 / 9.0, 1e-12);
}

TEST(PlateauDensity, WorksWithOneAxis)
{
    DensityGrid g;
    g.shape = {5};
    g.values = {5, 5, 0, 5, 5};
    g.valid = std::vector<bool>(5, true);
    // radius 1: idx 0 sees {0,1} yes; idx 1 sees {0,1,2} no; idx 2 no; idx 3 sees {2,3,4} no; idx 4 sees {3,4} yes -> 2/5
    EXPECT_NEAR(plateauDensity(g, 1.0, 1), 2.0 / 5.0, 1e-12);
}

TEST(PlateauDensity, WorksWithThreeAxes)
{
    DensityGrid g;
    g.shape = {2, 2, 2};
    g.values = {1, 1, 1, 1, 1, 1, 1, 0};   // one failing cell, and every cell is a neighbour of every other
    g.valid = std::vector<bool>(8, true);
    EXPECT_DOUBLE_EQ(plateauDensity(g, 0.5, 1), 0.0);   // everyone sees the 0
    EXPECT_NEAR(plateauDensity(g, 0.5, 0), 7.0 / 8.0, 1e-12);
}

TEST(PlateauDensity, NegativeRadiusIsRejected)
{
    EXPECT_THROW(plateauDensity(ninesGrid(), 0.0, -1), std::invalid_argument);
}

// ---------- peakOverNeighbours ----------

TEST(PeakOverNeighbours, BestCellMinusTheMedianOfItsNeighbours)
{
    // 1..9: the best is the 9 (idx 8). Its neighbours (not itself) are 5, 6, 8 -> median 6 -> 9 - 6 = 3
    EXPECT_NEAR(peakOverNeighbours(ninesGrid(), 1), 3.0, 1e-12);
}

TEST(PeakOverNeighbours, ASpikeStandsOut)
{
    DensityGrid g;
    g.shape = {3, 3};
    g.values = {1, 1, 1, 1, 10, 1, 1, 1, 1};
    g.valid = std::vector<bool>(9, true);
    EXPECT_NEAR(peakOverNeighbours(g, 1), 9.0, 1e-12);   // 10 - median(eight 1s)
}

TEST(PeakOverNeighbours, EvenNumberOfNeighboursUsesTheMiddlePair)
{
    DensityGrid g;
    g.shape = {3, 3};
    g.values = {1, 2, 3, 4, 10, 6, 7, 8, 9};
    g.valid = std::vector<bool>(9, true);
    // neighbours 1,2,3,4,6,7,8,9 -> sorted, middle pair 4 and 6 -> median 5 -> 10 - 5 = 5
    EXPECT_NEAR(peakOverNeighbours(g, 1), 5.0, 1e-12);
}

TEST(PeakOverNeighbours, InvalidCellsAreIgnored)
{
    DensityGrid g = ninesGrid();
    g.valid[8] = false;   // the 9 is out. The best valid cell is the 8 (idx 7).
    // its neighbours are idx 3,4,5,6,8 minus itself; idx 8 is invalid, so 4,5,6,7 -> median 5.5 -> 8 - 5.5 = 2.5
    EXPECT_NEAR(peakOverNeighbours(g, 1), 2.5, 1e-12);
}

TEST(PeakOverNeighbours, TiesGoToTheFirstCell)
{
    DensityGrid g;
    g.shape = {4};
    g.values = {5, 1, 3, 5};
    g.valid = std::vector<bool>(4, true);
    // Two cells share the best value 5. The first (idx 0) wins: its only neighbour is the 1 -> 5 - 1 = 4.
    // (Had the last one won, its neighbour would be the 3 -> 2. The numbers differ on purpose so a wrong pick shows.)
    EXPECT_NEAR(peakOverNeighbours(g, 1), 4.0, 1e-12);
}

TEST(PeakOverNeighbours, NotANumberWhenThereIsNothingToCompare)
{
    DensityGrid none = ninesGrid();
    none.valid = std::vector<bool>(9, false);
    EXPECT_TRUE(std::isnan(peakOverNeighbours(none, 1)));      // no valid cell

    DensityGrid one;
    one.shape = {1};
    one.values = {3.0};
    one.valid = {true};
    EXPECT_TRUE(std::isnan(peakOverNeighbours(one, 1)));       // no neighbours

    EXPECT_TRUE(std::isnan(peakOverNeighbours(ninesGrid(), 0)));   // radius 0 means no neighbours either
}

// ---------- percentile ----------

TEST(Percentile, LinearInterpolationBetweenSortedValues)
{
    const std::vector<double> v = {4, 1, 3, 2};   // unsorted on purpose
    EXPECT_DOUBLE_EQ(percentile(v, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(percentile(v, 1.0), 4.0);
    EXPECT_DOUBLE_EQ(percentile(v, 0.5), 2.5);
    // position = 0.95 * (4 - 1) = 2.85 -> between sorted[2] = 3 and sorted[3] = 4 -> 3.85
    EXPECT_NEAR(percentile(v, 0.95), 3.85, 1e-12);
}

TEST(Percentile, SingleValue)
{
    EXPECT_DOUBLE_EQ(percentile({7.0}, 0.3), 7.0);
}

TEST(Percentile, BadInputIsRejected)
{
    EXPECT_THROW(percentile({}, 0.5), std::invalid_argument);
    EXPECT_THROW(percentile({1.0, 2.0}, -0.1), std::invalid_argument);
    EXPECT_THROW(percentile({1.0, 2.0}, 1.1), std::invalid_argument);
}
