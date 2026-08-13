export module lito.toolchain:clang_preprocessor_probe;

import rstd;
import lito.cpp;
import lito.error;
import lito.toolchain.contract;
import lito.package.target_contract;
import lito.system.process;
import lito.system.environment;
import lito.frontend;
import :clang_options;
import :command;
import :clang_preprocessor_model;

using namespace rstd::prelude;
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

auto parse_macro_dump(ref<str> output) -> ToolchainResult<Vec<preprocessor::MacroSeed>> {
    auto macros = Vec<preprocessor::MacroSeed>::make();
    auto parsed = each_line(output, [&macros](ref<str> raw) -> ToolchainResult<empty> {
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
        macros.push(preprocessor::MacroSeed { .definition = String::make(*definition) });
        return Ok(empty {});
    });
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    return Ok(rstd::move(macros));
}

struct ParsedMacroSet {
    lexical::SharedSourceSnapshot            source;
    Vec<preprocessor::SharedMacroDefinition> definitions;
};

auto parse_macro_seeds(const Vec<preprocessor::MacroSeed>& seeds, ref<str> source_name)
    -> ToolchainResult<ParsedMacroSet> {
    auto text = String::make();
    for (const auto& seed : seeds) {
        text.push_str("#define "_str);
        text.push_str(seed.definition.as_str());
        text.push_ascii('\n');
    }
    auto snapshot = lexical::make_source_snapshot(lexical::SourceBuffer {
        .path     = PathBuf::from(source_name),
        .contents = rstd::move(text),
    });
    auto source   = lexical::SourceFile { .snapshot = snapshot.clone() };
    auto tokens   = lexical::lex(source, true);
    if (tokens.is_err()) {
        return Err(rstd::into<ToolchainError>(rstd::move(tokens).unwrap_err()));
    }
    auto definitions = Vec<preprocessor::SharedMacroDefinition>::make();
    for (auto cursor = usize {}; cursor < tokens->len();) {
        auto end = cursor;
        while (end < tokens->len() && (*tokens)[end].kind != lexical::TokenKind::Newline) {
            ++end;
        }
        if (cursor == end) {
            cursor = end < tokens->len() ? end + usize(1) : end;
            continue;
        }
        if (cursor + usize(2) > end || (*tokens)[cursor].text.as_str() != "#"_str ||
            (*tokens)[cursor + usize(1)].text.as_str() != "define"_str) {
            return environment_failure<ParsedMacroSet>("invalid cached predefined macro line"_str);
        }
        auto line = Vec<lexical::Token>::make();
        for (auto index = cursor + usize(2); index < end; ++index)
            line.push((*tokens)[index].clone());
        auto definition = preprocessor::parse_macro_definition(line);
        if (definition.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(definition).unwrap_err()));
        }
        definitions.push(preprocessor::share_macro_definition(rstd::move(definition).unwrap()));
        cursor = end < tokens->len() ? end + usize(1) : end;
    }
    return Ok(ParsedMacroSet {
        .source      = rstd::move(snapshot),
        .definitions = rstd::move(definitions),
    });
}

auto macro_name(ref<str> definition) -> Option<ref<str>> {
    auto bytes = definition.as_bytes();
    auto end   = usize {};
    while (end < bytes.len() && bytes[end] != u8('=') && bytes[end] != u8('(')) ++end;
    if (end == usize {}) return None();
    return definition.get(usize {}, end);
}

auto command_line_macro_seed(ref<str> definition) -> preprocessor::MacroSeed {
    auto bytes = definition.as_bytes();
    auto equal = usize {};
    while (equal < bytes.len() && bytes[equal] != u8('=')) ++equal;
    auto value = String::make();
    auto name  = definition.get(usize {}, equal);
    if (name.is_some()) value.push_str(*name);
    value.push_ascii(' ');
    if (equal < bytes.len()) {
        auto replacement = definition.get(equal + usize(1), bytes.len());
        if (replacement.is_some()) value.push_str(*replacement);
    } else {
        value.push_ascii('1');
    }
    return preprocessor::MacroSeed { .definition = rstd::move(value) };
}

auto command_line_macro_states(const Vec<CppMacroDirective>& macros)
    -> rstd::collections::BTreeMap<String, Option<preprocessor::MacroSeed>> {
    auto values = rstd::collections::BTreeMap<String, Option<preprocessor::MacroSeed>>::make();
    auto apply_define = [&values](ref<str> definition) {
        auto name = macro_name(definition);
        if (name.is_some()) {
            values.insert(String::make(*name), Some(command_line_macro_seed(definition)));
        }
    };
    auto apply_undefine = [&values](ref<str> name) {
        if (! name.is_empty()) values.insert(String::make(name), None());
    };
    for (const auto& macro : macros) {
        if (macro.action == CppMacroAction::Define) {
            apply_define(macro.value.as_str());
        } else {
            apply_undefine(macro.value.as_str());
        }
    }
    return values;
}

