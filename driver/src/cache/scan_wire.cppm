module;
#include <rstd/macro.hpp>

module lito.driver:cache.scan_wire;

import rstd;
import rstd.json;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::scan_cache_wire
{

struct File {
    String path;
    u64    size {};
    String fingerprint;
};

struct Source {
    String path;
    String relative;
    u64    size {};
    String fingerprint;
};

struct ResolvedLookup {
    String requested;
    String canonical;
    u64    search_index {};
};

struct IncludeLookup {
    String                 kind;
    String                 name;
    String                 including;
    Option<u64>            previous_search_index;
    Vec<String>            missing;
    Option<ResolvedLookup> resolved;
};

struct EmbedLookup {
    String                 kind;
    String                 name;
    String                 including;
    Vec<String>            missing;
    Option<ResolvedLookup> resolved;
};

struct ProvidedModule {
    String logical_name;
    bool   interface {};
};

struct DependencyLocation {
    String path;
    u64    line {};
};

struct ModuleImport {
    String             logical_name;
    DependencyLocation location;
    bool               exported {};
};

struct EmbeddedInput {
    String path;
    u64    size {};
    String digest;
    u64    offset {};
    u64    length {};
};

struct ExternalMacro {
    String         name;
    String         dependency_key;
    String         value_identity;
    String         state;
    Option<String> compiler_definition;
};

struct Snapshot {
    String                 source;
    Option<ProvidedModule> provided;
    Option<String>         implementation_module;
    Vec<ModuleImport>      imports;
    Vec<String>            header_inputs;
    Vec<EmbeddedInput>     embedded_inputs;
    Vec<ExternalMacro>     external_macros;
    String                 preprocessor_environment;
    u64                    input_bytes {};
};

struct Receipt {
    u64                version {};
    String             state;
    String             recipe;
    String             environment;
    String             target;
    String             context;
    String             source_origin;
    String             working_directory;
    String             external_macro_schema;
    Source             source;
    Vec<File>          files;
    Vec<IncludeLookup> include_lookups;
    Vec<EmbedLookup>   embed_lookups;
    Snapshot           result;
    String             receipt;
};

struct WritePath {
    ref<rstd::path::Path> value;
};

struct WriteSource {
    WritePath path;
    WritePath relative;
    u64       size {};
    ref<str>  fingerprint;
};

template<typename File>
struct WriteFile {
    const File* value {};
};

template<typename Files>
struct WriteFiles {
    const Files* values {};
};

template<typename Resolved>
struct WriteResolvedLookup {
    const Resolved* value {};
};

template<typename Resolved>
struct WriteOptionalResolvedLookup {
    const Option<Resolved>* value {};
};

struct WriteIncludeLookup {
    const frontend::IncludeLookupDependency* value {};
};

struct WriteIncludeLookups {
    const Vec<frontend::IncludeLookupDependency>* values {};
};

struct WriteEmbedLookup {
    const frontend::EmbedLookupDependency* value {};
};

struct WriteEmbedLookups {
    const Vec<frontend::EmbedLookupDependency>* values {};
};

struct WriteProvidedModule {
    const frontend::ProvidedModule* value {};
};

struct WriteOptionalProvidedModule {
    const Option<frontend::ProvidedModule>* value {};
};

struct WriteDependencyLocation {
    const frontend::DependencyLocation* value {};
};

struct WriteModuleImport {
    const frontend::ModuleImport* value {};
};

struct WriteModuleImports {
    const Vec<frontend::ModuleImport>* values {};
};

struct WritePaths {
    const Vec<rstd::path::PathBuf>* values {};
};

struct WriteEmbeddedInput {
    const frontend::EmbeddedInput* value {};
};

struct WriteEmbeddedInputs {
    const Vec<frontend::EmbeddedInput>* values {};
};

struct WriteExternalMacro {
    const frontend::ExternalMacroMaterialization* value {};
};

struct WriteExternalMacros {
    const Vec<frontend::ExternalMacroMaterialization>* values {};
};

struct WriteSnapshot {
    const frontend::FrontendResult* value {};
};

template<typename Files>
struct WriteReceipt {
    u64                                           version {};
    ref<str>                                      state;
    ref<str>                                      recipe;
    ref<str>                                      environment;
    ref<str>                                      target;
    ref<str>                                      context;
    ref<str>                                      source_origin;
    ref<str>                                      working_directory;
    ref<str>                                      external_macro_schema;
    WriteSource                                   source;
    const Files*                                  files {};
    const Vec<frontend::IncludeLookupDependency>* include_lookups {};
    const Vec<frontend::EmbedLookupDependency>*   embed_lookups {};
    const frontend::FrontendResult*               result {};
    ref<str>                                      receipt;
};

auto include_kind_name(frontend::IncludeLookupKind kind) -> ref<str> {
    switch (kind) {
    case frontend::IncludeLookupKind::Quoted: return "quoted"_str;
    case frontend::IncludeLookupKind::Angled: return "angled"_str;
    case frontend::IncludeLookupKind::NextQuoted: return "next-quoted"_str;
    case frontend::IncludeLookupKind::NextAngled: return "next-angled"_str;
    }
    __builtin_unreachable();
}

auto embed_kind_name(frontend::EmbedLookupKind kind) -> ref<str> {
    return kind == frontend::EmbedLookupKind::Quoted ? "quoted"_str : "angled"_str;
}

auto external_macro_state_name(frontend::ExternalMacroState state) -> ref<str> {
    return state == frontend::ExternalMacroState::Defined ? "defined"_str : "undefined"_str;
}

} // namespace lito::scan_cache_wire

