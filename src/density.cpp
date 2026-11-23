#include "density.hpp"

#include <algorithm>
#include <stdexcept>

std::size_t cellCount(const std::vector<std::size_t>& shape)
{
    if (shape.empty())
        throw std::invalid_argument("the grid needs at least one axis");
    std::size_t count = 1;
    for (std::size_t length : shape)
    {
        if (length == 0)
            throw std::invalid_argument("an axis of the grid has no values");
        count *= length;
    }
    return count;
}

std::size_t indexOf(const std::vector<std::size_t>& shape, const std::vector<std::size_t>& coords)
{
    cellCount(shape);   // checks the shape
    if (coords.size() != shape.size())
        throw std::invalid_argument("need one coordinate per axis");

    // Row-major: walk the axes from the first to the last, each step multiplying by that axis's length.
    std::size_t index = 0;
    for (std::size_t a = 0; a < shape.size(); ++a)
    {
        if (coords[a] >= shape[a])
            throw std::invalid_argument("coordinate out of range");
        index = index * shape[a] + coords[a];
    }
    return index;
}

std::vector<std::size_t> coordsOf(const std::vector<std::size_t>& shape, std::size_t index)
{
    if (index >= cellCount(shape))
        throw std::invalid_argument("cell index out of range");

    // Peel off the last axis first (it changes fastest): the remainder is its coordinate, the quotient carries on.
    std::vector<std::size_t> coords(shape.size());
    for (std::size_t a = shape.size(); a-- > 0;)
    {
        coords[a] = index % shape[a];
        index /= shape[a];
    }
    return coords;
}

std::vector<std::size_t> neighbourhood(const std::vector<std::size_t>& shape, std::size_t index, int radius)
{
    if (radius < 0)
        throw std::invalid_argument("radius cannot be negative");
    const std::vector<std::size_t> centre = coordsOf(shape, index);   // also checks the shape and the index

    // For each axis, the range of coordinates that are within `radius` of the centre and still inside the grid.
    const std::size_t r = static_cast<std::size_t>(radius);
    std::vector<std::size_t> lo(shape.size()), hi(shape.size());
    for (std::size_t a = 0; a < shape.size(); ++a)
    {
        lo[a] = centre[a] > r ? centre[a] - r : 0;
        hi[a] = std::min(shape[a] - 1, centre[a] + r);
    }

    // Visit every combination of coordinates in those ranges, like an odometer where the last axis turns fastest.
    // That visits the cells in ascending flat-index order.
    std::vector<std::size_t> result;
    std::vector<std::size_t> coords = lo;
    while (true)
    {
        result.push_back(indexOf(shape, coords));

        std::size_t a = shape.size();
        while (a-- > 0)
        {
            if (coords[a] < hi[a])
            {
                ++coords[a];
                break;
            }
            coords[a] = lo[a];
            if (a == 0)
                return result;
        }
    }
}

namespace
{
void check(const DensityGrid& grid)
{
    const std::size_t cells = cellCount(grid.shape);
    if (grid.values.size() != cells || grid.valid.size() != cells)
        throw std::invalid_argument("values and valid need one entry per cell");
}

// A cell can be judged only when it is valid and its value is a real number.
bool usable(const DensityGrid& grid, std::size_t i)
{
    return grid.valid[i] && !std::isnan(grid.values[i]);
}

bool passes(const DensityGrid& grid, std::size_t i, double threshold)
{
    return usable(grid, i) && grid.values[i] > threshold;
}
}

double passDensity(const DensityGrid& grid, double threshold)
{
    check(grid);
    std::size_t passing = 0;
    for (std::size_t i = 0; i < grid.values.size(); ++i)
        if (passes(grid, i, threshold))
            ++passing;
    return static_cast<double>(passing) / static_cast<double>(grid.values.size());
}

double plateauDensity(const DensityGrid& grid, double threshold, int radius)
{
    check(grid);
    if (radius < 0)
        throw std::invalid_argument("radius cannot be negative");

    std::size_t plateau = 0;
    for (std::size_t i = 0; i < grid.values.size(); ++i)
    {
        const std::vector<std::size_t> around = neighbourhood(grid.shape, i, radius);
        const bool all = std::all_of(around.begin(), around.end(),
                                     [&](std::size_t j) { return passes(grid, j, threshold); });
        if (all)
            ++plateau;
    }
    return static_cast<double>(plateau) / static_cast<double>(grid.values.size());
}

double peakOverNeighbours(const DensityGrid& grid, int radius)
{
    check(grid);

    // The best usable cell; `>` (not `>=`) keeps the first one on a tie.
    bool found = false;
    std::size_t best = 0;
    for (std::size_t i = 0; i < grid.values.size(); ++i)
    {
        if (usable(grid, i) && (!found || grid.values[i] > grid.values[best]))
        {
            best = i;
            found = true;
        }
    }
    if (!found)
        return std::nan("");

    std::vector<double> around;
    for (std::size_t j : neighbourhood(grid.shape, best, radius))
        if (j != best && usable(grid, j))
            around.push_back(grid.values[j]);
    if (around.empty())
        return std::nan("");

    return grid.values[best] - percentile(around, 0.5);
}

double percentile(std::vector<double> values, double q)
{
    if (values.empty())
        throw std::invalid_argument("percentile of nothing");
    if (!(q >= 0.0 && q <= 1.0))
        throw std::invalid_argument("q must be between 0 and 1");

    std::sort(values.begin(), values.end());
    const double position = q * static_cast<double>(values.size() - 1);
    const std::size_t below = static_cast<std::size_t>(position);
    const std::size_t above = std::min(below + 1, values.size() - 1);
    const double fraction = position - static_cast<double>(below);
    return values[below] + fraction * (values[above] - values[below]);
}
