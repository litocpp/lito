#include <rstd/test/gtest.hpp>

import rstd;
import lito.crypto;
import lito.core;

using namespace rstd::literals;

TEST(Crypto, Sha256MatchesPublishedVectors) {
    EXPECT_EQ(lito::crypto::sha256_hex(""_str).as_str(),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_str);
    EXPECT_EQ(lito::crypto::sha256_hex("abc"_str).as_str(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"_str);
    EXPECT_EQ(
        lito::crypto::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"_str)
            .as_str(),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"_str);
}

TEST(Crypto, Sha256IncrementalMatchesOneShot) {
    constexpr auto input    = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"_str;
    auto           expected = lito::crypto::sha256_hex(input);

    rstd::usize chunk_sizes[] { rstd::usize(1), rstd::usize(7), rstd::usize(64) };
    for (auto chunk_size : chunk_sizes) {
        auto state = lito::crypto::Sha256::make();
        for (rstd::usize offset {}; offset < input.len(); offset += chunk_size) {
            auto remaining = input.len() - offset;
            auto count     = remaining < chunk_size ? remaining : chunk_size;
            state.update(rstd::slice<rstd::u8>::from_raw_parts(
                input.as_bytes().as_raw_ptr() + offset.to_primitive(), count));
        }
        EXPECT_EQ(lito::crypto::sha256_hex(rstd::move(state).finalize()), expected);
    }
}

TEST(Crypto, Sha256IncrementalCoversPaddingBoundaries) {
    rstd::usize lengths[] { rstd::usize(),   rstd::usize(55), rstd::usize(56),
                            rstd::usize(63), rstd::usize(64), rstd::usize(65) };
    for (auto length : lengths) {
        auto input = rstd::vec::Vec<rstd::u8>::with_capacity(length);
        for (rstd::usize index {}; index < length; ++index) {
            input.push(rstd::u8(index.to_primitive() & 0xffu));
        }
        auto state = lito::crypto::Sha256::make();
        auto split = length / rstd::usize(2);
        state.update(rstd::slice<rstd::u8>::from_raw_parts(input.as_ptr(), split));
        state.update(rstd::slice<rstd::u8>::from_raw_parts(input.as_ptr() + split.to_primitive(),
                                                           length - split));
        EXPECT_EQ(lito::crypto::sha256_hex(rstd::move(state).finalize()),
                  lito::crypto::sha256_hex(input.as_slice()));
    }
}

TEST(Crypto, Sha256DigestParsesAndFormatsCanonicalHex) {
    constexpr auto lower = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str;
    constexpr auto upper = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"_str;

    auto parsed       = lito::crypto::Sha256Digest::parse_hex(upper);
    auto lower_parsed = lito::crypto::Sha256Digest::parse_hex(lower);
    auto trait_parsed = rstd::from_str<lito::crypto::Sha256Digest>(lower);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(lower_parsed.is_ok());
    ASSERT_TRUE(trait_parsed.is_ok());
    EXPECT_EQ(parsed->to_hex(), lower);
    EXPECT_EQ(rstd::format("{}", *parsed), lower);
    EXPECT_EQ(*parsed, *lower_parsed);
    EXPECT_EQ(*parsed, *trait_parsed);
    EXPECT_TRUE(lito::crypto::Sha256Digest::parse_hex("abc"_str).is_err());
    EXPECT_TRUE(lito::crypto::Sha256Digest::parse_hex(
                    "z123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str)
                    .is_err());
}

TEST(Crypto, Sha256DigestSupportsOneShotAndIncrementalHashing) {
    auto one_shot = lito::crypto::sha256_digest("abc"_str);
    auto state    = lito::crypto::Sha256::make();
    state.update("a"_str.as_bytes());
    state.update("bc"_str.as_bytes());
    auto incremental = rstd::move(state).finalize_digest();

    EXPECT_EQ(one_shot, incremental);
    EXPECT_EQ(one_shot.to_hex(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"_str);

    auto clone  = one_shot.clone();
    auto first  = rstd::hash::DefaultHasher(rstd::u64(3), rstd::u64(5));
    auto second = rstd::hash::DefaultHasher(rstd::u64(3), rstd::u64(5));
    rstd::hash::hash_into(one_shot, first);
    rstd::hash::hash_into(clone, second);
    EXPECT_EQ(first.finish(), second.finish());
}
