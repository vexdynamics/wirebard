#include "json.h"

#include <gtest/gtest.h>

namespace {

TEST(JsonTest, QuotesAndEscapes) {
    EXPECT_EQ(wirebard::json_quote("hello"), "\"hello\"");
    EXPECT_EQ(wirebard::json_quote("a\"b"), "\"a\\\"b\"");    // embedded quote
    EXPECT_EQ(wirebard::json_quote("a\\b"), "\"a\\\\b\"");    // backslash
    EXPECT_EQ(wirebard::json_quote("l1\nl2"), "\"l1\\nl2\""); // newline (client_config!)
    EXPECT_EQ(wirebard::json_quote("\t"), "\"\\t\"");
}

TEST(JsonTest, EscapesControlCharsAsUnicode) {
    EXPECT_EQ(wirebard::json_quote(std::string_view("\x01", 1)), "\"\\u0001\"");
}

TEST(JsonTest, PassesUtf8Through) {
    // The em dash is multibyte UTF-8; valid JSON keeps it verbatim.
    EXPECT_EQ(wirebard::json_quote("a—b"), "\"a—b\"");
}

TEST(JsonTest, ObjectPreservesInsertionOrder) {
    std::string out = wirebard::JsonObject{}.str("b", "1").str("a", "2").boolean("ok", true).dump();
    EXPECT_EQ(out, R"({"b":"1","a":"2","ok":true})");
}

TEST(JsonTest, RawEmbedsVerbatimAndDumpIsRepeatable) {
    wirebard::JsonObject o;
    o.raw("nested", R"({"x":1})").str("s", "hi");
    EXPECT_EQ(o.dump(), R"({"nested":{"x":1},"s":"hi"})");
    EXPECT_EQ(o.dump(), o.dump()); // const, repeatable
}

TEST(JsonTest, ArrayJoinsElements) {
    wirebard::JsonArray a;
    a.push(R"({"n":1})").push(R"({"n":2})");
    EXPECT_EQ(a.dump(), R"([{"n":1},{"n":2}])");
    EXPECT_EQ(wirebard::JsonArray{}.dump(), "[]");
}

} // namespace
