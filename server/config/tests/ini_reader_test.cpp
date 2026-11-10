#include "ini_reader.h"

#include <gtest/gtest.h>

#include <sstream>

TEST(IniReader, ReadsTrimmedValuesAndTypes) {
    std::istringstream input("  # comment\n[ mqtt ]\nenabled = true\nport = 1883\n");
    const IniReader config(input);

    EXPECT_TRUE(config.get_bool("mqtt", "enabled"));
    EXPECT_EQ(config.get_int("mqtt", "port"), 1883);
}

TEST(IniReader, RejectsDuplicateKeys) {
    std::istringstream input("[mqtt]\nport = 1883\nport = 1884\n");

    EXPECT_THROW(IniReader config(input), std::invalid_argument);
}

TEST(IniReader, RejectsMalformedEntries) {
    std::istringstream input("[mqtt]\nport 1883\n");

    EXPECT_THROW(IniReader config(input), std::invalid_argument);
}
