module lito.driver:cache.scan;

import rstd;
import rstd.json;
import lito.core;
import lito.cpp;
import lito.frontend;
import lito.toolchain;
import :build.layout;
import :cache.hash;
import :cache.common;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

namespace lito
{

enum class ScanCacheMissReason
{
    None,
    Absent,
    Refresh,
    Version,
    Recipe,
    Corrupt,
    Environment,
    Context,
    Source,
    FileDependency,
    IncludeLookup,
    ExternalMacro,
    Receipt,
};

struct ScanCacheStatistics {
    usize                hits {};
    usize                misses {};
    usize                uncacheable {};
    usize                absent {};
    usize                refresh {};
    usize                version {};
    usize                recipe {};
    usize                corrupt {};
    usize                environment {};
    usize                context {};
    usize                source {};
    usize                file_dependency {};
    usize                include_lookup {};
    usize                external_macro {};
    usize                receipt {};
    usize                fingerprint_requests {};
    usize                fingerprint_hits {};
    usize                fingerprint_builds {};
    usize                fingerprint_waits {};
    rstd::time::Duration fingerprint_wait;
};

struct ScanCacheInput {
    PathBuf                              record;
    String                               target;
    PathBuf                              relative_source;
    String                               source_origin_identity;
    PathBuf                              source;
    String                               context_identity;
    PathBuf                              working_directory;
    String                               preprocessor_environment;
    String                               external_macro_schema;
    toolchain::SharedPackageMacroCatalog external_macros;
};

struct ScanCacheLookup {
    Option<frontend::FrontendAnalysis> hit;
    ScanCacheMissReason                reason { ScanCacheMissReason::Absent };
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

auto parse_include_kind(ref<str> value) -> Option<frontend::IncludeLookupKind> {
    if (value == "quoted"_str) return Some(frontend::IncludeLookupKind::Quoted);
    if (value == "angled"_str) return Some(frontend::IncludeLookupKind::Angled);
    if (value == "next-quoted"_str) return Some(frontend::IncludeLookupKind::NextQuoted);
    if (value == "next-angled"_str) return Some(frontend::IncludeLookupKind::NextAngled);
    return None();
}

auto external_macro_state_name(frontend::ExternalMacroState state) -> ref<str> {
    return state == frontend::ExternalMacroState::Defined ? "defined"_str : "undefined"_str;
}

auto parse_external_macro_state(ref<str> value) -> Option<frontend::ExternalMacroState> {
    if (value == "defined"_str) return Some(frontend::ExternalMacroState::Defined);
    if (value == "undefined"_str) return Some(frontend::ExternalMacroState::Undefined);
    return None();
}

auto external_macro_json(const frontend::ExternalMacroMaterialization& macro) -> Json {
    auto definition = Json::Null();
    if (macro.compiler_definition.is_some()) {
        definition = cache_string(macro.compiler_definition->as_str());
    }
    auto value = JsonMap::make();
    value.insert(String::make("compiler-definition"_str), rstd::move(definition));
    value.insert(String::make("dependency-key"_str), cache_string(macro.dependency_key.as_str()));
    value.insert(String::make("name"_str), cache_string(macro.name.as_str()));
    value.insert(String::make("state"_str), cache_string(external_macro_state_name(macro.state)));
    value.insert(String::make("value-identity"_str), cache_string(macro.value_identity.as_str()));
    return Json::Object(rstd::move(value));
}

auto parse_external_macro(ref<Json> value) -> Option<frontend::ExternalMacroMaterialization> {
    if (! value->is_object()) return None();
    auto name       = json_text(value, "name"_str);
    auto dependency = json_text(value, "dependency-key"_str);
    auto identity   = json_text(value, "value-identity"_str);
    auto state_text = json_text(value, "state"_str);
    auto definition = json_member(value, "compiler-definition"_str);
    if (name.is_none() || name->is_empty() || dependency.is_none() || dependency->is_empty() ||
        identity.is_none() || identity->is_empty() || state_text.is_none() ||
        definition.is_none()) {
        return None();
    }
    auto state = parse_external_macro_state(*state_text);
    if (state.is_none()) return None();
    auto compiler_definition = Option<String> {};
    if (! (**definition).is_null()) {
        auto text = (**definition).as_str();
        if (text.is_none()) return None();
        compiler_definition = Some(String::make(*text));
    }
    if ((*state == frontend::ExternalMacroState::Defined) != compiler_definition.is_some()) {
        return None();
    }
    return Some(frontend::ExternalMacroMaterialization {
        .name                = String::make(*name),
        .dependency_key      = String::make(*dependency),
        .value_identity      = String::make(*identity),
        .state               = *state,
        .compiler_definition = rstd::move(compiler_definition),
    });
}

auto path_json(ref<rstd::path::Path> path) -> CacheResult<Json> {
    auto text = path_string(path);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    return Ok(cache_string(text->as_str()));
}

auto include_lookup_json(const frontend::IncludeLookupDependency& lookup) -> CacheResult<Json> {
    auto including = path_string(lookup.including_path.as_path());
    if (including.is_err()) return Err(rstd::move(including).unwrap_err());
    auto missing = JsonArray::make();
    for (const auto& candidate : lookup.missing_candidates) {
        auto encoded = path_json(candidate.as_path());
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        missing.push(rstd::move(encoded).unwrap());
    }
    auto previous = Json::Null();
    if (lookup.previous_search_index.is_some()) {
        previous = cache_u64(as_cast<u64>(*lookup.previous_search_index));
    }
    auto resolved = Json::Null();
    if (lookup.resolved.is_some()) {
        auto requested = path_string(lookup.resolved->requested_path.as_path());
        auto canonical = path_string(lookup.resolved->canonical_path.as_path());
        if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
        if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
        auto value = JsonMap::make();
        value.insert(String::make("canonical"_str), cache_string(canonical->as_str()));
        value.insert(String::make("requested"_str), cache_string(requested->as_str()));
        value.insert(String::make("search-index"_str),
                     cache_u64(as_cast<u64>(lookup.resolved->search_index)));
        resolved = Json::Object(rstd::move(value));
    }
    auto value = JsonMap::make();
    value.insert(String::make("including"_str), cache_string(including->as_str()));
    value.insert(String::make("kind"_str), cache_string(include_kind_name(lookup.kind)));
    value.insert(String::make("missing"_str), Json::Array(rstd::move(missing)));
    value.insert(String::make("name"_str), cache_string(lookup.name.as_str()));
    value.insert(String::make("previous-search-index"_str), rstd::move(previous));
    value.insert(String::make("resolved"_str), rstd::move(resolved));
    return Ok(Json::Object(rstd::move(value)));
}

auto parse_include_lookup(ref<Json> value) -> Option<frontend::IncludeLookupDependency> {
    if (! value->is_object()) return None();
    auto kind_text      = json_text(value, "kind"_str);
    auto name           = json_text(value, "name"_str);
    auto including      = json_text(value, "including"_str);
    auto missing_values = json_array(value, "missing"_str);
    auto previous_value = json_member(value, "previous-search-index"_str);
    auto resolved_value = json_member(value, "resolved"_str);
    if (kind_text.is_none() || name.is_none() || name->is_empty() || including.is_none() ||
        missing_values.is_none() || previous_value.is_none() || resolved_value.is_none()) {
        return None();
    }
    auto kind = parse_include_kind(*kind_text);
    if (kind.is_none()) return None();
    auto previous = Option<usize> {};
    if (! (**previous_value).is_null()) {
        auto number = (**previous_value).as_u64();
        if (number.is_none()) return None();
        previous = Some(usize(static_cast<size_t>(number->to_primitive())));
    }
    auto missing = Vec<PathBuf>::with_capacity((**missing_values).len());
    for (const auto& candidate : **missing_values) {
        auto path = json_path(ref<Json>::from_raw_parts(rstd::addressof(candidate)));
        if (path.is_none()) return None();
        missing.push(rstd::move(path).unwrap());
    }
    auto resolved = Option<frontend::ResolvedIncludeCandidate> {};
    if (! (**resolved_value).is_null()) {
        auto requested    = json_text(*resolved_value, "requested"_str);
        auto canonical    = json_text(*resolved_value, "canonical"_str);
        auto search_index = json_number(*resolved_value, "search-index"_str);
        if (requested.is_none() || canonical.is_none() || search_index.is_none()) return None();
        resolved = Some(frontend::ResolvedIncludeCandidate {
            .requested_path = PathBuf::from(*requested),
            .canonical_path = PathBuf::from(*canonical),
            .search_index   = usize(static_cast<size_t>(search_index->to_primitive())),
        });
    }
    return Some(frontend::IncludeLookupDependency {
        .kind                  = *kind,
        .name                  = String::make(*name),
        .including_path        = PathBuf::from(*including),
        .previous_search_index = previous,
        .missing_candidates    = rstd::move(missing),
        .resolved              = rstd::move(resolved),
    });
}

auto snapshot_json(const frontend::FrontendSnapshot& snapshot) -> CacheResult<Json> {
    auto source = path_string(snapshot.source.as_path());
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    auto provided = Json::Null();
    if (snapshot.provided.is_some()) {
        auto value = JsonMap::make();
        value.insert(String::make("interface"_str), Json::Bool(snapshot.provided->is_interface));
        value.insert(String::make("logical-name"_str),
                     cache_string(snapshot.provided->logical_name.as_str()));
        provided = Json::Object(rstd::move(value));
    }
    auto implementation = Json::Null();
    if (snapshot.implementation_module.is_some()) {
        implementation = cache_string(snapshot.implementation_module->as_str());
    }
    auto imports = JsonArray::make();
    for (const auto& imported : snapshot.imports) {
        auto path = path_string(imported.location.path.as_path());
        if (path.is_err()) return Err(rstd::move(path).unwrap_err());
        auto location = JsonMap::make();
        location.insert(String::make("line"_str), cache_u64(as_cast<u64>(imported.location.line)));
        location.insert(String::make("path"_str), cache_string(path->as_str()));
        auto value = JsonMap::make();
        value.insert(String::make("exported"_str), Json::Bool(imported.exported));
        value.insert(String::make("location"_str), Json::Object(rstd::move(location)));
        value.insert(String::make("logical-name"_str),
                     cache_string(imported.logical_name.as_str()));
        imports.push(Json::Object(rstd::move(value)));
    }
    auto headers = JsonArray::make();
    for (const auto& header : snapshot.header_inputs) {
        auto encoded = path_json(header.as_path());
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        headers.push(rstd::move(encoded).unwrap());
    }
    auto external_macros = JsonArray::with_capacity(snapshot.external_macros.len());
    for (const auto& macro : snapshot.external_macros) {
        external_macros.push(external_macro_json(macro));
    }
    auto value = JsonMap::make();
    value.insert(String::make("external-macros"_str), Json::Array(rstd::move(external_macros)));
    value.insert(String::make("header-inputs"_str), Json::Array(rstd::move(headers)));
    value.insert(String::make("implementation-module"_str), rstd::move(implementation));
    value.insert(String::make("imports"_str), Json::Array(rstd::move(imports)));
    value.insert(String::make("input-bytes"_str), cache_u64(as_cast<u64>(snapshot.input_bytes)));
    value.insert(String::make("preprocessor-environment"_str),
                 cache_string(snapshot.preprocessor_environment.as_str()));
    value.insert(String::make("provided-module"_str), rstd::move(provided));
    value.insert(String::make("source"_str), cache_string(source->as_str()));
    return Ok(Json::Object(rstd::move(value)));
}

auto parse_snapshot(ref<Json> value) -> Option<frontend::FrontendSnapshot> {
    if (! value->is_object()) return None();
    auto source                = json_text(value, "source"_str);
    auto environment           = json_text(value, "preprocessor-environment"_str);
    auto input_bytes           = json_number(value, "input-bytes"_str);
    auto provided_value        = json_member(value, "provided-module"_str);
    auto implementation_value  = json_member(value, "implementation-module"_str);
    auto import_values         = json_array(value, "imports"_str);
    auto header_values         = json_array(value, "header-inputs"_str);
    auto external_macro_values = json_array(value, "external-macros"_str);
    if (source.is_none() || environment.is_none() || input_bytes.is_none() ||
        provided_value.is_none() || implementation_value.is_none() || import_values.is_none() ||
        header_values.is_none() || external_macro_values.is_none()) {
        return None();
    }
    auto provided = Option<frontend::ProvidedModule> {};
    if (! (**provided_value).is_null()) {
        auto name             = json_text(*provided_value, "logical-name"_str);
        auto interface_member = json_member(*provided_value, "interface"_str);
        if (name.is_none() || interface_member.is_none()) return None();
        auto interface = (**interface_member).as_bool();
        if (interface.is_none()) return None();
        provided = Some(frontend::ProvidedModule {
            .logical_name = String::make(*name),
            .is_interface = *interface,
        });
    }
    auto implementation = Option<String> {};
    if (! (**implementation_value).is_null()) {
        auto text = (**implementation_value).as_str();
        if (text.is_none()) return None();
        implementation = Some(String::make(*text));
    }
    auto imports = Vec<frontend::ModuleImport>::with_capacity((**import_values).len());
    for (const auto& item : **import_values) {
        auto item_ref = ref<Json>::from_raw_parts(rstd::addressof(item));
        auto name     = json_text(item_ref, "logical-name"_str);
        auto location = json_member(item_ref, "location"_str);
        auto exported = json_member(item_ref, "exported"_str);
        if (name.is_none() || location.is_none() || exported.is_none()) return None();
        auto is_exported = (**exported).as_bool();
        if (is_exported.is_none()) return None();
        auto path = json_text(*location, "path"_str);
        auto line = json_number(*location, "line"_str);
        if (path.is_none() || line.is_none()) return None();
        imports.push(frontend::ModuleImport {
            .logical_name = String::make(*name),
            .location =
                frontend::DependencyLocation {
                    .path = PathBuf::from(*path),
                    .line = usize(static_cast<size_t>(line->to_primitive())),
                },
            .exported = *is_exported,
        });
    }
    auto headers = Vec<PathBuf>::with_capacity((**header_values).len());
    for (const auto& item : **header_values) {
        auto path = json_path(ref<Json>::from_raw_parts(rstd::addressof(item)));
        if (path.is_none()) return None();
        headers.push(rstd::move(path).unwrap());
    }
    auto external_macros =
        Vec<frontend::ExternalMacroMaterialization>::with_capacity((**external_macro_values).len());
    for (const auto& item : **external_macro_values) {
        auto macro = parse_external_macro(ref<Json>::from_raw_parts(rstd::addressof(item)));
        if (macro.is_none()) return None();
        external_macros.push(rstd::move(macro).unwrap());
    }
    return Some(frontend::FrontendSnapshot {
        .source                   = PathBuf::from(*source),
        .provided                 = rstd::move(provided),
        .implementation_module    = rstd::move(implementation),
        .imports                  = rstd::move(imports),
        .header_inputs            = rstd::move(headers),
        .external_macros          = rstd::move(external_macros),
        .preprocessor_environment = String::make(*environment),
        .input_bytes              = usize(static_cast<size_t>(input_bytes->to_primitive())),
    });
}

class ScanCacheTransaction;

class ScanCacheSession {
    using SharedFingerprintError = rstd::sync::Arc<CacheError>;
    using FingerprintResult      = Result<FileFingerprint, SharedFingerprintError>;
    using FingerprintCell        = rstd::sync::OnceLock<FingerprintResult>;
    using SharedFingerprintCell  = rstd::sync::Arc<FingerprintCell>;

