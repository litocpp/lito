#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

static_assert(lito::hash::fnv1a64(""_str.as_bytes()) == u64(0xcbf29ce484222325ull));
static_assert(lito::hash::fnv1a64("foobar"_str.as_bytes()) == u64(0x85944171f73967e8ull));
static_assert([] {
    auto state = lito::hash::Fnv1a64 {};
    state.write_zero_terminated("foo"_str);
    return state.finish() == u64(0xdd1270790c25b935ull);
}());
static_assert(rstd::Impled<lito::hash::Fnv1a64, rstd::hash::Hasher>);

TEST(CoreHash, FixedByteVectors) {
    EXPECT_EQ(lito::hash::fnv1a64(""_str.as_bytes()), u64(0xcbf29ce484222325ull));
    EXPECT_EQ(lito::hash::fnv1a64("a"_str.as_bytes()), u64(0xaf63dc4c8601ec8cull));
    EXPECT_EQ(lito::hash::fnv1a64("foobar"_str.as_bytes()), u64(0x85944171f73967e8ull));
    EXPECT_EQ(lito::hash::fnv1a64("中文"_str.as_bytes()), u64(0x514dcc99d8cadec5ull));
    EXPECT_EQ(lito::hash::fnv1a64("a\0b"_str.as_bytes()), u64(0xe5d29919042666b2ull));
}

TEST(CoreHash, StreamingFinishPreservesState) {
    auto state = lito::hash::Fnv1a64 {};
    state.write("foo"_str.as_bytes());
    EXPECT_EQ(state.finish(), u64(0xdcb27518fed9d577ull));
    EXPECT_EQ(state.finish(), u64(0xdcb27518fed9d577ull));
    state.write(""_str.as_bytes());
    state.write("bar"_str.as_bytes());
    EXPECT_EQ(state.finish(), lito::hash::fnv1a64("foobar"_str.as_bytes()));
}

TEST(CoreHash, ZeroTerminatedTextPreservesExistingEncoding) {
    auto state = lito::hash::Fnv1a64 {};
    state.write_zero_terminated(""_str);
    EXPECT_EQ(state.finish(), u64(0xaf63bd4c8601b7dfull));
    state = lito::hash::Fnv1a64 {};
    state.write_zero_terminated("foo"_str);
    EXPECT_EQ(state.finish(), u64(0xdd1270790c25b935ull));
    state.write_zero_terminated("bar"_str);
    EXPECT_EQ(state.finish(), u64(0x5863d9458c1038deull));
    state = lito::hash::Fnv1a64 {};
    state.write_zero_terminated("bar"_str);
    state.write_zero_terminated("foo"_str);
    EXPECT_EQ(state.finish(), u64(0x067357ab98b9732aull));
}

TEST(CoreHash, HexIsFixedWidthLowercase) {
    auto state = lito::hash::Fnv1a64 {};
    EXPECT_EQ(state.hex64().as_str(), "cbf29ce484222325"_str);
    state.write_zero_terminated("bar"_str);
    state.write_zero_terminated("foo"_str);
    EXPECT_EQ(state.hex64().as_str(), "067357ab98b9732a"_str);
}

TEST(CoreHash, HexPreservesState) {
    auto state = lito::hash::Fnv1a64 {};
    state.write("foo"_str.as_bytes());
    const auto& snapshot = state;
    EXPECT_EQ(snapshot.hex64().as_str(), "dcb27518fed9d577"_str);
    EXPECT_EQ(snapshot.hex64().as_str(), "dcb27518fed9d577"_str);
    EXPECT_EQ(snapshot.finish(), u64(0xdcb27518fed9d577ull));
    state.write("bar"_str.as_bytes());
    EXPECT_EQ(snapshot.hex64().as_str(), "85944171f73967e8"_str);
}

TEST(CoreHash, SupportsRstdHasherProtocol) {
    auto state = lito::hash::Fnv1a64 {};
    rstd::as<rstd::hash::Hasher>(state).write("foobar"_str.as_bytes());
    EXPECT_EQ(rstd::as<rstd::hash::Hasher>(state).finish(), u64(0x85944171f73967e8ull));
}

TEST(CoreHash, PreservesCacheRecipeEncoding) {
    auto state = lito::hash::Fnv1a64 {};
    state.write_zero_terminated("lito-context-key-v1"_str);
    state.write_zero_terminated("context"_str);
    EXPECT_EQ(state.hex64().as_str(), "80dc3c81bcf4dc80"_str);
}
