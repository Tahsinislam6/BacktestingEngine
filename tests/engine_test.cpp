#include <gtest/gtest.h>
#include "engine.hpp"

TEST(PercentReturn, Gain) {
    EXPECT_DOUBLE_EQ(percentReturn(100.0, 110.0), 10.0);
}

TEST(PercentReturn, Loss) {
    EXPECT_DOUBLE_EQ(percentReturn(100.0, 90.0), -10.0);
}