namespace rstd
{

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WritePath> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::WritePath& value) ->
        typename Serializer::result_type {
        auto text = value.value.to_str();
        if (text.is_none()) return Err(serializer.invalid_value("path is not valid UTF-8"_str));
        return serializer.serialize_string(*text);
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteSource> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::WriteSource& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(4)));
        rstd_try(serde::field(map, "fingerprint"_str, value.fingerprint));
        rstd_try(serde::field(map, "path"_str, value.path));
        rstd_try(serde::field(map, "relative"_str, value.relative));
        rstd_try(serde::field(map, "size"_str, value.size));
        return map.end();
    }
};

template<typename File>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteFile<File>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteFile<File>& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(map, "fingerprint"_str, value.value->fingerprint));
        rstd_try(serde::field(map,
                              "path"_str,
                              lito::scan_cache_wire::WritePath { value.value->path.as_path() }));
        rstd_try(serde::field(map, "size"_str, value.value->size));
        return map.end();
    }
};

template<typename Files>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteFiles<Files>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteFiles<Files>& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        auto iter     = value.values->iter();
        for (auto item : iter) {
            using File = mtp::rm_cvf<decltype(*item.template get<1>())>;
            rstd_try(sequence.element(lito::scan_cache_wire::WriteFile<File> {
                rstd::addressof(*item.template get<1>()),
            }));
        }
        return sequence.end();
    }
};

template<typename Resolved>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteResolvedLookup<Resolved>> {
    template<typename Serializer>
    static auto serialize(
        Serializer& serializer,
        const lito::scan_cache_wire::WriteResolvedLookup<Resolved>& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(
            map,
            "canonical"_str,
            lito::scan_cache_wire::WritePath { value.value->canonical_path.as_path() }));
        rstd_try(serde::field(
            map,
            "requested"_str,
            lito::scan_cache_wire::WritePath { value.value->requested_path.as_path() }));
        rstd_try(serde::field(map, "search-index"_str, as_cast<u64>(value.value->search_index)));
        return map.end();
    }
};

