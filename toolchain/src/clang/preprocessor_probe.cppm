export module lito.toolchain.clang:preprocessor_probe;

import rstd;
import lito.cpp;
import lito.core;
import lito.toolchain.common;
import lito.system;
import lito.frontend;
import lito.frontend.static_name;
import :options;
import :preprocessor_model;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito::toolchain
{

template<typename T>
auto environment_failure(String message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(rstd::move(message)));
}

template<typename T>
auto environment_failure(ref<str> message) -> ToolchainResult<T> {
    return Err(ToolchainError::Message(String::make(message)));
}

auto clone_command(const Vec<String>& source) -> Vec<String> {
    auto result = Vec<String>::with_capacity(source.len());
    for (const auto& argument : source) result.push(argument.clone());
    return result;
}

template<typename Callback>
auto each_line(ref<str> text, Callback&& callback) -> ToolchainResult<empty> {
    auto bytes = text.as_bytes();
    auto begin = usize {};
    while (begin <= bytes.len()) {
        auto end = begin;
        while (end < bytes.len() && bytes[end] != u8('\n') && bytes[end] != u8('\r')) ++end;
        auto line = text.get(begin, end);
        if (line.is_none()) return environment_failure<empty>("invalid UTF-8 line boundary"_str);
        auto result = callback(*line);
        if (result.is_err()) return result;
        if (end == bytes.len()) break;
        if (bytes[end] == u8('\r') && end + usize(1) < bytes.len() &&
            bytes[end + usize(1)] == u8('\n')) {
            ++end;
        }
        begin = end + usize(1);
    }
    return Ok(empty {});
}

auto macro_name(ref<str> definition) -> Option<ref<str>> {
    auto bytes = definition.as_bytes();
    auto end   = usize {};
    while (end < bytes.len() && bytes[end] != u8('=') && bytes[end] != u8('(') &&
           bytes[end] != u8(' ') && bytes[end] != u8('\t')) {
        ++end;
    }
    if (end == usize {}) return None();
    return definition.get(usize {}, end);
}

auto command_line_macro_states(const Vec<PreprocessorMacroDirective>& macros)
    -> rstd::collections::BTreeMap<String, Option<String>> {
    auto values       = rstd::collections::BTreeMap<String, Option<String>>::make();
    auto apply_define = [&values](ref<str> definition) {
        auto name = macro_name(definition);
        if (name.is_some()) {
            values.insert(String::make(*name), Some(String::make(definition)));
        }
    };
    auto apply_undefine = [&values](ref<str> name) {
        if (! name.is_empty()) values.insert(String::make(name), None());
    };
    for (const auto& macro : macros) {
        if (macro.defined) {
            apply_define(macro.value.as_str());
        } else {
            apply_undefine(macro.value.as_str());
        }
    }
    return values;
}

struct ParsedCommandLineMacros {
    Vec<preprocessor::PredefinedMacroOperation> operations;
};

auto parse_command_line_macros(const Vec<PreprocessorMacroDirective>& macros)
    -> ToolchainResult<ParsedCommandLineMacros> {
    auto states     = command_line_macro_states(macros);
    auto operations = Vec<preprocessor::PredefinedMacroOperation>::with_capacity(states.len());
    for (auto value : rstd::move(states).into_iter()) {
        auto name  = rstd::move(value.template get<0>());
        auto state = rstd::move(value.template get<1>());
        if (state.is_some()) {
            auto definition = preprocessor::parse_command_line_macro_definition(state->as_str());
            if (definition.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(definition).unwrap_err()));
            }
            operations.push(preprocessor::PredefinedMacroOperation::define(
                preprocessor::share_macro_definition(rstd::move(definition).unwrap())));
        } else {
            operations.push(preprocessor::PredefinedMacroOperation::undefine(rstd::move(name)));
        }
    }
    return Ok(ParsedCommandLineMacros {
        .operations = rstd::move(operations),
    });
}

