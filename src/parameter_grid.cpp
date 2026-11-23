#include "parameter_grid.hpp"

#include <cmath>
#include <stdexcept>

std::vector<std::size_t> gridShape(const std::vector<ParameterAxis>& axes)
{
    std::vector<std::size_t> shape;
    for (const ParameterAxis& axis : axes)
        shape.push_back(axis.values.size());
    cellCount(shape);   // throws if there is no axis or an axis is empty
    return shape;
}

GridResult sweep(const BarSeries& series, const std::vector<ParameterAxis>& axes, const StrategyFactory& factory,
                 double startingCash)
{
    if (!factory)
        throw std::invalid_argument("sweep needs a strategy factory");
    const std::vector<std::size_t> shape = gridShape(axes);

    GridResult result;
    result.axes = axes;
    const std::size_t cells = cellCount(shape);
    result.metrics.reserve(cells);

    for (std::size_t i = 0; i < cells; ++i)
    {
        // Cell i -> one coordinate per axis -> the value each axis has there.
        const std::vector<std::size_t> coords = coordsOf(shape, i);
        Params params(axes.size());
        for (std::size_t a = 0; a < axes.size(); ++a)
            params[a] = axes[a].values[coords[a]];

        std::unique_ptr<Strategy> strategy = factory(params);
        if (!strategy)
            throw std::invalid_argument("the strategy factory returned no strategy");

        const BacktestResult run = runBacktest(series, *strategy, startingCash);
        result.metrics.push_back(computeMetrics(run, startingCash));
    }
    return result;
}

DensityGrid toDensityGrid(const GridResult& result, const std::function<double(const Metrics&)>& metric, int minTrades)
{
    DensityGrid grid;
    grid.shape = gridShape(result.axes);
    if (result.metrics.size() != cellCount(grid.shape))
        throw std::invalid_argument("the result does not have one Metrics per cell");

    for (const Metrics& m : result.metrics)
    {
        const double value = metric(m);
        grid.values.push_back(value);
        grid.valid.push_back(m.trades >= minTrades && !std::isnan(value));
    }
    return grid;
}