template<typename Resolved>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteOptionalResolvedLookup<Resolved>> {
    template<typename Serializer>
    static auto serialize(
        Serializer& serializer,
        const lito::scan_cache_wire::WriteOptionalResolvedLookup<Resolved>& value) ->
        typename Serializer::result_type {
        if (value.value->is_none()) return serializer.serialize_none();
        return serde::serialize(
            serializer,
            lito::scan_cache_wire::WriteResolvedLookup<Resolved> { rstd::addressof(**value.value) });
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WritePaths> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::WritePaths& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& path : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WritePath { path.as_path() }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteIncludeLookup> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteIncludeLookup& value) ->
        typename Serializer::result_type {
        const auto& lookup = *value.value;
        auto        map    = rstd_try(serializer.begin_map(usize(6)));
        rstd_try(serde::field(
            map,
            "including"_str,
            lito::scan_cache_wire::WritePath { lookup.including_path.as_path() }));
        rstd_try(serde::field(
            map, "kind"_str, lito::scan_cache_wire::include_kind_name(lookup.kind)));
        rstd_try(serde::field(
            map,
            "missing"_str,
            lito::scan_cache_wire::WritePaths { rstd::addressof(lookup.missing_candidates) }));
        rstd_try(serde::field(map, "name"_str, lookup.name));
        auto previous = Option<u64> {};
        if (lookup.previous_search_index.is_some()) {
            previous = Some(as_cast<u64>(*lookup.previous_search_index));
        }
        rstd_try(serde::field(map, "previous-search-index"_str, previous));
        using Resolved = mtp::rm_cvf<decltype(*lookup.resolved)>;
        rstd_try(serde::field(
            map,
            "resolved"_str,
            lito::scan_cache_wire::WriteOptionalResolvedLookup<Resolved> {
                rstd::addressof(lookup.resolved),
            }));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteIncludeLookups> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteIncludeLookups& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& lookup : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WriteIncludeLookup { rstd::addressof(lookup) }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteEmbedLookup> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteEmbedLookup& value) ->
        typename Serializer::result_type {
        const auto& lookup = *value.value;
        auto        map    = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(
            map,
            "including"_str,
            lito::scan_cache_wire::WritePath { lookup.including_path.as_path() }));
        rstd_try(serde::field(
            map, "kind"_str, lito::scan_cache_wire::embed_kind_name(lookup.kind)));
        rstd_try(serde::field(
            map,
            "missing"_str,
            lito::scan_cache_wire::WritePaths { rstd::addressof(lookup.missing_candidates) }));
        rstd_try(serde::field(map, "name"_str, lookup.name));
        using Resolved = mtp::rm_cvf<decltype(*lookup.resolved)>;
        rstd_try(serde::field(
            map,
            "resolved"_str,
            lito::scan_cache_wire::WriteOptionalResolvedLookup<Resolved> {
                rstd::addressof(lookup.resolved),
            }));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteEmbedLookups> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteEmbedLookups& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& lookup : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WriteEmbedLookup { rstd::addressof(lookup) }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteProvidedModule> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteProvidedModule& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "interface"_str, value.value->is_interface));
        rstd_try(serde::field(map, "logical-name"_str, value.value->logical_name));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteOptionalProvidedModule> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteOptionalProvidedModule& value) ->
        typename Serializer::result_type {
        if (value.value->is_none()) return serializer.serialize_none();
        return serde::serialize(
            serializer,
            lito::scan_cache_wire::WriteProvidedModule { rstd::addressof(**value.value) });
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteDependencyLocation> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteDependencyLocation& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "line"_str, as_cast<u64>(value.value->line)));
        rstd_try(serde::field(
            map,
            "path"_str,
            lito::scan_cache_wire::WritePath { value.value->path.as_path() }));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteModuleImport> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteModuleImport& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(map, "exported"_str, value.value->exported));
        rstd_try(serde::field(
            map,
            "location"_str,
            lito::scan_cache_wire::WriteDependencyLocation {
                rstd::addressof(value.value->location),
            }));
        rstd_try(serde::field(map, "logical-name"_str, value.value->logical_name));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteModuleImports> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteModuleImports& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& imported : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WriteModuleImport { rstd::addressof(imported) }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteEmbeddedInput> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteEmbeddedInput& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(map, "digest"_str, value.value->digest));
        rstd_try(serde::field(map, "length"_str, as_cast<u64>(value.value->length)));
        rstd_try(serde::field(map, "offset"_str, as_cast<u64>(value.value->offset)));
        rstd_try(serde::field(
            map,
            "path"_str,
            lito::scan_cache_wire::WritePath { value.value->path.as_path() }));
        rstd_try(serde::field(map, "size"_str, value.value->size));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteEmbeddedInputs> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteEmbeddedInputs& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& embedded : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WriteEmbeddedInput { rstd::addressof(embedded) }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteExternalMacro> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteExternalMacro& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(map, "compiler-definition"_str, value.value->compiler_definition));
        rstd_try(serde::field(map, "dependency-key"_str, value.value->dependency_key));
        rstd_try(serde::field(map, "name"_str, value.value->name));
        rstd_try(serde::field(
            map,
            "state"_str,
            lito::scan_cache_wire::external_macro_state_name(value.value->state)));
        rstd_try(serde::field(map, "value-identity"_str, value.value->value_identity));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteExternalMacros> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteExternalMacros& value) ->
        typename Serializer::result_type {
        auto sequence = rstd_try(serializer.begin_sequence(value.values->len()));
        for (const auto& macro : *value.values) {
            rstd_try(sequence.element(
                lito::scan_cache_wire::WriteExternalMacro { rstd::addressof(macro) }));
        }
        return sequence.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteSnapshot> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteSnapshot& value) ->
        typename Serializer::result_type {
        const auto& result = *value.value;
        auto        map    = rstd_try(serializer.begin_map(usize(9)));
        rstd_try(serde::field(
            map,
            "embedded-inputs"_str,
            lito::scan_cache_wire::WriteEmbeddedInputs {
                rstd::addressof(result.embedded_inputs),
            }));
        rstd_try(serde::field(
            map,
            "external-macros"_str,
            lito::scan_cache_wire::WriteExternalMacros {
                rstd::addressof(result.external_macros),
            }));
        rstd_try(serde::field(
            map,
            "header-inputs"_str,
            lito::scan_cache_wire::WritePaths { rstd::addressof(result.header_inputs) }));
        rstd_try(serde::field(
            map, "implementation-module"_str, result.implementation_module));
        rstd_try(serde::field(
            map,
            "imports"_str,
            lito::scan_cache_wire::WriteModuleImports { rstd::addressof(result.imports) }));
        rstd_try(serde::field(map, "input-bytes"_str, as_cast<u64>(result.input_bytes)));
        rstd_try(serde::field(
            map, "preprocessor-environment"_str, result.preprocessor_environment));
        rstd_try(serde::field(
            map,
            "provided-module"_str,
            lito::scan_cache_wire::WriteOptionalProvidedModule {
                rstd::addressof(result.provided),
            }));
        rstd_try(serde::field(
            map,
            "source"_str,
            lito::scan_cache_wire::WritePath { result.source.as_path() }));
        return map.end();
    }
};