struct CommandLineMacroEntry {
    String name;
    bool   defined { false };
};

struct ParsedCommandLineMacros {
    lexical::SharedSourceSnapshot               source;
    Vec<preprocessor::PredefinedMacroOperation> operations;
};

auto parse_command_line_macros(const Vec<CppMacroDirective>& macros)
    -> ToolchainResult<ParsedCommandLineMacros> {
    auto states  = command_line_macro_states(macros);
    auto seeds   = Vec<preprocessor::MacroSeed>::make();
    auto entries = Vec<CommandLineMacroEntry>::with_capacity(states.len());
    auto values  = states.into_iter();
    while (auto value = values.next()) {
        auto name    = rstd::move((*value).template get<0>());
        auto state   = rstd::move((*value).template get<1>());
        auto defined = state.is_some();
        if (defined) seeds.push(rstd::move(state).unwrap());
        entries.push(CommandLineMacroEntry {
            .name    = rstd::move(name),
            .defined = defined,
        });
    }
    auto parsed = parse_macro_seeds(seeds, "<command-line>"_str);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto parsed_values = rstd::move(parsed).unwrap();
    auto operations    = Vec<preprocessor::PredefinedMacroOperation>::with_capacity(entries.len());
    auto definition_index = usize {};
    for (auto& entry : entries) {
        if (entry.defined) {
            operations.push(preprocessor::PredefinedMacroOperation::define(
                parsed_values.definitions[definition_index].clone()));
            ++definition_index;
        } else {
            operations.push(
                preprocessor::PredefinedMacroOperation::undefine(rstd::move(entry.name)));
        }
    }
    return Ok(ParsedCommandLineMacros {
        .source     = rstd::move(parsed_values.source),
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

auto clang_owned_macro_seeds(const Vec<preprocessor::MacroSeed>& macros)
    -> Vec<preprocessor::MacroSeed> {
    auto result = Vec<preprocessor::MacroSeed>::with_capacity(macros.len());
    for (const auto& macro : macros) {
        auto name = macro_name(macro.definition.as_str());
        if (name.is_none() || ! native_predefined_macro(*name))
            result.push(preprocessor::MacroSeed {
                .definition = macro.definition.clone(),
            });
    }
    return result;
}

auto native_predefined_macro_seeds(const BuiltinSemanticContext& context)
    -> Vec<preprocessor::MacroSeed> {
    auto result = Vec<preprocessor::MacroSeed>::make();
    if (context.exceptions) {
        result.push(preprocessor::MacroSeed {
            .definition = String::make("__EXCEPTIONS 1"_str),
        });
        result.push(preprocessor::MacroSeed {
            .definition = String::make("__cpp_exceptions 199711L"_str),
        });
    }
    if (context.rtti) {
        result.push(preprocessor::MacroSeed {
            .definition = String::make("__GXX_RTTI 1"_str),
        });
        result.push(preprocessor::MacroSeed {
            .definition = String::make("__cpp_rtti 199711L"_str),
        });
    }
    return result;
}

auto builtin_snapshot_identity(const Vec<preprocessor::MacroSeed>& macros, ref<str> key) -> String {
    constexpr rstd::uint64_t offset = 14695981039346656037ull;
    constexpr rstd::uint64_t prime  = 1099511628211ull;
    auto                     hash   = offset;
    auto                     add    = [&hash](ref<str> value) {
        for (auto byte : value) {
            hash ^= byte.to_primitive();
            hash *= prime;
        }
        hash ^= 0;
        hash *= prime;
    };
    add("lito-clang-builtin-environment-v2"_str);
    add(key);
    for (const auto& macro : macros) add(macro.definition.as_str());
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (rstd::size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

auto environment_identity(ref<str>                       builtin_identity,
                          const Vec<IncludeSearchEntry>& includes,
                          ref<str>                       context_id) -> ToolchainResult<String> {
    constexpr rstd::uint64_t offset = 14695981039346656037ull;
    constexpr rstd::uint64_t prime  = 1099511628211ull;
    auto                     hash   = offset;
    auto                     add    = [&hash](ref<str> value) {
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
    add(CPP_IDENTIFIER_RULE_ID);
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
    for (rstd::size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return Ok(String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16))));
}

} // namespace lito::toolchain

namespace lito::toolchain
{

inline constexpr auto STANDARD_LIBRARY_HAS_BUILTIN = R"LITO(__add_pointer
__array_extent
__array_rank
__atomic_fetch_max
__atomic_fetch_min
__builtin_assume
__builtin_bswap128
__builtin_bswapg
__builtin_char_memchr
__builtin_clear_padding
__builtin_common_type
__builtin_complex
__builtin_coro_noop
__builtin_invoke
__builtin_is_constant_evaluated
__builtin_is_implicit_lifetime
__builtin_is_virtual_base_of
__builtin_is_within_lifetime
__builtin_isinf
__builtin_lt_synthesizes_from_spaceship
__builtin_readcyclecounter
__builtin_va_copy
__builtin_verbose_trap
__builtin_wcslen
__builtin_wmemchr
__builtin_wmemcmp
__has_trivial_destructor
__is_compound
__is_const
__is_destructible
__is_fundamental
__is_integral
__is_lvalue_reference
__is_nothrow_destructible
__is_pointer
__is_rvalue_reference
__is_scalar
__is_signed
__is_trivially_destructible
__is_trivially_equality_comparable
__is_trivially_relocatable
__is_unbounded_array
__is_unsigned
__make_integer_seq
__make_signed
__make_unsigned
__remove_const
__remove_extent
__remove_pointer
__remove_reference
__remove_reference_t
__remove_volatile
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_FEATURE = R"LITO(address_sanitizer
cxx_atomic
experimental_library
modules
nullability
objc_arc
objc_arc_weak
ptrauth_calls
ptrauth_type_info_vtable_pointer_discrimination
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_EXTENSION = R"LITO(blocks
c_atomic
datasizeof
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_CPP_ATTRIBUTE =
    R"LITO(_Clang::acquire_shared_capability
_Clang::capability
_Clang::lifetimebound
_Clang::no_destroy
_Clang::no_field_protection
_Clang::no_specializations
_Clang::no_thread_safety_analysis
_Clang::noescape
_Clang::preferred_name
_Clang::ptrauth_vtable_pointer
_Clang::release_capability
_Clang::release_shared_capability
_Clang::scoped_lockable
_Clang::try_acquire_capability
_Clang::try_acquire_shared_capability
clang::coro_await_elidable
clang::coro_await_elidable_argument
msvc::no_unique_address
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_ATTRIBUTE =
    R"LITO(acquire_capability
deprecated
diagnose_if
enable_if
exclude_from_explicit_instantiation
no_sanitize
noinline
release_capability
require_constant_initialization
requires_capability
type_visibility
using_if_exists
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_DECLSPEC_ATTRIBUTE = R"LITO(empty_bases
)LITO"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_WARNING = R"LITO(-Winvalid-specialization
)LITO"_str;

template<typename Query>
auto append_standard_library_capabilities(Vec<preprocessor::BuiltinQueryKey>& result,
                                          ref<str>                            values) -> void {
    auto begin = usize {};
    while (begin < values.len()) {
        auto end = begin;
        while (end < values.len() && values.as_bytes()[end] != u8('\n')) ++end;
        auto value = values.get(begin, end);
        if (value.is_some() && ! value->is_empty()) {
            result.push(preprocessor::BuiltinQueryKey::make<Query>(*value));
        }
        begin = end + usize(1);
    }
}

auto standard_library_capabilities() -> Vec<preprocessor::BuiltinQueryKey> {
    auto result = Vec<preprocessor::BuiltinQueryKey>::with_capacity(usize(96));
    append_standard_library_capabilities<preprocessor::HasBuiltinQuery>(
        result, STANDARD_LIBRARY_HAS_BUILTIN);
    append_standard_library_capabilities<preprocessor::HasFeatureQuery>(
        result, STANDARD_LIBRARY_HAS_FEATURE);
    append_standard_library_capabilities<preprocessor::HasExtensionQuery>(
        result, STANDARD_LIBRARY_HAS_EXTENSION);
    append_standard_library_capabilities<preprocessor::HasCppAttributeQuery>(
        result, STANDARD_LIBRARY_HAS_CPP_ATTRIBUTE);
    append_standard_library_capabilities<preprocessor::HasAttributeQuery>(
        result, STANDARD_LIBRARY_HAS_ATTRIBUTE);
    append_standard_library_capabilities<preprocessor::HasDeclspecAttributeQuery>(
        result, STANDARD_LIBRARY_HAS_DECLSPEC_ATTRIBUTE);
    append_standard_library_capabilities<preprocessor::HasWarningQuery>(
        result, STANDARD_LIBRARY_HAS_WARNING);
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
                              ref<rstd::path::Path>             working_directory,
                              const ResolvedProcessEnvironment& environment)
    -> ToolchainResult<QueriedCapabilities> {
    auto catalog      = standard_library_capabilities();
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
    command::push_option(command_line, clang_options::CXX_SOURCE);
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
    auto parsed =
        each_line(output->standard_output.as_str(), [&values](ref<str> raw) -> ToolchainResult<empty> {
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
