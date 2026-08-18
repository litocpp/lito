export module lito.test.cpp;

import rstd;
import lito.core;
import lito.cpp;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;

export namespace lito_test
{

template<typename... Values>
auto strings(Values... values) -> Vec<String> {
    auto result = Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

template<typename... Values>
auto paths(Values... values) -> Vec<PathBuf> {
    auto result = Vec<PathBuf>::with_capacity(usize(sizeof...(Values)));
    (result.push(PathBuf::from(values)), ...);
    return result;
}

auto argument_layer(Vec<String> options) -> cpp::CppArgumentLayer {
    auto parser = make_clang_cpp_argument_parser();
    if (parser.is_err()) return cpp::CppArgumentLayer {};
    auto parsed = parser->parse(options, "cpp test"_str);
    return parsed.is_ok() ? rstd::move(parsed).unwrap() : cpp::CppArgumentLayer {};
}

auto cpp_options(ref<str>                     standard,
                 lito::manifest::Optimization optimization,
                 lito::manifest::DebugInfo    debug_info,
                 Vec<String>                  options = {}) -> cpp::CppCompileOptions {
    auto result = cpp::make_cpp_options(standard,
                                        lito::config::StandardLibrary::Libcxx,
                                        false,
                                        false,
                                        optimization,
                                        debug_info,
                                        cpp::CppOptionLayer {
                                            .arguments = argument_layer(rstd::move(options)),
                                        });
    if (result.is_err()) return cpp::CppCompileOptions {};
    return rstd::move(result).unwrap();
}

auto format(ref<str> build = "clang-build-a"_str) -> cpp::BmiFormatIdentity {
    return cpp::BmiFormatIdentity {
        .family               = String::make("clang"_str),
        .compiler_build       = String::make(build),
        .target               = String::make("x86_64-unknown-linux-gnu"_str),
        .resource_environment = String::make("resource-a"_str),
    };
}

auto artifact_key(cpp::BmiRepresentation        representation,
                  cpp::BmiSourceEmbeddingPolicy embedding,
                  ref<str>                      dependency,
                  ref<str> source_content = "source-content-a"_str) -> cpp::BmiArtifactKey {
    auto dependencies = Vec<cpp::BmiRecipeDependency>::make();
    if (! dependency.is_empty()) {
        dependencies.push(cpp::BmiRecipeDependency {
            .logical_name = String::make("dependency"_str),
            .artifact_key = String::make(dependency),
        });
    }
    return cpp::make_bmi_artifact_key(cpp::BmiRecipe {
        .request =
            cpp::BmiRequest {
                .representation   = representation,
                .source_embedding = embedding,
            },
        .logical_name                 = String::make("sample"_str),
        .provider_identity            = String::make("package:source.cppm"_str),
        .source_identity              = String::make("/source/source.cppm"_str),
        .source_content_identity      = String::make(source_content),
        .cpp_context_identity         = String::make("cpp-context"_str),
        .public_requirements_identity = String::make("public-requirements"_str),
        .format_identity              = String::make("clang-format"_str),
        .direct_dependencies          = rstd::move(dependencies),
    });
}

auto has_argument(const Vec<String>& arguments, ref<str> expected) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str() == expected) return true;
    }
    return false;
}

auto has_prefix(const Vec<String>& arguments, ref<str> prefix) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str().starts_with(prefix)) return true;
    }
    return false;
}

} // namespace lito_test
