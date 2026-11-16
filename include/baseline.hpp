#pragma once

#include <concepts>

#include "indicators/core.hpp"

template <typename T>
concept BaselineLine = BarIndicator<T> && requires(const T& t)
{
    {t.value()} -> std::convertible_to<double>;
};
