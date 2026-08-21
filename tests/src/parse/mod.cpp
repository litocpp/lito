#include <rstd/test/gtest.hpp>

import rstd;
import rstd.json;
import rstd.toml;
import lito.core;

using namespace rstd::prelude;
using namespace rstd::literals;

TEST(Parse, TracksJsonAndTomlNodePaths) {
    auto json = rstd::json::from_str(R"json({"items":[{"name":"value"}]})json"_str);
    ASSERT_TRUE(json.is_ok());
    auto catalog_path = lito::parse::NodePath::root("catalog"_str);
    auto items        = lito::parse::json::required_member(*json, "items"_str, catalog_path);
    ASSERT_TRUE(items.is_ok());
    auto items_path = catalog_path.field("items"_str);
    auto array      = lito::parse::json::array(**items, items_path);
    ASSERT_TRUE(array.is_ok());
    auto item_path = items_path.index(usize {});
    auto name      = lito::parse::json::required_member((**array)[usize {}], "name"_str, item_path);
    ASSERT_TRUE(name.is_ok());
    auto name_path = item_path.field("name"_str);
    EXPECT_EQ(rstd::format("{}", name_path), "catalog.items[0].name"_str);

    auto missing =
        lito::parse::json::required_member((**array)[usize {}], "missing"_str, item_path);
    ASSERT_TRUE(missing.is_err());
    auto error = rstd::move(missing).unwrap_err();
    ASSERT_TRUE(error.is_MissingField());
    EXPECT_EQ(rstd::format("{}", error.as_MissingField().node), "catalog.items[0]"_str);
    EXPECT_EQ(error.as_MissingField().field, "missing"_str);

    auto toml = rstd::toml::from_str("[build]\noptions = [\"-O2\", 1]\n"_str);
    ASSERT_TRUE(toml.is_ok());
    auto config_path = lito::parse::NodePath::root("config"_str);
    auto build       = lito::parse::toml::required_member(*toml, "build"_str, config_path);
    ASSERT_TRUE(build.is_ok());
    auto build_path = config_path.field("build"_str);
    auto options    = lito::parse::toml::required_member(**build, "options"_str, build_path);
    ASSERT_TRUE(options.is_ok());
    auto options_path = build_path.field("options"_str);
    auto values       = lito::parse::toml::array(**options, options_path);
    ASSERT_TRUE(values.is_ok());
    auto text = lito::parse::toml::string((**values)[usize(1)], options_path.index(usize(1)));
    ASSERT_TRUE(text.is_err());
    auto type_error = rstd::move(text).unwrap_err();
    ASSERT_TRUE(type_error.is_WrongType());
    EXPECT_EQ(rstd::format("{}", type_error.as_WrongType().node), "config.build.options[1]"_str);
}

TEST(Parse, SeparatesRequiredAndNonEmptyStrings) {
    auto json = rstd::json::from_str(R"json({"value":""})json"_str);
    ASSERT_TRUE(json.is_ok());
    const auto path = lito::parse::NodePath::root("document"_str);

    auto required = lito::parse::json::required_string(*json, "value"_str, path);
    ASSERT_TRUE(required.is_ok());
    EXPECT_TRUE(required->is_empty());
    EXPECT_TRUE(lito::parse::json::required_non_empty_string(*json, "value"_str, path).is_err());

    auto value = lito::parse::json::required_member(*json, "value"_str, path);
    ASSERT_TRUE(value.is_ok());
    EXPECT_TRUE(lito::parse::json::non_empty_string(**value, path.field("value"_str)).is_err());
}

TEST(Parse, ParsesCanonicalSha256Values) {
    constexpr auto lower = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_str;
    constexpr auto mixed = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF"_str;

    auto canonical = lito::parse::parse_sha256(lower, lito::parse::Sha256TextMode::Canonical);
    ASSERT_TRUE(canonical.is_ok());
    EXPECT_EQ(canonical->to_hex(), lower);

    auto normalized = lito::parse::parse_sha256(mixed, lito::parse::Sha256TextMode::Flexible);
    ASSERT_TRUE(normalized.is_ok());
    EXPECT_EQ(normalized->to_hex(), lower);
    EXPECT_TRUE(lito::parse::parse_sha256(mixed, lito::parse::Sha256TextMode::Canonical).is_err());
    EXPECT_TRUE(
        lito::parse::parse_sha256("abc"_str, lito::parse::Sha256TextMode::Flexible).is_err());
}

TEST(Parse, SeparatesUrlSyntaxFetchAndHttpsPolicies) {
    auto url = lito::parse::Url::parse("HTTPS://example.com/archive.tar.xz?download=1"_str);
    ASSERT_TRUE(url.is_ok());
    EXPECT_EQ(url->scheme(), "https"_str);
    EXPECT_EQ(url->authority(), "example.com"_str);
    EXPECT_EQ(url->path(), "/archive.tar.xz"_str);

    auto fetch = lito::parse::FetchUrl::parse(url->as_str());
    ASSERT_TRUE(fetch.is_ok());
    auto https = lito::parse::HttpsUrl::parse(fetch->as_str());
    ASSERT_TRUE(https.is_ok());
    EXPECT_EQ(https->url()->authority(), "example.com"_str);

    EXPECT_TRUE(lito::parse::FetchUrl::parse("file:///tmp/archive.tar"_str).is_ok());
    EXPECT_TRUE(lito::parse::FetchUrl::parse("https://example.com/a#fragment"_str).is_err());
    EXPECT_TRUE(lito::parse::HttpsUrl::parse("http://example.com/a"_str).is_err());
    EXPECT_TRUE(lito::parse::Url::parse("https://example.com/%zz"_str).is_err());
}

TEST(Parse, ParsesPathAndDecimalValues) {
    auto relative = lito::parse::NormalRelativePath::parse("lib/cmake/file.json"_str);
    ASSERT_TRUE(relative.is_ok());
    EXPECT_EQ(relative->as_path().to_str(), Some("lib/cmake/file.json"_str));

    auto component = lito::parse::PathComponent::parse("archive-root"_str);
    ASSERT_TRUE(component.is_ok());
    EXPECT_EQ(component->as_str(), Some("archive-root"_str));
    EXPECT_TRUE(lito::parse::NormalRelativePath::parse("../escape"_str).is_err());
    EXPECT_TRUE(lito::parse::PathComponent::parse("two/components"_str).is_err());

    auto maximum = lito::parse::parse_canonical_u64_decimal("18446744073709551615"_str);
    ASSERT_TRUE(maximum.is_ok());
    EXPECT_EQ(*maximum, u64::MAX);
    EXPECT_TRUE(lito::parse::parse_canonical_u64_decimal("01"_str).is_err());
    EXPECT_TRUE(lito::parse::parse_canonical_u64_decimal("18446744073709551616"_str).is_err());
}