    struct FingerprintFields {
        rstd::collections::HashMap<String, SharedFingerprintCell> entries;

        FingerprintFields()
            : entries(rstd::collections::HashMap<String, SharedFingerprintCell>::make()) {}
    };

    struct State {
        String                                 environment;
        bool                                   force_refresh { false };
        rstd::sync::Mutex<FingerprintFields>   fingerprints;
        rstd::sync::Mutex<ScanCacheStatistics> statistics;

        State(String environment, bool force_refresh)
            : environment(rstd::move(environment)),
              force_refresh(force_refresh),
              fingerprints(FingerprintFields {}),
              statistics(ScanCacheStatistics {}) {}
    };

    rstd::sync::Arc<State> state_;

    explicit ScanCacheSession(rstd::sync::Arc<State> state): state_(rstd::move(state)) {}

    static auto clone_error(const SharedFingerprintError& error) -> CacheError {
        if (error->is_Record()) return CacheError::Record(error->as_Record().message.clone());
        const auto& value = error->as_SharedIo();
        return CacheError::SharedIo(
            value.operation.clone(), value.path.clone(), value.source.clone());
    }

    static auto clone_fingerprint_result(const FingerprintResult& value)
        -> CacheResult<FileFingerprint> {
        auto borrowed = value.as_ref();
        if (borrowed.is_err()) return Err(clone_error(borrowed.unwrap_err_unchecked()));
        return Ok(borrowed.unwrap_unchecked().clone());
    }