template<typename Files>
struct Impl<serde::Serialize, lito::scan_cache_wire::WriteReceipt<Files>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer,
                          const lito::scan_cache_wire::WriteReceipt<Files>& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(15)));
        rstd_try(serde::field(map, "context"_str, value.context));
        rstd_try(serde::field(
            map,
            "embed-lookups"_str,
            lito::scan_cache_wire::WriteEmbedLookups { value.embed_lookups }));
        rstd_try(serde::field(map, "environment"_str, value.environment));
        rstd_try(serde::field(
            map, "external-macro-schema"_str, value.external_macro_schema));
        rstd_try(serde::field(
            map,
            "files"_str,
            lito::scan_cache_wire::WriteFiles<Files> { value.files }));
        rstd_try(serde::field(
            map,
            "include-lookups"_str,
            lito::scan_cache_wire::WriteIncludeLookups { value.include_lookups }));
        rstd_try(serde::field(map, "receipt"_str, value.receipt));
        rstd_try(serde::field(map, "recipe"_str, value.recipe));
        rstd_try(serde::field(
            map, "result"_str, lito::scan_cache_wire::WriteSnapshot { value.result }));
        rstd_try(serde::field(map, "source"_str, value.source));
        rstd_try(serde::field(map, "source-origin"_str, value.source_origin));
        rstd_try(serde::field(map, "state"_str, value.state));
        rstd_try(serde::field(map, "target"_str, value.target));
        rstd_try(serde::field(map, "version"_str, value.version));
        rstd_try(serde::field(map, "working-directory"_str, value.working_directory));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::File> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::File& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(map, "fingerprint"_str, value.fingerprint));
        rstd_try(serde::field(map, "path"_str, value.path));
        rstd_try(serde::field(map, "size"_str, value.size));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::Source> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::Source& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(4)));
        rstd_try(serde::field(map, "fingerprint"_str, value.fingerprint));
        rstd_try(serde::field(map, "path"_str, value.path));
        rstd_try(serde::field(map, "relative"_str, value.relative));
        rstd_try(serde::field(map, "size"_str, value.size));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::ResolvedLookup> {
    template<typename Serializer>
    static auto serialize(Serializer&                                  serializer,
                          const lito::scan_cache_wire::ResolvedLookup& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(map, "canonical"_str, value.canonical));
        rstd_try(serde::field(map, "requested"_str, value.requested));
        rstd_try(serde::field(map, "search-index"_str, value.search_index));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::IncludeLookup> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::IncludeLookup& value)
        -> typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(6)));
        rstd_try(serde::field(map, "including"_str, value.including));
        rstd_try(serde::field(map, "kind"_str, value.kind));
        rstd_try(serde::field(map, "missing"_str, value.missing));
        rstd_try(serde::field(map, "name"_str, value.name));
        rstd_try(serde::field(map, "previous-search-index"_str, value.previous_search_index));
        rstd_try(serde::field(map, "resolved"_str, value.resolved));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::EmbedLookup> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::EmbedLookup& value)
        -> typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(map, "including"_str, value.including));
        rstd_try(serde::field(map, "kind"_str, value.kind));
        rstd_try(serde::field(map, "missing"_str, value.missing));
        rstd_try(serde::field(map, "name"_str, value.name));
        rstd_try(serde::field(map, "resolved"_str, value.resolved));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::ProvidedModule> {
    template<typename Serializer>
    static auto serialize(Serializer&                                  serializer,
                          const lito::scan_cache_wire::ProvidedModule& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "interface"_str, value.interface));
        rstd_try(serde::field(map, "logical-name"_str, value.logical_name));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::DependencyLocation> {
    template<typename Serializer>
    static auto serialize(Serializer&                                      serializer,
                          const lito::scan_cache_wire::DependencyLocation& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(2)));
        rstd_try(serde::field(map, "line"_str, value.line));
        rstd_try(serde::field(map, "path"_str, value.path));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::ModuleImport> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::ModuleImport& value)
        -> typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(3)));
        rstd_try(serde::field(map, "exported"_str, value.exported));
        rstd_try(serde::field(map, "location"_str, value.location));
        rstd_try(serde::field(map, "logical-name"_str, value.logical_name));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::EmbeddedInput> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::EmbeddedInput& value)
        -> typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(map, "digest"_str, value.digest));
        rstd_try(serde::field(map, "length"_str, value.length));
        rstd_try(serde::field(map, "offset"_str, value.offset));
        rstd_try(serde::field(map, "path"_str, value.path));
        rstd_try(serde::field(map, "size"_str, value.size));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::ExternalMacro> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::ExternalMacro& value)
        -> typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(5)));
        rstd_try(serde::field(map, "compiler-definition"_str, value.compiler_definition));
        rstd_try(serde::field(map, "dependency-key"_str, value.dependency_key));
        rstd_try(serde::field(map, "name"_str, value.name));
        rstd_try(serde::field(map, "state"_str, value.state));
        rstd_try(serde::field(map, "value-identity"_str, value.value_identity));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::Snapshot> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::Snapshot& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(9)));
        rstd_try(serde::field(map, "embedded-inputs"_str, value.embedded_inputs));
        rstd_try(serde::field(map, "external-macros"_str, value.external_macros));
        rstd_try(serde::field(map, "header-inputs"_str, value.header_inputs));
        rstd_try(serde::field(map, "implementation-module"_str, value.implementation_module));
        rstd_try(serde::field(map, "imports"_str, value.imports));
        rstd_try(serde::field(map, "input-bytes"_str, value.input_bytes));
        rstd_try(serde::field(map, "preprocessor-environment"_str, value.preprocessor_environment));
        rstd_try(serde::field(map, "provided-module"_str, value.provided));
        rstd_try(serde::field(map, "source"_str, value.source));
        return map.end();
    }
};

