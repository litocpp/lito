export module tenon.toolchain:clang_preprocessor_environment;

import rstd;
import tenon.model;
import tenon.process;
import tenon.frontend;
import :clang_options;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{
namespace lexical      = frontend::lexical;
namespace preprocessor = frontend::preprocessor;
} // namespace tenon

export namespace tenon::toolchain
{

struct IncludeSearchEntry {
    PathBuf directory;
    bool    system { true };
};

struct BuiltinSemanticContext {
    String language_standard;
    bool   rtti { false };
    bool   exceptions { false };
};

inline constexpr auto CLANG_STANDARD_LIBRARY_CAPABILITY_ID =
    "tenon-clang-standard-library-capabilities-v1"_str;

struct ClangBuiltinEnvironmentSnapshot {
    String                                   key;
    String                                   identity;
    lexical::SharedSourceSnapshot            source;
    Vec<preprocessor::SharedMacroDefinition> definitions;
    rstd::collections::HashMap<String, i64>  capabilities;
    usize                                    clang_macro_count {};
    usize                                    native_macro_count {};
    usize                                    clang_capability_count {};
    usize                                    native_capability_count {};
    usize                                    macro_output_bytes {};
    usize                                    capability_input_bytes {};
    usize                                    capability_output_bytes {};
};

using SharedClangBuiltinEnvironmentSnapshot = rstd::rc::Rc<const ClangBuiltinEnvironmentSnapshot>;

struct PreprocessorEnvironmentKey {
    String  context_id;
    PathBuf working_directory;

    static auto make(ref<str> context_id, ref<rstd::path::Path> working_directory)
        -> PreprocessorEnvironmentKey {
        return PreprocessorEnvironmentKey {
            .context_id        = String::make(context_id),
            .working_directory = PathBuf::from(working_directory),
        };
    }

    auto matches(ref<str> context, ref<rstd::path::Path> working) const noexcept -> bool {
        return context_id.as_str() == context && working_directory.as_path() == working;
    }
};

struct PreprocessorEnvironment {
    PreprocessorEnvironmentKey                  key;
    SharedClangBuiltinEnvironmentSnapshot       builtin_environment;
    lexical::SharedSourceSnapshot               native_source;
    Vec<preprocessor::SharedMacroDefinition>    native_definitions;
    lexical::SharedSourceSnapshot               command_line_source;
    Vec<preprocessor::PredefinedMacroOperation> command_line_macros;
    BuiltinSemanticContext                      semantic_context;
    Vec<IncludeSearchEntry>                     include_search;
    Vec<String>                                 query_command;
    String                                      identity;
    Option<String>                              date;
    Option<String>                              time;
};

} // namespace tenon::toolchain