auto parse_include_search(ref<str> output) -> ToolchainResult<Vec<IncludeSearchEntry>> {
    auto entries   = Vec<IncludeSearchEntry>::make();
    auto inside    = false;
    auto saw_start = false;
    auto saw_end   = false;
    auto system    = true;
    auto parsed    = each_line(output, [&](ref<str> raw) -> ToolchainResult<empty> {
        auto line = raw.trim_ascii();
        if (line == "#include \"...\" search starts here:"_str) {
            inside    = true;
            saw_start = true;
            system    = false;
            return Ok(empty {});
        }
        if (line == "#include <...> search starts here:"_str) {
            inside    = true;
            saw_start = true;
            system    = true;
            return Ok(empty {});
        }
        if (line == "End of search list."_str) {
            if (inside) saw_end = true;
            inside = false;
            return Ok(empty {});
        }
        if (! inside || line.is_empty()) return Ok(empty {});
        if (line.contains("(framework directory)"_str)) {
            return environment_failure<empty>(
                rstd::format("framework include search is unsupported: {}", line));
        }
        auto directory = PathBuf::from(line);
        auto canonical = rstd::fs::canonicalize(directory.as_path());
        if (canonical.is_err()) {
            return Err(ToolchainError::Io(String::make("resolve clang include directory"_str),
                                          rstd::move(directory),
                                          rstd::move(canonical).unwrap_err()));
        }
        entries.push(
            IncludeSearchEntry { .directory = rstd::move(canonical).unwrap(), .system = system });
        return Ok(empty {});
    });
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    if (! saw_start || ! saw_end) {
        return environment_failure<Vec<IncludeSearchEntry>>(
            "clang++ -E -v did not emit a complete include search list"_str);
    }
    return Ok(rstd::move(entries));
}

auto capability_key(const preprocessor::BuiltinQueryKey& query) -> String {
    return rstd::format("{}:{}", query.name(), query.argument.as_str());
}

auto native_capability(const preprocessor::BuiltinQueryKey& query,
                       const BuiltinSemanticContext&        context) -> Option<i64> {
    if ((query.is<preprocessor::HasFeatureQuery>() ||
         query.is<preprocessor::HasExtensionQuery>()) &&
        query.argument.as_str() == "cxx_exceptions"_str) {
        return Some(i64(context.exceptions));
    }
    if ((query.is<preprocessor::HasFeatureQuery>() ||
         query.is<preprocessor::HasExtensionQuery>()) &&
        query.argument.as_str() == "cxx_rtti"_str) {
        return Some(i64(context.rtti));
    }
    return None();
}

auto native_predefined_macro(ref<str> name) -> bool {
    return name == "__EXCEPTIONS"_str || name == "__cpp_exceptions"_str ||
           name == "__GXX_RTTI"_str || name == "__cpp_rtti"_str;
}

auto native_predefined_macros(const BuiltinSemanticContext& context)
    -> ToolchainResult<Vec<preprocessor::SharedMacroDefinition>> {
    auto result = Vec<preprocessor::SharedMacroDefinition>::make();
    auto append = [&result](ref<str> value) -> ToolchainResult<empty> {
        auto definition = preprocessor::parse_command_line_macro_definition(value);
        if (definition.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(definition).unwrap_err()));
        }
        result.push(preprocessor::share_macro_definition(rstd::move(definition).unwrap()));
        return Ok(empty {});
    };
    if (context.exceptions) {
        auto exceptions = append("__EXCEPTIONS=1"_str);
        if (exceptions.is_err()) return Err(rstd::move(exceptions).unwrap_err());
        auto cpp_exceptions = append("__cpp_exceptions=199711L"_str);
        if (cpp_exceptions.is_err()) return Err(rstd::move(cpp_exceptions).unwrap_err());
    }
    if (context.rtti) {
        auto rtti = append("__GXX_RTTI=1"_str);
        if (rtti.is_err()) return Err(rstd::move(rtti).unwrap_err());
        auto cpp_rtti = append("__cpp_rtti=199711L"_str);
        if (cpp_rtti.is_err()) return Err(rstd::move(cpp_rtti).unwrap_err());
    }
    return Ok(rstd::move(result));
}