template<>
struct Impl<serde::Serialize, lito::scan_cache_wire::Receipt> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const lito::scan_cache_wire::Receipt& value) ->
        typename Serializer::result_type {
        auto map = rstd_try(serializer.begin_map(usize(15)));
        rstd_try(serde::field(map, "context"_str, value.context));
        rstd_try(serde::field(map, "embed-lookups"_str, value.embed_lookups));
        rstd_try(serde::field(map, "environment"_str, value.environment));
        rstd_try(serde::field(map, "external-macro-schema"_str, value.external_macro_schema));
        rstd_try(serde::field(map, "files"_str, value.files));
        rstd_try(serde::field(map, "include-lookups"_str, value.include_lookups));
        rstd_try(serde::field(map, "receipt"_str, value.receipt));
        rstd_try(serde::field(map, "recipe"_str, value.recipe));
        rstd_try(serde::field(map, "result"_str, value.result));
        rstd_try(serde::field(map, "source"_str, value.source));
        rstd_try(serde::field(map, "source-origin"_str, value.source_origin));
        rstd_try(serde::field(map, "state"_str, value.state));
        rstd_try(serde::field(map, "target"_str, value.target));
        rstd_try(serde::field(map, "version"_str, value.version));
        rstd_try(serde::field(map, "working-directory"_str, value.working_directory));
        return map.end();
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::File> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::File, typename Deserializer::error_type> {
        auto path        = serde::RequiredField<String>("path"_str);
        auto size        = serde::RequiredField<u64>("size"_str);
        auto fingerprint = serde::RequiredField<String>("fingerprint"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, path, size, fingerprint));
        return Ok(lito::scan_cache_wire::File {
            .path        = rstd_try(path.take(deserializer)),
            .size        = rstd_try(size.take(deserializer)),
            .fingerprint = rstd_try(fingerprint.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::Source> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::Source, typename Deserializer::error_type> {
        auto path        = serde::RequiredField<String>("path"_str);
        auto relative    = serde::RequiredField<String>("relative"_str);
        auto size        = serde::RequiredField<u64>("size"_str);
        auto fingerprint = serde::RequiredField<String>("fingerprint"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, path, relative, size, fingerprint));
        return Ok(lito::scan_cache_wire::Source {
            .path        = rstd_try(path.take(deserializer)),
            .relative    = rstd_try(relative.take(deserializer)),
            .size        = rstd_try(size.take(deserializer)),
            .fingerprint = rstd_try(fingerprint.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::ResolvedLookup> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::ResolvedLookup, typename Deserializer::error_type> {
        auto requested    = serde::RequiredField<String>("requested"_str);
        auto canonical    = serde::RequiredField<String>("canonical"_str);
        auto search_index = serde::RequiredField<u64>("search-index"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, requested, canonical, search_index));
        return Ok(lito::scan_cache_wire::ResolvedLookup {
            .requested    = rstd_try(requested.take(deserializer)),
            .canonical    = rstd_try(canonical.take(deserializer)),
            .search_index = rstd_try(search_index.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::IncludeLookup> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::IncludeLookup, typename Deserializer::error_type> {
        auto kind      = serde::RequiredField<String>("kind"_str);
        auto name      = serde::RequiredField<String>("name"_str);
        auto including = serde::RequiredField<String>("including"_str);
        auto previous  = serde::RequiredField<Option<u64>>("previous-search-index"_str);
        auto missing   = serde::RequiredField<Vec<String>>("missing"_str);
        auto resolved =
            serde::RequiredField<Option<lito::scan_cache_wire::ResolvedLookup>>("resolved"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           kind,
                                           name,
                                           including,
                                           previous,
                                           missing,
                                           resolved));
        return Ok(lito::scan_cache_wire::IncludeLookup {
            .kind                  = rstd_try(kind.take(deserializer)),
            .name                  = rstd_try(name.take(deserializer)),
            .including             = rstd_try(including.take(deserializer)),
            .previous_search_index = rstd_try(previous.take(deserializer)),
            .missing               = rstd_try(missing.take(deserializer)),
            .resolved              = rstd_try(resolved.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::EmbedLookup> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::EmbedLookup, typename Deserializer::error_type> {
        auto kind      = serde::RequiredField<String>("kind"_str);
        auto name      = serde::RequiredField<String>("name"_str);
        auto including = serde::RequiredField<String>("including"_str);
        auto missing   = serde::RequiredField<Vec<String>>("missing"_str);
        auto resolved =
            serde::RequiredField<Option<lito::scan_cache_wire::ResolvedLookup>>("resolved"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           kind,
                                           name,
                                           including,
                                           missing,
                                           resolved));
        return Ok(lito::scan_cache_wire::EmbedLookup {
            .kind      = rstd_try(kind.take(deserializer)),
            .name      = rstd_try(name.take(deserializer)),
            .including = rstd_try(including.take(deserializer)),
            .missing   = rstd_try(missing.take(deserializer)),
            .resolved  = rstd_try(resolved.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::ProvidedModule> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::ProvidedModule, typename Deserializer::error_type> {
        auto logical_name = serde::RequiredField<String>("logical-name"_str);
        auto interface    = serde::RequiredField<bool>("interface"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, logical_name, interface));
        return Ok(lito::scan_cache_wire::ProvidedModule {
            .logical_name = rstd_try(logical_name.take(deserializer)),
            .interface    = rstd_try(interface.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::DependencyLocation> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::DependencyLocation, typename Deserializer::error_type> {
        auto path = serde::RequiredField<String>("path"_str);
        auto line = serde::RequiredField<u64>("line"_str);
        rstd_try(
            serde::deserialize_record(deserializer, serde::UnknownFieldPolicy::Reject, path, line));
        return Ok(lito::scan_cache_wire::DependencyLocation {
            .path = rstd_try(path.take(deserializer)),
            .line = rstd_try(line.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::ModuleImport> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::ModuleImport, typename Deserializer::error_type> {
        auto logical_name = serde::RequiredField<String>("logical-name"_str);
        auto location =
            serde::RequiredField<lito::scan_cache_wire::DependencyLocation>("location"_str);
        auto exported = serde::RequiredField<bool>("exported"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, logical_name, location, exported));
        return Ok(lito::scan_cache_wire::ModuleImport {
            .logical_name = rstd_try(logical_name.take(deserializer)),
            .location     = rstd_try(location.take(deserializer)),
            .exported     = rstd_try(exported.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::EmbeddedInput> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::EmbeddedInput, typename Deserializer::error_type> {
        auto path   = serde::RequiredField<String>("path"_str);
        auto size   = serde::RequiredField<u64>("size"_str);
        auto digest = serde::RequiredField<String>("digest"_str);
        auto offset = serde::RequiredField<u64>("offset"_str);
        auto length = serde::RequiredField<u64>("length"_str);
        rstd_try(serde::deserialize_record(
            deserializer, serde::UnknownFieldPolicy::Reject, path, size, digest, offset, length));
        return Ok(lito::scan_cache_wire::EmbeddedInput {
            .path   = rstd_try(path.take(deserializer)),
            .size   = rstd_try(size.take(deserializer)),
            .digest = rstd_try(digest.take(deserializer)),
            .offset = rstd_try(offset.take(deserializer)),
            .length = rstd_try(length.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::ExternalMacro> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::ExternalMacro, typename Deserializer::error_type> {
        auto name           = serde::RequiredField<String>("name"_str);
        auto dependency_key = serde::RequiredField<String>("dependency-key"_str);
        auto value_identity = serde::RequiredField<String>("value-identity"_str);
        auto state          = serde::RequiredField<String>("state"_str);
        auto definition     = serde::RequiredField<Option<String>>("compiler-definition"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           name,
                                           dependency_key,
                                           value_identity,
                                           state,
                                           definition));
        return Ok(lito::scan_cache_wire::ExternalMacro {
            .name                = rstd_try(name.take(deserializer)),
            .dependency_key      = rstd_try(dependency_key.take(deserializer)),
            .value_identity      = rstd_try(value_identity.take(deserializer)),
            .state               = rstd_try(state.take(deserializer)),
            .compiler_definition = rstd_try(definition.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::Snapshot> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::Snapshot, typename Deserializer::error_type> {
        auto source   = serde::RequiredField<String>("source"_str);
        auto provided = serde::RequiredField<Option<lito::scan_cache_wire::ProvidedModule>>(
            "provided-module"_str);
        auto implementation = serde::RequiredField<Option<String>>("implementation-module"_str);
        auto imports =
            serde::RequiredField<Vec<lito::scan_cache_wire::ModuleImport>>("imports"_str);
        auto headers = serde::RequiredField<Vec<String>>("header-inputs"_str);
        auto embedded =
            serde::RequiredField<Vec<lito::scan_cache_wire::EmbeddedInput>>("embedded-inputs"_str);
        auto macros =
            serde::RequiredField<Vec<lito::scan_cache_wire::ExternalMacro>>("external-macros"_str);
        auto environment = serde::RequiredField<String>("preprocessor-environment"_str);
        auto input_bytes = serde::RequiredField<u64>("input-bytes"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           source,
                                           provided,
                                           implementation,
                                           imports,
                                           headers,
                                           embedded,
                                           macros,
                                           environment,
                                           input_bytes));
        return Ok(lito::scan_cache_wire::Snapshot {
            .source                   = rstd_try(source.take(deserializer)),
            .provided                 = rstd_try(provided.take(deserializer)),
            .implementation_module    = rstd_try(implementation.take(deserializer)),
            .imports                  = rstd_try(imports.take(deserializer)),
            .header_inputs            = rstd_try(headers.take(deserializer)),
            .embedded_inputs          = rstd_try(embedded.take(deserializer)),
            .external_macros          = rstd_try(macros.take(deserializer)),
            .preprocessor_environment = rstd_try(environment.take(deserializer)),
            .input_bytes              = rstd_try(input_bytes.take(deserializer)),
        });
    }
};

template<>
struct Impl<serde::Deserialize, lito::scan_cache_wire::Receipt> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<lito::scan_cache_wire::Receipt, typename Deserializer::error_type> {
        auto version               = serde::RequiredField<u64>("version"_str);
        auto state                 = serde::RequiredField<String>("state"_str);
        auto recipe                = serde::RequiredField<String>("recipe"_str);
        auto environment           = serde::RequiredField<String>("environment"_str);
        auto target                = serde::RequiredField<String>("target"_str);
        auto context               = serde::RequiredField<String>("context"_str);
        auto source_origin         = serde::RequiredField<String>("source-origin"_str);
        auto working_directory     = serde::RequiredField<String>("working-directory"_str);
        auto external_macro_schema = serde::RequiredField<String>("external-macro-schema"_str);
        auto source = serde::RequiredField<lito::scan_cache_wire::Source>("source"_str);
        auto files  = serde::RequiredField<Vec<lito::scan_cache_wire::File>>("files"_str);
        auto include_lookups =
            serde::RequiredField<Vec<lito::scan_cache_wire::IncludeLookup>>("include-lookups"_str);
        auto embed_lookups =
            serde::RequiredField<Vec<lito::scan_cache_wire::EmbedLookup>>("embed-lookups"_str);
        auto result  = serde::RequiredField<lito::scan_cache_wire::Snapshot>("result"_str);
        auto receipt = serde::RequiredField<String>("receipt"_str);
        rstd_try(serde::deserialize_record(deserializer,
                                           serde::UnknownFieldPolicy::Reject,
                                           version,
                                           state,
                                           recipe,
                                           environment,
                                           target,
                                           context,
                                           source_origin,
                                           working_directory,
                                           external_macro_schema,
                                           source,
                                           files,
                                           include_lookups,
                                           embed_lookups,
                                           result,
                                           receipt));
        return Ok(lito::scan_cache_wire::Receipt {
            .version               = rstd_try(version.take(deserializer)),
            .state                 = rstd_try(state.take(deserializer)),
            .recipe                = rstd_try(recipe.take(deserializer)),
            .environment           = rstd_try(environment.take(deserializer)),
            .target                = rstd_try(target.take(deserializer)),
            .context               = rstd_try(context.take(deserializer)),
            .source_origin         = rstd_try(source_origin.take(deserializer)),
            .working_directory     = rstd_try(working_directory.take(deserializer)),
            .external_macro_schema = rstd_try(external_macro_schema.take(deserializer)),
            .source                = rstd_try(source.take(deserializer)),
            .files                 = rstd_try(files.take(deserializer)),
            .include_lookups       = rstd_try(include_lookups.take(deserializer)),
            .embed_lookups         = rstd_try(embed_lookups.take(deserializer)),
            .result                = rstd_try(result.take(deserializer)),
            .receipt               = rstd_try(receipt.take(deserializer)),
        });
    }
};

} // namespace rstd
