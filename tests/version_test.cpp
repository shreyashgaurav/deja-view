#include <gtest/gtest.h>
#include "dejaview/version.hpp"

TEST(Version, IsSet) {
    EXPECT_STREQ(dejaview::version(), "0.1.0");
}