struct ParsedMacroDump {
    Vec<preprocessor::SharedMacroDefinition> definitions;
    String                                   identity;
    usize                                    macro_count {};
};

auto parse_macro_dump(String output, ref<str> source_name, ref<str> key)
    -> ToolchainResult<ParsedMacroDump> {
    constexpr uint64_t offset = 14695981039346656037ull;
    constexpr uint64_t prime  = 1099511628211ull;
    auto               hash   = offset;
    auto               add    = [&hash](ref<str> value) {
        for (auto byte : value) {
            hash ^= byte.to_primitive();
            hash *= prime;
        }
        hash ^= 0;
        hash *= prime;
    };
    add("lito-clang-builtin-environment-v2"_str);
    add(key);
    auto macro_count = usize {};
    auto summarized  = each_line(output.as_str(), [&](ref<str> raw) -> ToolchainResult<empty> {
        auto line = raw.trim_ascii();
        if (line.is_empty()) return Ok(empty {});
        constexpr auto prefix = "#define "_str;
        if (! line.starts_with(prefix)) {
            return environment_failure<empty>(
                rstd::format("unexpected clang++ -dM output line: {}", line));
        }
        auto definition = line.get(prefix.len(), line.len());
        if (definition.is_none() || definition->is_empty()) {
            return environment_failure<empty>("clang++ -dM emitted an empty definition"_str);
        }
        auto name = macro_name(*definition);
        if (name.is_none() || ! native_predefined_macro(*name)) {
            add(*definition);
            ++macro_count;
        }
        return Ok(empty {});
    });
    if (summarized.is_err()) return Err(rstd::move(summarized).unwrap_err());

    static constexpr char digits[] = "0123456789abcdef";
    char                  identity_text[16];
    for (size_t index = 0; index < 16; ++index) {
        identity_text[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }

    auto parsed = preprocessor::parse_macro_source(lexical::SourceBuffer {
        .path     = PathBuf::from(source_name),
        .contents = rstd::move(output),
    });
    if (parsed.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(parsed).unwrap_err()));
    }
    auto definitions = Vec<preprocessor::SharedMacroDefinition>::with_capacity(macro_count);
    for (auto& definition : parsed->definitions) {
        if (! native_predefined_macro(definition->name.as_str())) {
            definitions.push(rstd::move(definition));
        }
    }
    if (definitions.len() != macro_count) {
        return environment_failure<ParsedMacroDump>(
            "clang++ -dM macro framing and parsed definitions disagree"_str);
    }
    return Ok(ParsedMacroDump {
        .definitions = rstd::move(definitions),
        .identity    = String::make(ref<str>::from_raw_parts_unchecked(
            reinterpret_cast<const byte*>(identity_text), usize(16))),
        .macro_count = macro_count,
    });
}

auto environment_identity(ref<str>                       builtin_identity,
                          const Vec<IncludeSearchEntry>& includes,
                          ref<str>                       context_id,
                          PreprocessorLanguage           language) -> ToolchainResult<String> {
    constexpr uint64_t offset = 14695981039346656037ull;
    constexpr uint64_t prime  = 1099511628211ull;
    auto               hash   = offset;
    auto               add    = [&hash](ref<str> value) {
        for (auto byte : value) {
            hash ^= byte.to_primitive();
            hash *= prime;
        }
        hash ^= 0;
        hash *= prime;
    };
    add("lito-clang-preprocessor-environment-v2"_str);
    add(context_id);
    add(builtin_identity);
    add(language == PreprocessorLanguage::C ? lito::c::C_IDENTIFIER_RULE_ID
                                            : cpp::CPP_IDENTIFIER_RULE_ID);
    for (const auto& include : includes) {
        auto text = include.directory.as_path().to_str();
        if (text.is_none()) {
            return environment_failure<String>(rstd::format(
                "include directory '{}' is not valid UTF-8", include.directory.as_path()));
        }
        add(*text);
        add(include.system ? "system"_str : "quote"_str);
    }
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return Ok(String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16))));
}

} // namespace lito::toolchain