    auto record_miss(ScanCacheMissReason reason) -> void {
        auto statistics = state_->statistics.lock().unwrap_unchecked();
        ++statistics->misses;
        switch (reason) {
        case ScanCacheMissReason::Absent: ++statistics->absent; break;
        case ScanCacheMissReason::Refresh: ++statistics->refresh; break;
        case ScanCacheMissReason::Version: ++statistics->version; break;
        case ScanCacheMissReason::Recipe: ++statistics->recipe; break;
        case ScanCacheMissReason::Corrupt: ++statistics->corrupt; break;
        case ScanCacheMissReason::Environment: ++statistics->environment; break;
        case ScanCacheMissReason::Context: ++statistics->context; break;
        case ScanCacheMissReason::Source: ++statistics->source; break;
        case ScanCacheMissReason::FileDependency: ++statistics->file_dependency; break;
        case ScanCacheMissReason::IncludeLookup: ++statistics->include_lookup; break;
        case ScanCacheMissReason::ExternalMacro: ++statistics->external_macro; break;
        case ScanCacheMissReason::Receipt: ++statistics->receipt; break;
        case ScanCacheMissReason::None: __builtin_unreachable();
        }
    }

    auto miss(ScanCacheMissReason reason) -> CacheResult<ScanCacheLookup> {
        record_miss(reason);
        return Ok(ScanCacheLookup { .reason = reason });
    }

