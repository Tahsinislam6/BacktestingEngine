#include "engine.hpp"

double percentReturn(double entry, double exit) {
    return (exit - entry) / entry * 100.0;
}
