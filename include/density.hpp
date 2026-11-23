#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

// "Parametric density": how much of a strategy's parameter space gives good results. A strategy that only works at one
// exact setting is probably fitted to the past; one that works over a broad region is more believable.
//
// A grid is one number per combination of parameter values ("cell"), stored flat in row-major order: the LAST axis changes
// fastest. For shape {3, 3} the cell at row r, column c is index r * 3 + c:
//
//     0 1 2
//     3 4 5
//     6 7 8
//
// Any number of axes works (shape {2, 3, 4} is a 2x3x4 block of cells).
struct DensityGrid
{
    std::vector<std::size_t> shape;   // the number of values on each axis
    std::vector<double> values;       // one number per cell (e.g. the expectancy of that run)
    std::vector<bool> valid;          // false for a cell that should not count (e.g. too few trades to mean anything)
};

// ---- shape helpers (all throw std::invalid_argument on bad input) ----

// The number of cells: the product of the axis lengths. An empty shape or an axis of length 0 is an error.
std::size_t cellCount(const std::vector<std::size_t>& shape);

// Coordinates (one per axis) -> flat index, and back.
std::size_t indexOf(const std::vector<std::size_t>& shape, const std::vector<std::size_t>& coords);
std::vector<std::size_t> coordsOf(const std::vector<std::size_t>& shape, std::size_t index);

// The flat indexes (ascending) of every cell at most `radius` steps from `index` on EVERY axis, including `index` itself.
// Near an edge the neighbourhood is cut off. radius 0 is just the cell.
std::vector<std::size_t> neighbourhood(const std::vector<std::size_t>& shape, std::size_t index, int radius);

// ---- the density numbers ----

// (valid cells with a value above `threshold`) / (ALL cells). An invalid or not-a-number cell never passes but still
// counts in the denominator, so cells that could not be judged cannot make a grid look better.
double passDensity(const DensityGrid& grid, double threshold);

// (cells whose whole neighbourhood passes) / (all cells). A lone lucky cell scores nothing here, so this is the number
// that tells a broad plateau from a spike. radius 0 gives the same as passDensity.
double plateauDensity(const DensityGrid& grid, double threshold, int radius);

// The best valid cell's value minus the median of the valid values around it (not counting itself): a big number means
// the best cell is a spike. The first cell wins a tie. Not a number when there is no valid cell or nothing to compare with.
double peakOverNeighbours(const DensityGrid& grid, int radius);

// The q-th quantile (q from 0 to 1) of `values`, with linear interpolation between neighbours after sorting.
// Use it to read off e.g. the 95th percentile of the densities seen on shuffled data.
double percentile(std::vector<double> values, double q);