    auto file_fingerprint(ref<rstd::path::Path> path) -> CacheResult<FileFingerprint> {
        auto text = path_string(path);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        auto key = rstd::move(text).unwrap();
        struct Entry {
            SharedFingerprintCell cell;
            bool                  existing {};
        };
        auto entry = [&] {
            auto fields = state_->fingerprints.lock().unwrap_unchecked();
            auto found  = fields->entries.get(key.as_str());
            if (found.is_some()) return Entry { .cell = (**found).clone(), .existing = true };
            auto created = SharedFingerprintCell::make();
            fields->entries.insert(key.clone(), created.clone());
            return Entry { .cell = rstd::move(created) };
        }();
        auto waiting     = entry.existing && entry.cell->get().is_none();
        auto started     = rstd::time::Instant::now();
        auto initialized = false;
        auto stored      = entry.cell->get_or_init([&]() -> FingerprintResult {
            initialized   = true;
            auto metadata = rstd::fs::metadata(path);
            if (metadata.is_err()) {
                return Err(rstd::sync::Arc<CacheError>::make(
                    CacheError::SharedIo(String::make("inspect scan cache input"_str),
                                         PathBuf::from(path),
                                         rstd::sync::Arc<rstd::io::error::Error>::make(
                                             rstd::move(metadata).unwrap_err()))));
            }
            if (! metadata->is_file()) {
                return Err(rstd::sync::Arc<CacheError>::make(
                    CacheError::Record(rstd::format("scan cache input '{}' is not a file", path))));
            }
            auto contents = rstd::fs::read(path);
            if (contents.is_err()) {
                return Err(rstd::sync::Arc<CacheError>::make(
                    CacheError::SharedIo(String::make("hash scan cache input"_str),
                                         PathBuf::from(path),
                                         rstd::sync::Arc<rstd::io::error::Error>::make(
                                             rstd::move(contents).unwrap_err()))));
            }
            auto hash = cache::FNV_OFFSET;
            cache::add_text(hash, "lito-file-content-v1"_str);
            cache::add_bytes(hash, contents->as_slice());
            return Ok(FileFingerprint {
                .path        = PathBuf::from(path),
                .size        = metadata->size(),
                .fingerprint = cache::hex(hash),
            });
        });
        {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->fingerprint_requests;
            if (entry.existing) ++statistics->fingerprint_hits;
            if (initialized) ++statistics->fingerprint_builds;
            if (waiting && ! initialized) {
                ++statistics->fingerprint_waits;
                statistics->fingerprint_wait =
                    statistics->fingerprint_wait.saturating_add(started.elapsed());
            }
        }
        if (stored->is_err()) {
            auto fields  = state_->fingerprints.lock().unwrap_unchecked();
            auto current = fields->entries.get(key.as_str());
            if (current.is_some() && SharedFingerprintCell::ptr_eq(**current, entry.cell)) {
                (void)fields->entries.remove(key.as_str());
            }
        }
        return clone_fingerprint_result(*stored);
    }