namespace tenon::toolchain
{

template<typename T>
auto environment_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto environment_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

auto clone_command(const Vec<String>& source) -> Vec<String> {
    auto result = Vec<String>::with_capacity(source.len());
    for (const auto& argument : source) result.push(argument.clone());
    return result;
}

template<typename Callback>
auto each_line(ref<str> text, Callback&& callback) -> Result<empty> {
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

auto parse_macro_dump(ref<str> output) -> Result<Vec<preprocessor::MacroSeed>> {
    auto macros = Vec<preprocessor::MacroSeed>::make();
    auto parsed = each_line(output, [&macros](ref<str> raw) -> Result<empty> {
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
    -> Result<ParsedMacroSet> {
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
        return environment_failure<ParsedMacroSet>(rstd::move(tokens).unwrap_err().message.clone());
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
            return environment_failure<ParsedMacroSet>(
                rstd::move(definition).unwrap_err().message.clone());
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

auto command_line_macro_states(const Vec<String>& options, const Vec<String>& definitions)
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
    for (auto index = usize {}; index < options.len(); ++index) {
        auto option = options[index].as_str();
        if (option == "-D"_str && index + usize(1) < options.len()) {
            ++index;
            apply_define(options[index].as_str());
        } else if (option.starts_with("-D"_str) && option.len() > usize(2)) {
            auto value = option.get(usize(2), option.len());
            if (value.is_some()) apply_define(*value);
        } else if (option == "-U"_str && index + usize(1) < options.len()) {
            ++index;
            apply_undefine(options[index].as_str());
        } else if (option.starts_with("-U"_str) && option.len() > usize(2)) {
            auto value = option.get(usize(2), option.len());
            if (value.is_some()) apply_undefine(*value);
        }
    }
    for (const auto& definition : definitions) apply_define(definition.as_str());
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

auto parse_command_line_macros(const Vec<String>& options, const Vec<String>& definitions)
    -> Result<ParsedCommandLineMacros> {
    auto states  = command_line_macro_states(options, definitions);
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

auto parse_include_search(ref<str> output) -> Result<Vec<IncludeSearchEntry>> {
    auto entries   = Vec<IncludeSearchEntry>::make();
    auto inside    = false;
    auto saw_start = false;
    auto saw_end   = false;
    auto system    = true;
    auto parsed    = each_line(output, [&](ref<str> raw) -> Result<empty> {
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
            return environment_failure<empty>(
                rstd::format("cannot resolve clang include directory '{}': {}",
                             directory.as_path(),
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
    return rstd::format(
        "{}:{}", preprocessor::builtin_query_name(query.kind), query.argument.as_str());
}

auto native_capability(const preprocessor::BuiltinQueryKey& query,
                       const BuiltinSemanticContext&        context) -> Option<i64> {
    if ((query.kind == preprocessor::BuiltinQueryKind::HasFeature ||
         query.kind == preprocessor::BuiltinQueryKind::HasExtension) &&
        query.argument.as_str() == "cxx_exceptions"_str) {
        return Some(i64(context.exceptions));
    }
    if ((query.kind == preprocessor::BuiltinQueryKind::HasFeature ||
         query.kind == preprocessor::BuiltinQueryKind::HasExtension) &&
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
    add("tenon-clang-builtin-environment-v2"_str);
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
                          ref<str>                       context_id) -> Result<String> {
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
    add("tenon-clang-preprocessor-environment-v2"_str);
    add(context_id);
    add(builtin_identity);
    add(lexical::CPP_IDENTIFIER_RULE_ID);
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

auto preprocessor_error(const Error& error) -> preprocessor::Error {
    return preprocessor::Error::make(error.message.clone());
}

} // namespace tenon::toolchain

namespace tenon::toolchain
{

inline constexpr auto STANDARD_LIBRARY_HAS_BUILTIN = R"TENON(__add_pointer
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
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_FEATURE = R"TENON(address_sanitizer
cxx_atomic
experimental_library
modules
nullability
objc_arc
objc_arc_weak
ptrauth_calls
ptrauth_type_info_vtable_pointer_discrimination
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_EXTENSION = R"TENON(blocks
c_atomic
datasizeof
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_CPP_ATTRIBUTE =
    R"TENON(_Clang::acquire_shared_capability
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
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_ATTRIBUTE =
    R"TENON(acquire_capability
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
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_DECLSPEC_ATTRIBUTE = R"TENON(empty_bases
)TENON"_str;

inline constexpr auto STANDARD_LIBRARY_HAS_WARNING = R"TENON(-Winvalid-specialization
)TENON"_str;

auto append_standard_library_capabilities(Vec<preprocessor::BuiltinQueryKey>& result,
                                          preprocessor::BuiltinQueryKind      kind,
                                          ref<str>                            values) -> void {
    auto begin = usize {};
    while (begin < values.len()) {
        auto end = begin;
        while (end < values.len() && values.as_bytes()[end] != u8('\n')) ++end;
        auto value = values.get(begin, end);
        if (value.is_some() && ! value->is_empty()) {
            result.push(preprocessor::BuiltinQueryKey {
                .kind     = kind,
                .argument = String::make(*value),
            });
        }
        begin = end + usize(1);
    }
}

auto standard_library_capabilities() -> Vec<preprocessor::BuiltinQueryKey> {
    auto result = Vec<preprocessor::BuiltinQueryKey>::with_capacity(usize(96));
    append_standard_library_capabilities(
        result, preprocessor::BuiltinQueryKind::HasBuiltin, STANDARD_LIBRARY_HAS_BUILTIN);
    append_standard_library_capabilities(
        result, preprocessor::BuiltinQueryKind::HasFeature, STANDARD_LIBRARY_HAS_FEATURE);
    append_standard_library_capabilities(
        result, preprocessor::BuiltinQueryKind::HasExtension, STANDARD_LIBRARY_HAS_EXTENSION);
    append_standard_library_capabilities(result,
                                         preprocessor::BuiltinQueryKind::HasCppAttribute,
                                         STANDARD_LIBRARY_HAS_CPP_ATTRIBUTE);
    append_standard_library_capabilities(
        result, preprocessor::BuiltinQueryKind::HasAttribute, STANDARD_LIBRARY_HAS_ATTRIBUTE);
    append_standard_library_capabilities(result,
                                         preprocessor::BuiltinQueryKind::HasDeclspecAttribute,
                                         STANDARD_LIBRARY_HAS_DECLSPEC_ATTRIBUTE);
    append_standard_library_capabilities(
        result, preprocessor::BuiltinQueryKind::HasWarning, STANDARD_LIBRARY_HAS_WARNING);
    return result;
}

struct QueriedCapabilities {
    rstd::collections::HashMap<String, i64> values;
    usize                                   clang_count {};
    usize                                   native_count {};
    usize                                   input_bytes {};
    usize                                   output_bytes {};
};

auto parse_capability_value(ref<str> raw) -> Result<i64> {
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

auto render_capability_argument(const preprocessor::BuiltinQueryKey& query) -> String {
    if (query.kind == preprocessor::BuiltinQueryKind::HasWarning)
        return rstd::format("\"{}\"", query.argument.as_str());
    return query.argument.clone();
}

auto query_clang_capabilities(const Vec<String>&            base_command,
                              const BuiltinSemanticContext& semantic_context,
                              ref<rstd::path::Path>         working_directory)
    -> Result<QueriedCapabilities> {
    auto catalog      = standard_library_capabilities();
    auto pending      = Vec<preprocessor::BuiltinQueryKey>::make();
    auto source       = String::make();
    auto native_count = usize {};
    auto cursor       = usize {};
    while (cursor < catalog.len()) {
        auto kind    = catalog[cursor].kind;
        auto builtin = preprocessor::builtin_query_name(kind);
        source.push_str(rstd::format("#if !defined({})\n", builtin).as_str());
        source.push_str(rstd::format("#define {}(...) 0\n", builtin).as_str());
        source.push_str("#define TENON_DEFINED_QUERY_BUILTIN 1\n"_str);
        source.push_str("#endif\n"_str);
        while (cursor < catalog.len() && catalog[cursor].kind == kind) {
            if (native_capability(catalog[cursor], semantic_context).is_some()) {
                ++native_count;
                ++cursor;
                continue;
            }
            auto argument = render_capability_argument(catalog[cursor]);
            auto index    = pending.len();
            source.push_str(
                rstd::format("TENON_BUILTIN_QUERY_{} {}({})\n", index, builtin, argument.as_str())
                    .as_str());
            pending.push(catalog[cursor].clone());
            ++cursor;
        }
        source.push_str("#if defined(TENON_DEFINED_QUERY_BUILTIN)\n"_str);
        source.push_str(rstd::format("#undef {}\n", builtin).as_str());
        source.push_str("#undef TENON_DEFINED_QUERY_BUILTIN\n"_str);
        source.push_str("#endif\n"_str);
    }

    auto command_line = clone_command(base_command);
    command::push_option(command_line, clang_options::PREPROCESS);
    command::push_option(command_line, clang_options::NO_LINE_MARKERS);
    command::push_option(command_line, clang_options::LANGUAGE);
    command::push_option(command_line, clang_options::CXX_SOURCE);
    command::push_option(command_line, clang_options::STANDARD_INPUT);
    auto output = run_command_with_input(command_line, source.as_str(), Some(working_directory));
    if (output.is_err()) return Err(rstd::move(output).unwrap_err());
    if (output->exit_code != i32 {}) {
        return environment_failure<QueriedCapabilities>(
            rstd::format("clang builtin capability query failed\n{}\n{}",
                         command_text(command_line).as_str(),
                         output->standard_error.as_str()));
    }

    auto values = Vec<i64>::make();
    auto parsed =
        each_line(output->standard_output.as_str(), [&values](ref<str> raw) -> Result<empty> {
            auto line = raw.trim_ascii();
            if (line.is_empty()) return Ok(empty {});
            constexpr auto prefix = "TENON_BUILTIN_QUERY_"_str;
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
                return environment_failure<empty>(rstd::move(value).unwrap_err().message);
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

} // namespace tenon::toolchain

export namespace tenon::toolchain
{

auto query_clang_builtin_environment_snapshot(const Vec<String>&            base_command,
                                              ref<str>                      key,
                                              const BuiltinSemanticContext& semantic_context,
                                              ref<rstd::path::Path>         working_directory)
    -> Result<SharedClangBuiltinEnvironmentSnapshot> {
    auto macro_command = clone_command(base_command);
    command::push_option(macro_command, clang_options::DUMP_MACROS);
    command::push_option(macro_command, clang_options::PREPROCESS);
    command::push_option(macro_command, clang_options::LANGUAGE);
    command::push_option(macro_command, clang_options::CXX_SOURCE);
    command::push_option(macro_command, clang_options::STANDARD_INPUT);
    auto macro_output = run_command_with_input(macro_command, ""_str, Some(working_directory));
    if (macro_output.is_err()) return Err(rstd::move(macro_output).unwrap_err());
    if (macro_output->exit_code != i32 {}) {
        return environment_failure<SharedClangBuiltinEnvironmentSnapshot>(
            rstd::format("clang++ -dM failed\n{}\n{}",
                         command_text(macro_command).as_str(),
                         macro_output->standard_error.as_str()));
    }
    auto macros = parse_macro_dump(macro_output->standard_output.as_str());
    if (macros.is_err()) return Err(rstd::move(macros).unwrap_err());
    auto clang_macros = clang_owned_macro_seeds(*macros);
    auto parsed       = parse_macro_seeds(clang_macros, "<built-in>"_str);
    if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err());
    auto capabilities = query_clang_capabilities(base_command, semantic_context, working_directory);
    if (capabilities.is_err()) return Err(rstd::move(capabilities).unwrap_err());
    auto values            = rstd::move(parsed).unwrap();
    auto capability_values = rstd::move(capabilities).unwrap();
    auto identity          = builtin_snapshot_identity(clang_macros, key);
    return Ok(rstd::rc::make_rc<ClangBuiltinEnvironmentSnapshot>(
                  ClangBuiltinEnvironmentSnapshot {
                      .key                     = String::make(key),
                      .identity                = rstd::move(identity),
                      .source                  = rstd::move(values.source),
                      .definitions             = rstd::move(values.definitions),
                      .capabilities            = rstd::move(capability_values.values),
                      .clang_macro_count       = clang_macros.len(),
                      .native_macro_count      = usize(4),
                      .clang_capability_count  = capability_values.clang_count,
                      .native_capability_count = capability_values.native_count,
                      .macro_output_bytes      = macro_output->standard_output.len(),
                      .capability_input_bytes  = capability_values.input_bytes,
                      .capability_output_bytes = capability_values.output_bytes,
                  })
                  .to_const());
}

auto query_preprocessor_environment(const Vec<String>&                    base_command,
                                    PreprocessorEnvironmentKey            key,
                                    SharedClangBuiltinEnvironmentSnapshot builtin_environment,
                                    BuiltinSemanticContext                semantic_context,
                                    const Vec<String>&                    options,
                                    const Vec<String>&                    definitions)
    -> Result<PreprocessorEnvironment> {
    auto working_directory = key.working_directory.as_path();
    auto native_macros =
        parse_macro_seeds(native_predefined_macro_seeds(semantic_context), "<tenon-built-in>"_str);
    if (native_macros.is_err()) return Err(rstd::move(native_macros).unwrap_err());
    auto command_line_macros = parse_command_line_macros(options, definitions);
    if (command_line_macros.is_err()) return Err(rstd::move(command_line_macros).unwrap_err());
    auto native_values       = rstd::move(native_macros).unwrap();
    auto command_line_values = rstd::move(command_line_macros).unwrap();

    auto include_command = clone_command(base_command);
    command::push_option(include_command, clang_options::PREPROCESS);
    command::push_option(include_command, clang_options::VERBOSE);
    command::push_option(include_command, clang_options::LANGUAGE);
    command::push_option(include_command, clang_options::CXX_SOURCE);
    command::push_option(include_command, clang_options::STANDARD_INPUT);
    auto include_output = run_command_with_input(include_command, ""_str, Some(working_directory));
    if (include_output.is_err()) return Err(rstd::move(include_output).unwrap_err());
    if (include_output->exit_code != i32 {}) {
        return environment_failure<PreprocessorEnvironment>(
            rstd::format("clang++ -E -v failed\n{}\n{}",
                         command_text(include_command).as_str(),
                         include_output->standard_error.as_str()));
    }
    auto includes = parse_include_search(include_output->standard_error.as_str());
    if (includes.is_err()) return Err(rstd::move(includes).unwrap_err());
    auto identity = environment_identity(
        builtin_environment.get()->identity.as_str(), *includes, key.context_id.as_str());
    if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
    return Ok(PreprocessorEnvironment {
        .key                 = rstd::move(key),
        .builtin_environment = rstd::move(builtin_environment),
        .native_source       = rstd::move(native_values.source),
        .native_definitions  = rstd::move(native_values.definitions),
        .command_line_source = rstd::move(command_line_values.source),
        .command_line_macros = rstd::move(command_line_values.operations),
        .semantic_context    = rstd::move(semantic_context),
        .include_search      = rstd::move(includes).unwrap(),
        .query_command       = clone_command(base_command),
        .identity            = rstd::move(identity).unwrap(),
    });
}

class ClangIncludeResolver {
public:
    explicit ClangIncludeResolver(const PreprocessorEnvironment& environment)
        : environment_(environment) {}

    auto resolve(const preprocessor::IncludeRequest& request)
        -> preprocessor::Result<Option<preprocessor::IncludeResolution>> {
        auto dependency = frontend::IncludeLookupDependency {
            .kind                  = request.kind,
            .name                  = request.name.clone(),
            .including_path        = request.including_path.clone(),
            .previous_search_index = request.previous_search_index,
        };
        auto next   = request.kind == preprocessor::IncludeKind::NextQuoted ||
                      request.kind == preprocessor::IncludeKind::NextAngled;
        auto quoted = request.kind == preprocessor::IncludeKind::Quoted ||
                      request.kind == preprocessor::IncludeKind::NextQuoted;
        auto start  = next && request.previous_search_index.is_some()
                          ? *request.previous_search_index + usize(1)
                          : usize {};
        if (quoted && ! next && start == usize {}) {
            auto parent = request.including_path.as_path().parent();
            if (parent.is_some()) {
                auto resolved =
                    candidate(*parent, request.name.as_str(), usize {}, false, dependency);
                if (resolved.is_err()) return resolved;
                if (resolved->is_some()) {
                    dependencies_.push(rstd::move(dependency));
                    return resolved;
                }
            }
            start = usize(1);
        }
        if (start == usize {}) start = usize(1);
        for (auto index = start; index <= environment_.include_search.len(); ++index) {
            const auto& entry    = environment_.include_search[index - usize(1)];
            auto        resolved = candidate(
                entry.directory.as_path(), request.name.as_str(), index, entry.system, dependency);
            if (resolved.is_err()) return resolved;
            if (resolved->is_some()) {
                dependencies_.push(rstd::move(dependency));
                return resolved;
            }
        }
        dependencies_.push(rstd::move(dependency));
        return Ok(None());
    }

    auto take_dependencies() -> Vec<frontend::IncludeLookupDependency> {
        return rstd::move(dependencies_);
    }

private:
    auto candidate(ref<rstd::path::Path>              directory,
                   ref<str>                           name,
                   usize                              search_index,
                   bool                               system,
                   frontend::IncludeLookupDependency& dependency)
        -> preprocessor::Result<Option<preprocessor::IncludeResolution>> {
        auto requested = PathBuf::from(directory).join(PathBuf::from(name).as_path());
        auto exists    = rstd::fs::exists(requested.as_path());
        if (exists.is_err()) {
            return Err(
                preprocessor::Error::make(rstd::format("cannot inspect include candidate '{}': {}",
                                                       requested.as_path(),
                                                       rstd::move(exists).unwrap_err())));
        }
        if (! *exists) {
            dependency.missing_candidates.push(requested.clone());
            return Ok(None());
        }
        auto canonical = rstd::fs::canonicalize(requested.as_path());
        if (canonical.is_err()) {
            return Err(
                preprocessor::Error::make(rstd::format("cannot resolve include candidate '{}': {}",
                                                       requested.as_path(),
                                                       rstd::move(canonical).unwrap_err())));
        }
        auto canonical_path = rstd::move(canonical).unwrap();
        dependency.resolved = Some(frontend::ResolvedIncludeCandidate {
            .requested_path = requested.clone(),
            .canonical_path = canonical_path.clone(),
            .search_index   = search_index,
        });
        return Ok(Some(preprocessor::IncludeResolution {
            .path         = rstd::move(canonical_path),
            .search_index = search_index,
            .system       = system,
        }));
    }

    const PreprocessorEnvironment&         environment_;
    Vec<frontend::IncludeLookupDependency> dependencies_;
};

class ClangBuiltinProvider {
public:
    ClangBuiltinProvider(PreprocessorEnvironment& environment,
                         ref<rstd::path::Path>    working_directory)
        : environment_(environment), working_directory_(PathBuf::from(working_directory)) {}

    auto predefined_macros() -> preprocessor::Result<Vec<preprocessor::PredefinedMacroOperation>> {
        auto result = Vec<preprocessor::PredefinedMacroOperation>::with_capacity(
            environment_.builtin_environment.get()->definitions.len() +
            environment_.native_definitions.len() + environment_.command_line_macros.len());
        for (const auto& definition : environment_.builtin_environment.get()->definitions) {
            result.push(preprocessor::PredefinedMacroOperation::define(definition.clone()));
        }
        for (const auto& definition : environment_.native_definitions) {
            result.push(preprocessor::PredefinedMacroOperation::define(definition.clone()));
        }
        for (const auto& operation : environment_.command_line_macros) {
            if (operation.kind == preprocessor::PredefinedMacroOperationKind::Define) {
                result.push(
                    preprocessor::PredefinedMacroOperation::define(operation.definition->clone()));
            } else {
                result.push(
                    preprocessor::PredefinedMacroOperation::undefine(operation.name.clone()));
            }
        }
        return Ok(rstd::move(result));
    }

    auto evaluate(const preprocessor::BuiltinQueryKey& query) -> preprocessor::Result<i64> {
        auto native = native_capability(query, environment_.semantic_context);
        if (native.is_some()) return Ok(*native);
        auto key    = capability_key(query);
        auto cached = environment_.builtin_environment.get()->capabilities.get(key.as_str());
        if (cached.is_some()) return Ok(**cached);
        return Ok(i64 {});
    }

    auto text(preprocessor::BuiltinTextKind kind) -> preprocessor::Result<String> {
        if (environment_.date.is_none() || environment_.time.is_none()) {
            auto queried = query_text_builtins();
            if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
        }
        const auto& value =
            kind == preprocessor::BuiltinTextKind::Date ? environment_.date : environment_.time;
        if (value.is_none()) {
            return Err(
                preprocessor::Error::make("clang did not initialize a requested text builtin"_str));
        }
        return Ok(value->clone());
    }

private:
    auto query_text_builtins() -> preprocessor::Result<empty> {
        auto command_line = clone_command(environment_.query_command);
        command::push_option(command_line, clang_options::PREPROCESS);
        command::push_option(command_line, clang_options::NO_LINE_MARKERS);
        command::push_option(command_line, clang_options::LANGUAGE);
        command::push_option(command_line, clang_options::CXX_SOURCE);
        command::push_option(command_line, clang_options::STANDARD_INPUT);
        auto output =
            run_command_with_input(command_line,
                                   "TENON_BUILTIN_DATE __DATE__\nTENON_BUILTIN_TIME __TIME__\n"_str,
                                   Some(working_directory_.as_path()));
        if (output.is_err()) return Err(preprocessor_error(rstd::move(output).unwrap_err()));
        if (output->exit_code != i32 {}) {
            return Err(preprocessor::Error::make(rstd::format("clang text builtin query failed: {}",
                                                              output->standard_error.as_str())));
        }
        auto parsed =
            each_line(output->standard_output.as_str(), [this](ref<str> raw) -> Result<empty> {
                auto           line        = raw.trim_ascii();
                auto           value       = Option<ref<str>> {};
                auto           target      = static_cast<Option<String>*>(nullptr);
                constexpr auto date_prefix = "TENON_BUILTIN_DATE "_str;
                constexpr auto time_prefix = "TENON_BUILTIN_TIME "_str;
                if (line.starts_with(date_prefix)) {
                    value  = line.get(date_prefix.len(), line.len());
                    target = rstd::addressof(environment_.date);
                } else if (line.starts_with(time_prefix)) {
                    value  = line.get(time_prefix.len(), line.len());
                    target = rstd::addressof(environment_.time);
                } else if (! line.is_empty()) {
                    return environment_failure<empty>(
                        rstd::format("unexpected clang text builtin output: {}", line));
                }
                if (target == nullptr) return Ok(empty {});
                auto text = value->trim_ascii();
                if (text.len() < usize(2) || text.as_bytes()[usize {}] != u8('"') ||
                    text.as_bytes()[text.len() - usize(1)] != u8('"')) {
                    return environment_failure<empty>(
                        rstd::format("invalid clang text builtin value: {}", text));
                }
                auto inner = text.get(usize(1), text.len() - usize(1));
                if (inner.is_none()) {
                    return environment_failure<empty>("invalid clang text builtin boundary"_str);
                }
                *target = Some(String::make(*inner));
                return Ok(empty {});
            });
        if (parsed.is_err()) return Err(preprocessor_error(rstd::move(parsed).unwrap_err()));
        if (environment_.date.is_none() || environment_.time.is_none()) {
            return Err(preprocessor::Error::make(
                "clang text builtin query returned an incomplete snapshot"_str));
        }
        return Ok(empty {});
    }

    PreprocessorEnvironment& environment_;
    PathBuf                  working_directory_;
};

class ClangPragmaHandler {
public:
    auto handle(const preprocessor::PragmaRequest&)
        -> preprocessor::Result<preprocessor::PragmaOutcome> {
        return Ok(preprocessor::PragmaOutcome::Ignored);
    }
};

class DependencyEvents {
public:
    auto wants(preprocessor::EventKind kind) const -> bool {
        return kind == preprocessor::EventKind::IncludeResolved;
    }

    auto on_event(const preprocessor::Event& event) -> preprocessor::Result<empty> {
        if (event.kind != preprocessor::EventKind::IncludeResolved || event.path.is_none()) {
            return Ok(empty {});
        }
        auto text = event.path->as_path().to_str();
        if (text.is_none()) {
            return Err(preprocessor::Error::at(
                String::make("resolved include path is not valid UTF-8"_str), event.location));
        }
        if (! paths_.contains_key(*text)) {
            paths_.insert(String::make(*text), empty {});
            headers_.push(event.path->clone());
        }
        return Ok(empty {});
    }

    auto take_headers() -> Vec<PathBuf> {
        auto result = rstd::move(headers_);
        headers_    = Vec<PathBuf>::make();
        paths_      = rstd::collections::BTreeMap<String, empty>::make();
        return result;
    }

private:
    rstd::collections::BTreeMap<String, empty> paths_;
    Vec<PathBuf>                               headers_;
};

} // namespace tenon::toolchain