namespace lito::toolchain
{

template<typename QueryType, typename... Arguments>
struct StandardLibraryCapabilitySet {
    using Query = QueryType;

    static constexpr auto size = usize(sizeof...(Arguments));

    static auto append(Vec<preprocessor::BuiltinQueryKey>& result) -> void {
        frontend::StaticNameSet<Arguments...>::for_each([&result](auto argument) {
            using Argument = typename decltype(argument)::type;
            result.push(preprocessor::BuiltinQueryKey::make<Query>(Argument::name));
        });
    }
};

using StandardLibraryHasBuiltin =
    StandardLibraryCapabilitySet<preprocessor::HasBuiltinQuery,
                                 frontend::StaticName<"__add_pointer">,
                                 frontend::StaticName<"__array_extent">,
                                 frontend::StaticName<"__array_rank">,
                                 frontend::StaticName<"__atomic_fetch_max">,
                                 frontend::StaticName<"__atomic_fetch_min">,
                                 frontend::StaticName<"__builtin_assume">,
                                 frontend::StaticName<"__builtin_bswap128">,
                                 frontend::StaticName<"__builtin_bswapg">,
                                 frontend::StaticName<"__builtin_char_memchr">,
                                 frontend::StaticName<"__builtin_clear_padding">,
                                 frontend::StaticName<"__builtin_common_type">,
                                 frontend::StaticName<"__builtin_complex">,
                                 frontend::StaticName<"__builtin_coro_noop">,
                                 frontend::StaticName<"__builtin_invoke">,
                                 frontend::StaticName<"__builtin_is_constant_evaluated">,
                                 frontend::StaticName<"__builtin_is_implicit_lifetime">,
                                 frontend::StaticName<"__builtin_is_virtual_base_of">,
                                 frontend::StaticName<"__builtin_is_within_lifetime">,
                                 frontend::StaticName<"__builtin_isinf">,
                                 frontend::StaticName<"__builtin_lt_synthesizes_from_spaceship">,
                                 frontend::StaticName<"__builtin_readcyclecounter">,
                                 frontend::StaticName<"__builtin_va_copy">,
                                 frontend::StaticName<"__builtin_verbose_trap">,
                                 frontend::StaticName<"__builtin_wcslen">,
                                 frontend::StaticName<"__builtin_wmemchr">,
                                 frontend::StaticName<"__builtin_wmemcmp">,
                                 frontend::StaticName<"__has_trivial_destructor">,
                                 frontend::StaticName<"__is_compound">,
                                 frontend::StaticName<"__is_const">,
                                 frontend::StaticName<"__is_destructible">,
                                 frontend::StaticName<"__is_fundamental">,
                                 frontend::StaticName<"__is_integral">,
                                 frontend::StaticName<"__is_lvalue_reference">,
                                 frontend::StaticName<"__is_nothrow_destructible">,
                                 frontend::StaticName<"__is_pointer">,
                                 frontend::StaticName<"__is_rvalue_reference">,
                                 frontend::StaticName<"__is_scalar">,
                                 frontend::StaticName<"__is_signed">,
                                 frontend::StaticName<"__is_trivially_destructible">,
                                 frontend::StaticName<"__is_trivially_equality_comparable">,
                                 frontend::StaticName<"__is_trivially_relocatable">,
                                 frontend::StaticName<"__is_unbounded_array">,
                                 frontend::StaticName<"__is_unsigned">,
                                 frontend::StaticName<"__make_integer_seq">,
                                 frontend::StaticName<"__make_signed">,
                                 frontend::StaticName<"__make_unsigned">,
                                 frontend::StaticName<"__remove_const">,
                                 frontend::StaticName<"__remove_extent">,
                                 frontend::StaticName<"__remove_pointer">,
                                 frontend::StaticName<"__remove_reference">,
                                 frontend::StaticName<"__remove_reference_t">,
                                 frontend::StaticName<"__remove_volatile">>;

using StandardLibraryHasFeature = StandardLibraryCapabilitySet<
    preprocessor::HasFeatureQuery,
    frontend::StaticName<"address_sanitizer">,
    frontend::StaticName<"cxx_atomic">,
    frontend::StaticName<"cxx_unicode_literals">,
    frontend::StaticName<"experimental_library">,
    frontend::StaticName<"modules">,
    frontend::StaticName<"nullability">,
    frontend::StaticName<"objc_arc">,
    frontend::StaticName<"objc_arc_weak">,
    frontend::StaticName<"ptrauth_calls">,
    frontend::StaticName<"ptrauth_type_info_vtable_pointer_discrimination">>;

using StandardLibraryHasExtension =
    StandardLibraryCapabilitySet<preprocessor::HasExtensionQuery,
                                 frontend::StaticName<"blocks">,
                                 frontend::StaticName<"c_atomic">,
                                 frontend::StaticName<"datasizeof">>;

using StandardLibraryHasCppAttribute =
    StandardLibraryCapabilitySet<preprocessor::HasCppAttributeQuery,
                                 frontend::StaticName<"_Clang::acquire_shared_capability">,
                                 frontend::StaticName<"_Clang::capability">,
                                 frontend::StaticName<"_Clang::lifetimebound">,
                                 frontend::StaticName<"_Clang::no_destroy">,
                                 frontend::StaticName<"_Clang::no_field_protection">,
                                 frontend::StaticName<"_Clang::no_specializations">,
                                 frontend::StaticName<"_Clang::no_thread_safety_analysis">,
                                 frontend::StaticName<"_Clang::noescape">,
                                 frontend::StaticName<"_Clang::preferred_name">,
                                 frontend::StaticName<"_Clang::ptrauth_vtable_pointer">,
                                 frontend::StaticName<"_Clang::release_capability">,
                                 frontend::StaticName<"_Clang::release_shared_capability">,
                                 frontend::StaticName<"_Clang::scoped_lockable">,
                                 frontend::StaticName<"_Clang::try_acquire_capability">,
                                 frontend::StaticName<"_Clang::try_acquire_shared_capability">,
                                 frontend::StaticName<"clang::coro_await_elidable">,
                                 frontend::StaticName<"clang::coro_await_elidable_argument">,
                                 frontend::StaticName<"msvc::no_unique_address">>;

using StandardLibraryHasAttribute =
    StandardLibraryCapabilitySet<preprocessor::HasAttributeQuery,
                                 frontend::StaticName<"acquire_capability">,
                                 frontend::StaticName<"deprecated">,
                                 frontend::StaticName<"diagnose_if">,
                                 frontend::StaticName<"enable_if">,
                                 frontend::StaticName<"exclude_from_explicit_instantiation">,
                                 frontend::StaticName<"no_sanitize">,
                                 frontend::StaticName<"noinline">,
                                 frontend::StaticName<"release_capability">,
                                 frontend::StaticName<"require_constant_initialization">,
                                 frontend::StaticName<"requires_capability">,
                                 frontend::StaticName<"type_visibility">,
                                 frontend::StaticName<"using_if_exists">>;

using StandardLibraryHasDeclspecAttribute =
    StandardLibraryCapabilitySet<preprocessor::HasDeclspecAttributeQuery,
                                 frontend::StaticName<"empty_bases">>;

using StandardLibraryHasWarning =
    StandardLibraryCapabilitySet<preprocessor::HasWarningQuery,
                                 frontend::StaticName<"-Winvalid-specialization">>;

auto standard_library_capabilities() -> Vec<preprocessor::BuiltinQueryKey> {
    auto result = Vec<preprocessor::BuiltinQueryKey>::with_capacity(
        StandardLibraryHasBuiltin::size + StandardLibraryHasFeature::size +
        StandardLibraryHasExtension::size + StandardLibraryHasCppAttribute::size +
        StandardLibraryHasAttribute::size + StandardLibraryHasDeclspecAttribute::size +
        StandardLibraryHasWarning::size);
    StandardLibraryHasBuiltin::append(result);
    StandardLibraryHasFeature::append(result);
    StandardLibraryHasExtension::append(result);
    StandardLibraryHasCppAttribute::append(result);
    StandardLibraryHasAttribute::append(result);
    StandardLibraryHasDeclspecAttribute::append(result);
    StandardLibraryHasWarning::append(result);
    return result;
}

struct QueriedCapabilities {
    rstd::collections::HashMap<String, i64> values;
    usize                                   clang_count {};
    usize                                   native_count {};
    usize                                   input_bytes {};
    usize                                   output_bytes {};
};

auto parse_capability_value(ref<str> raw) -> ToolchainResult<i64> {
    auto value = raw.trim_ascii();
    while (value.len() >= usize(2) && value.as_bytes()[usize {}] == u8('(') &&
           value.as_bytes()[value.len() - usize(1)] == u8(')')) {
        auto inner = value.get(usize(1), value.len() - usize(1));
        if (inner.is_none()) break;
        value = inner->trim_ascii();
    }
    auto cursor   = usize {};
    auto negative = false;
    if (cursor < value.len() &&
        (value.as_bytes()[cursor] == u8('+') || value.as_bytes()[cursor] == u8('-'))) {
        negative = value.as_bytes()[cursor] == u8('-');
        ++cursor;
    }
    auto digits = usize {};
    auto result = i64 {};
    while (cursor < value.len() && value.as_bytes()[cursor] >= u8('0') &&
           value.as_bytes()[cursor] <= u8('9')) {
        result = result * i64(10) +
                 i64(value.as_bytes()[cursor].to_primitive() - u8('0').to_primitive());
        ++cursor;
        ++digits;
    }
    while (cursor < value.len() &&
           (value.as_bytes()[cursor] == u8('u') || value.as_bytes()[cursor] == u8('U') ||
            value.as_bytes()[cursor] == u8('l') || value.as_bytes()[cursor] == u8('L'))) {
        ++cursor;
    }
    if (digits == usize {} || cursor != value.len()) {
        return environment_failure<i64>(rstd::format("invalid clang builtin query value: {}", raw));
    }
    return Ok(negative ? -result : result);
}

auto query_clang_capabilities(const Vec<String>&                base_command,
                              const BuiltinSemanticContext&     semantic_context,
                              PreprocessorLanguage              language,
                              ref<rstd::path::Path>             working_directory,
                              const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<QueriedCapabilities> {
    auto catalog      = language == PreprocessorLanguage::Cpp
                            ? standard_library_capabilities()
                            : Vec<preprocessor::BuiltinQueryKey>::make();
    auto pending      = Vec<preprocessor::BuiltinQueryKey>::make();
    auto source       = String::make();
    auto native_count = usize {};
    auto cursor       = usize {};
    while (cursor < catalog.len()) {
        const auto& query   = catalog[cursor];
        auto        builtin = query.name();
        source.push_str(rstd::format("#if !defined({})\n", builtin).as_str());
        source.push_str(rstd::format("#define {}(...) 0\n", builtin).as_str());
        source.push_str("#define LITO_DEFINED_QUERY_BUILTIN 1\n"_str);
        source.push_str("#endif\n"_str);
        while (cursor < catalog.len() && catalog[cursor].same_query(query)) {
            if (native_capability(catalog[cursor], semantic_context).is_some()) {
                ++native_count;
                ++cursor;
                continue;
            }
            auto argument = catalog[cursor].render_argument();
            auto index    = pending.len();
            source.push_str(
                rstd::format("LITO_BUILTIN_QUERY_{} {}({})\n", index, builtin, argument.as_str())
                    .as_str());
            pending.push(catalog[cursor].clone());
            ++cursor;
        }
        source.push_str("#if defined(LITO_DEFINED_QUERY_BUILTIN)\n"_str);
        source.push_str(rstd::format("#undef {}\n", builtin).as_str());
        source.push_str("#undef LITO_DEFINED_QUERY_BUILTIN\n"_str);
        source.push_str("#endif\n"_str);
    }

    auto command_line = clone_command(base_command);
    command::push_option(command_line, clang_options::PREPROCESS);
    command::push_option(command_line, clang_options::NO_LINE_MARKERS);
    command::push_option(command_line, clang_options::LANGUAGE);
    command::push_option(command_line,
                         language == PreprocessorLanguage::C ? clang_options::C_SOURCE
                                                             : clang_options::CXX_SOURCE);
    command::push_option(command_line, clang_options::STANDARD_INPUT);
    auto output =
        run_command_with_input(command_line, source.as_str(), environment, Some(working_directory));
    if (output.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
    }
    if (output->exit_code != i32 {}) {
        return environment_failure<QueriedCapabilities>(
            rstd::format("clang builtin capability query failed\n{}\n{}",
                         command_text(command_line).as_str(),
                         output->standard_error.as_str()));
    }

    auto values = Vec<i64>::make();
    auto parsed = each_line(
        output->standard_output.as_str(), [&values](ref<str> raw) -> ToolchainResult<empty> {
            auto line = raw.trim_ascii();
            if (line.is_empty()) return Ok(empty {});
            constexpr auto prefix = "LITO_BUILTIN_QUERY_"_str;
            if (! line.starts_with(prefix)) {
                return environment_failure<empty>(
                    rstd::format("unexpected clang builtin query output: {}", line));
            }
            auto cursor = prefix.len();
            auto index  = usize {};
            auto digits = usize {};
            while (cursor < line.len() && line.as_bytes()[cursor] >= u8('0') &&
                   line.as_bytes()[cursor] <= u8('9')) {
                index = index * usize(10) +
                        usize(line.as_bytes()[cursor].to_primitive() - u8('0').to_primitive());
                ++cursor;
                ++digits;
            }
            if (digits == usize {} || index != values.len()) {
                return environment_failure<empty>(
                    "clang builtin query output index is invalid"_str);
            }
            while (cursor < line.len() && line.as_bytes()[cursor] == u8(' ')) ++cursor;
            auto value_text = line.get(cursor, line.len());
            if (value_text.is_none() || value_text->is_empty()) {
                return environment_failure<empty>("clang builtin query emitted no value"_str);
            }
            auto value = parse_capability_value(*value_text);
            if (value.is_err()) {
                return Err(rstd::move(value).unwrap_err());
            }
            values.push(i64(*value));
            return Ok(empty {});
        });
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    if (values.len() != pending.len()) {
        return environment_failure<QueriedCapabilities>(
            rstd::format("clang builtin query returned {} values for {} catalog entries",
                         values.len(),
                         pending.len()));
    }
    auto result = rstd::collections::HashMap<String, i64>::with_capacity(pending.len());
    for (auto index = usize {}; index < pending.len(); ++index)
        result.insert(capability_key(pending[index]), values[index]);
    return Ok(QueriedCapabilities {
        .values       = rstd::move(result),
        .clang_count  = pending.len(),
        .native_count = native_count,
        .input_bytes  = source.len(),
        .output_bytes = output->standard_output.len(),
    });
}

} // namespace lito::toolchain