    auto receipt(const ScanCacheInput&                                       input,
                 const FileFingerprint&                                      source,
                 const rstd::collections::BTreeMap<String, FileFingerprint>& files,
                 const Vec<frontend::IncludeLookupDependency>&               lookups,
                 const frontend::FrontendResult& result) const -> CacheResult<String> {
        auto source_path = path_string(input.source.as_path());
        auto relative    = path_string(input.relative_source.as_path());
        auto working     = path_string(input.working_directory.as_path());
        if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (working.is_err()) return Err(rstd::move(working).unwrap_err());
        auto hash = cache::FNV_OFFSET;
        cache::add_text(hash, "lito-scan-receipt-v1"_str);
        cache::add_text(hash, state_->environment.as_str());
        cache::add_text(hash, input.target.as_str());
        cache::add_text(hash, input.context_identity.as_str());
        cache::add_text(hash, input.external_macro_schema.as_str());
        cache::add_text(hash, working->as_str());
        cache::add_text(hash, source_path->as_str());
        cache::add_text(hash, relative->as_str());
        cache::add_text(hash, input.source_origin_identity.as_str());
        cache::add_text(hash, source.fingerprint.as_str());
        auto iter = files.iter();
        for (auto item = iter.next(); item.is_some(); item = iter.next()) {
            cache::add_text(hash, (*(*item).template get<0>()).as_str());
            cache::add_text(hash, (*(*item).template get<1>()).fingerprint.as_str());
        }
        for (const auto& lookup : lookups) {
            cache::add_text(hash, include_kind_name(lookup.kind));
            cache::add_text(hash, lookup.name.as_str());
            auto including = path_string(lookup.including_path.as_path());
            if (including.is_err()) return Err(rstd::move(including).unwrap_err());
            cache::add_text(hash, including->as_str());
            cache::add_text(hash,
                            lookup.previous_search_index.is_some()
                                ? rstd::format("{}", *lookup.previous_search_index).as_str()
                                : "none"_str);
            for (const auto& candidate : lookup.missing_candidates) {
                auto path = path_string(candidate.as_path());
                if (path.is_err()) return Err(rstd::move(path).unwrap_err());
                cache::add_text(hash, path->as_str());
            }
            if (lookup.resolved.is_some()) {
                auto requested = path_string(lookup.resolved->requested_path.as_path());
                auto canonical = path_string(lookup.resolved->canonical_path.as_path());
                if (requested.is_err()) return Err(rstd::move(requested).unwrap_err());
                if (canonical.is_err()) return Err(rstd::move(canonical).unwrap_err());
                cache::add_text(hash, requested->as_str());
                cache::add_text(hash, canonical->as_str());
                cache::add_text(hash, rstd::format("{}", lookup.resolved->search_index).as_str());
            } else {
                cache::add_text(hash, "unresolved"_str);
            }
        }
        auto encoded = snapshot_json(frontend::snapshot(result));
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        auto text = rstd::json::to_string(*encoded);
        cache::add_text(hash, text.as_str());
        return Ok(cache::hex(hash));
    }

public:
    static auto create(const CacheEnvironment& environment) -> ScanCacheSession {
        return ScanCacheSession { rstd::sync::Arc<State>::make(environment.scan_key_.clone(),
                                                               environment.force_refresh_) };
    }

