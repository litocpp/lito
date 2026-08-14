export module lito.test.cpp;

import rstd;
import lito.error;
import lito.compiler.arguments;
import lito.cpp;
import lito.cpp.bmi;
import lito.package.target_contract;
import lito.build.plan_contract;
import lito.toolchain;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito;

export namespace lito_test
{

template<typename... Values>
auto strings(Values... values) -> lito::Vec<String> {
    auto result = lito::Vec<String>::with_capacity(usize(sizeof...(Values)));
    (result.push(String::make(values)), ...);
    return result;
}

template<typename... Values>
auto paths(Values... values) -> lito::Vec<PathBuf> {
    auto result = lito::Vec<PathBuf>::with_capacity(usize(sizeof...(Values)));
    (result.push(PathBuf::from(values)), ...);
    return result;
}

auto argument_layer(lito::Vec<String> options) -> CppArgumentLayer {
    auto parser = make_clang_cpp_argument_parser();
    if (parser.is_err()) return CppArgumentLayer {};
    auto parsed = parser->parse(options, "cpp test"_str);
    return parsed.is_ok() ? rstd::move(parsed).unwrap() : CppArgumentLayer {};
}

auto cpp_options(ref<str>          standard,
                 CppOptimization   optimization,
                 CppDebugInfo      debug_info,
                 lito::Vec<String> options = {}) -> CppCompileOptions {
    auto result = make_cpp_options(standard,
                                   StandardLibrary::Libcxx,
                                   false,
                                   false,
                                   optimization,
                                   debug_info,
                                   CppOptionLayer {
                                       .arguments = argument_layer(rstd::move(options)),
                                   });
    if (result.is_err()) return CppCompileOptions {};
    return rstd::move(result).unwrap();
}

auto format(ref<str> build = "clang-build-a"_str) -> BmiFormatIdentity {
    return BmiFormatIdentity {
        .family               = String::make("clang"_str),
        .compiler_build       = String::make(build),
        .target               = String::make("x86_64-unknown-linux-gnu"_str),
        .resource_environment = String::make("resource-a"_str),
    };
}

auto artifact_key(BmiRepresentation        representation,
                  BmiSourceEmbeddingPolicy embedding,
                  ref<str>                 dependency,
                  ref<str> source_content = "source-content-a"_str) -> BmiArtifactKey {
    auto dependencies = lito::Vec<BmiRecipeDependency>::make();
    if (! dependency.is_empty()) {
        dependencies.push(BmiRecipeDependency {
            .logical_name = String::make("dependency"_str),
            .artifact_key = String::make(dependency),
        });
    }
    return make_bmi_artifact_key(BmiRecipe {
        .request =
            BmiRequest {
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

auto has_argument(const lito::Vec<String>& arguments, ref<str> expected) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str() == expected) return true;
    }
    return false;
}

auto has_prefix(const lito::Vec<String>& arguments, ref<str> prefix) -> bool {
    for (const auto& argument : arguments) {
        if (argument.as_str().starts_with(prefix)) return true;
    }
    return false;
}

} // namespace lito_test