    auto clone() const -> ScanCacheSession { return ScanCacheSession { state_.clone() }; }

    auto statistics() const -> ScanCacheStatistics {
        return *state_->statistics.lock().unwrap_unchecked();
    }

    auto begin(ScanCacheInput input) const -> ScanCacheTransaction;

private:
    friend class ScanCacheTransaction;

    auto lookup(const ScanCacheInput& input) -> CacheResult<ScanCacheLookup> {
        if (state_->force_refresh) {
            return miss(ScanCacheMissReason::Refresh);
        }
        auto exists = rstd::fs::exists(input.record.as_path());
        if (exists.is_err()) {
            return cache_io_failure<ScanCacheLookup>(
                "inspect scan record"_str, input.record.as_path(), rstd::move(exists).unwrap_err());
        }
        if (! *exists) return miss(ScanCacheMissReason::Absent);
        auto contents = rstd::fs::read_to_string(input.record.as_path());
        if (contents.is_err()) {
            return cache_io_failure<ScanCacheLookup>(
                "read scan record"_str, input.record.as_path(), rstd::move(contents).unwrap_err());
        }
        auto parsed = rstd::json::from_str(contents->as_str());
        if (parsed.is_err() || ! parsed->is_object()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto document    = ref<Json>::from_raw_parts(rstd::addressof(*parsed));
        auto source_path = path_string(input.source.as_path());
        auto relative    = path_string(input.relative_source.as_path());
        auto working     = path_string(input.working_directory.as_path());
        if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (working.is_err()) return Err(rstd::move(working).unwrap_err());
        auto version                      = json_number(document, "version"_str);
        auto state                        = json_text(document, "state"_str);
        auto recipe                       = json_text(document, "recipe"_str);
        auto environment                  = json_text(document, "environment"_str);
        auto target                       = json_text(document, "target"_str);
        auto context                      = json_text(document, "context"_str);
        auto source_origin                = json_text(document, "source-origin"_str);
        auto stored_working               = json_text(document, "working-directory"_str);
        auto stored_source                = json_member(document, "source"_str);
        auto files_value                  = json_array(document, "files"_str);
        auto lookups_value                = json_array(document, "include-lookups"_str);
        auto result_value                 = json_member(document, "result"_str);
        auto stored_receipt               = json_text(document, "receipt"_str);
        auto stored_external_macro_schema = json_text(document, "external-macro-schema"_str);
        if (version.is_none() || state.is_none() || *state != "complete"_str || recipe.is_none() ||
            environment.is_none() || target.is_none() || context.is_none() ||
            source_origin.is_none() || stored_working.is_none() || stored_source.is_none() ||
            files_value.is_none() || lookups_value.is_none() || result_value.is_none() ||
            stored_receipt.is_none() || stored_external_macro_schema.is_none()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        if (*version != CACHE_VERSION) return miss(ScanCacheMissReason::Version);
        if (*recipe != SCAN_RECIPE) return miss(ScanCacheMissReason::Recipe);
        if (*environment != state_->environment.as_str()) {
            return miss(ScanCacheMissReason::Environment);
        }
        if (*target != input.target.as_str() || *context != input.context_identity.as_str() ||
            *source_origin != input.source_origin_identity.as_str() ||
            *stored_working != working->as_str()) {
            return miss(ScanCacheMissReason::Context);
        }
        if (*stored_external_macro_schema != input.external_macro_schema.as_str()) {
            return miss(ScanCacheMissReason::ExternalMacro);
        }
        auto stored_source_path        = json_text(*stored_source, "path"_str);
        auto stored_relative           = json_text(*stored_source, "relative"_str);
        auto stored_source_fingerprint = json_text(*stored_source, "fingerprint"_str);
        auto stored_source_size        = json_number(*stored_source, "size"_str);
        if (stored_source_path.is_none() || stored_relative.is_none() ||
            stored_source_fingerprint.is_none() || stored_source_size.is_none() ||
            *stored_source_path != source_path->as_str() ||
            *stored_relative != relative->as_str()) {
            return miss(ScanCacheMissReason::Source);
        }
        auto source_file = file_fingerprint(input.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        if (source_file->size != *stored_source_size ||
            source_file->fingerprint.as_str() != *stored_source_fingerprint) {
            return miss(ScanCacheMissReason::Source);
        }
        auto files = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        for (const auto& item : **files_value) {
            auto item_ref    = ref<Json>::from_raw_parts(rstd::addressof(item));
            auto path        = json_text(item_ref, "path"_str);
            auto size        = json_number(item_ref, "size"_str);
            auto fingerprint = json_text(item_ref, "fingerprint"_str);
            if (path.is_none() || size.is_none() || fingerprint.is_none() ||
                files.contains_key(*path)) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            auto exists = rstd::fs::exists(PathBuf::from(*path).as_path());
            if (exists.is_err()) {
                return cache_io_failure<ScanCacheLookup>("inspect scan input"_str,
                                                         PathBuf::from(*path).as_path(),
                                                         rstd::move(exists).unwrap_err());
            }
            if (! *exists) return miss(ScanCacheMissReason::FileDependency);
            auto metadata = rstd::fs::metadata(PathBuf::from(*path).as_path());
            if (metadata.is_err()) {
                return cache_io_failure<ScanCacheLookup>("inspect scan input"_str,
                                                         PathBuf::from(*path).as_path(),
                                                         rstd::move(metadata).unwrap_err());
            }
            if (! metadata->is_file()) return miss(ScanCacheMissReason::FileDependency);
            auto current = file_fingerprint(PathBuf::from(*path).as_path());
            if (current.is_err()) return Err(rstd::move(current).unwrap_err());
            if (current->size != *size || current->fingerprint.as_str() != *fingerprint) {
                return miss(ScanCacheMissReason::FileDependency);
            }
            files.insert(String::make(*path), rstd::move(current).unwrap());
        }
        auto lookups =
            Vec<frontend::IncludeLookupDependency>::with_capacity((**lookups_value).len());
        for (const auto& item : **lookups_value) {
            auto lookup = parse_include_lookup(ref<Json>::from_raw_parts(rstd::addressof(item)));
            if (lookup.is_none()) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            auto valid = frontend::validate(*lookup);
            if (valid.is_err()) {
                return cache_failure<ScanCacheLookup>(rstd::move(valid).unwrap_err());
            }
            if (! *valid) {
                return miss(ScanCacheMissReason::IncludeLookup);
            }
            lookups.push(rstd::move(lookup).unwrap());
        }
        auto stored_snapshot = parse_snapshot(*result_value);
        if (stored_snapshot.is_none()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto restored = frontend::restore(rstd::move(stored_snapshot).unwrap());
        if (restored.is_none() || restored->source.as_path() != input.source.as_path() ||
            restored->preprocessor_environment.as_str() !=
                input.preprocessor_environment.as_str()) {
            return miss(ScanCacheMissReason::Environment);
        }
        for (const auto& macro : restored->external_macros) {
            if (! input.external_macros->validates(macro)) {
                return miss(ScanCacheMissReason::ExternalMacro);
            }
        }
        auto header_paths = rstd::collections::BTreeMap<String, empty>::make();
        for (const auto& header : restored->header_inputs) {
            auto path = path_string(header.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (header_paths.contains_key(path->as_str()) || ! files.contains_key(path->as_str())) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            header_paths.insert(rstd::move(path).unwrap(), empty {});
        }
        if (header_paths.len() != files.len()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto expected_receipt = receipt(input, *source_file, files, lookups, *restored);
        if (expected_receipt.is_err()) return Err(rstd::move(expected_receipt).unwrap_err());
        if (expected_receipt->as_str() != *stored_receipt) {
            return miss(ScanCacheMissReason::Receipt);
        }
        {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->hits;
        }
        return Ok(ScanCacheLookup {
            .hit    = Some(frontend::FrontendAnalysis {
                .result           = rstd::move(restored).unwrap(),
                .context_identity = input.context_identity.clone(),
                .receipt          = rstd::move(expected_receipt).unwrap(),
                .origin           = frontend::FrontendAnalysisOrigin::PersistentCache,
            }),
            .reason = ScanCacheMissReason::None,
        });
    }

    auto publish(const ScanCacheInput& input, frontend::UncachedFrontendAnalysis value)
        -> CacheResult<frontend::FrontendAnalysis> {
        auto source_file = file_fingerprint(input.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        auto files = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        for (const auto& header : value.result.header_inputs) {
            auto path = path_string(header.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (files.contains_key(path->as_str())) continue;
            auto file = file_fingerprint(header.as_path());
            if (file.is_err()) return Err(rstd::move(file).unwrap_err());
            files.insert(rstd::move(path).unwrap(), rstd::move(file).unwrap());
        }
        auto scan_receipt =
            receipt(input, *source_file, files, value.include_lookups, value.result);
        if (scan_receipt.is_err()) return Err(rstd::move(scan_receipt).unwrap_err());
        auto cacheable = value.result.preprocessor_environment.as_str() ==
                         input.preprocessor_environment.as_str();
        if (cacheable) {
            auto source_path = path_string(input.source.as_path());
            auto relative    = path_string(input.relative_source.as_path());
            auto working     = path_string(input.working_directory.as_path());
            if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
            if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
            if (working.is_err()) return Err(rstd::move(working).unwrap_err());
            auto files_json = JsonArray::make();
            auto iter       = files.iter();
            for (auto item = iter.next(); item.is_some(); item = iter.next()) {
                auto encoded = file_json(*(*item).template get<1>());
                if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
                files_json.push(rstd::move(encoded).unwrap());
            }
            auto lookups_json = JsonArray::make();
            for (const auto& lookup : value.include_lookups) {
                auto encoded = include_lookup_json(lookup);
                if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
                lookups_json.push(rstd::move(encoded).unwrap());
            }
            auto encoded_result = snapshot_json(frontend::snapshot(value.result));
            if (encoded_result.is_err()) return Err(rstd::move(encoded_result).unwrap_err());
            auto source_json = JsonMap::make();
            source_json.insert(String::make("fingerprint"_str),
                               cache_string(source_file->fingerprint.as_str()));
            source_json.insert(String::make("path"_str), cache_string(source_path->as_str()));
            source_json.insert(String::make("relative"_str), cache_string(relative->as_str()));
            source_json.insert(String::make("size"_str), cache_u64(source_file->size));
            auto root = JsonMap::make();
            root.insert(String::make("context"_str), cache_string(input.context_identity.as_str()));
            root.insert(String::make("environment"_str),
                        cache_string(state_->environment.as_str()));
            root.insert(String::make("external-macro-schema"_str),
                        cache_string(input.external_macro_schema.as_str()));
            root.insert(String::make("files"_str), Json::Array(rstd::move(files_json)));
            root.insert(String::make("include-lookups"_str), Json::Array(rstd::move(lookups_json)));
            root.insert(String::make("receipt"_str), cache_string(scan_receipt->as_str()));
            root.insert(String::make("recipe"_str), cache_string(SCAN_RECIPE));
            root.insert(String::make("result"_str), rstd::move(encoded_result).unwrap());
            root.insert(String::make("source"_str), Json::Object(rstd::move(source_json)));
            root.insert(String::make("source-origin"_str),
                        cache_string(input.source_origin_identity.as_str()));
            root.insert(String::make("state"_str), cache_string("complete"_str));
            root.insert(String::make("target"_str), cache_string(input.target.as_str()));
            root.insert(String::make("version"_str), cache_u64(CACHE_VERSION));
            root.insert(String::make("working-directory"_str), cache_string(working->as_str()));
            auto written = write_json(input.record.as_path(), Json::Object(rstd::move(root)));
            if (written.is_err()) return Err(rstd::move(written).unwrap_err());
        } else {
            auto statistics = state_->statistics.lock().unwrap_unchecked();
            ++statistics->uncacheable;
        }
        return Ok(frontend::FrontendAnalysis {
            .result           = rstd::move(value.result),
            .context_identity = input.context_identity.clone(),
            .receipt          = rstd::move(scan_receipt).unwrap(),
            .origin           = cacheable ? frontend::FrontendAnalysisOrigin::Native
                                          : frontend::FrontendAnalysisOrigin::Uncacheable,
        });
    }
};

class ScanCacheTransaction {
    ScanCacheSession session_;
    ScanCacheInput   input_;

    ScanCacheTransaction(ScanCacheSession session, ScanCacheInput input)
        : session_(rstd::move(session)), input_(rstd::move(input)) {}

    friend class ScanCacheSession;

public:
    ScanCacheTransaction(ScanCacheTransaction&&) noexcept                    = default;
    auto operator=(ScanCacheTransaction&&) noexcept -> ScanCacheTransaction& = default;

    auto lookup() -> CacheResult<ScanCacheLookup> { return session_.lookup(input_); }

    auto publish(frontend::UncachedFrontendAnalysis value)
        -> CacheResult<frontend::FrontendAnalysis> {
        return session_.publish(input_, rstd::move(value));
    }
};

auto ScanCacheSession::begin(ScanCacheInput input) const -> ScanCacheTransaction {
    return ScanCacheTransaction(clone(), rstd::move(input));
}

} // namespace lito
