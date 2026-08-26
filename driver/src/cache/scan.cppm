module;
#include <rstd/macro.hpp>

module lito.driver:cache.scan;

import rstd;
import rstd.json;
import lito.crypto;
import lito.core;
import lito.cpp;
import lito.frontend;
import lito.toolchain;
import :build.layout;
import :cache.hash;
import :cache.common;
import :cache.scan_wire;

using namespace rstd::prelude;
using namespace rstd::literals;

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
    EmbedLookup,
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
    usize                embed_lookup {};
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

auto parse_include_kind(ref<str> value) -> Option<frontend::IncludeLookupKind> {
    if (value == "quoted"_str) return Some(frontend::IncludeLookupKind::Quoted);
    if (value == "angled"_str) return Some(frontend::IncludeLookupKind::Angled);
    if (value == "next-quoted"_str) return Some(frontend::IncludeLookupKind::NextQuoted);
    if (value == "next-angled"_str) return Some(frontend::IncludeLookupKind::NextAngled);
    return None();
}

auto parse_embed_kind(ref<str> value) -> Option<frontend::EmbedLookupKind> {
    if (value == "quoted"_str) return Some(frontend::EmbedLookupKind::Quoted);
    if (value == "angled"_str) return Some(frontend::EmbedLookupKind::Angled);
    return None();
}

auto parse_external_macro_state(ref<str> value) -> Option<frontend::ExternalMacroState> {
    if (value == "defined"_str) return Some(frontend::ExternalMacroState::Defined);
    if (value == "undefined"_str) return Some(frontend::ExternalMacroState::Undefined);
    return None();
}

auto restore_external_macro(scan_cache_wire::ExternalMacro value)
    -> Option<frontend::ExternalMacroMaterialization> {
    if (value.name.is_empty() || value.dependency_key.is_empty() ||
        value.value_identity.is_empty()) {
        return None();
    }
    auto state = parse_external_macro_state(value.state.as_str());
    if (state.is_none() ||
        (*state == frontend::ExternalMacroState::Defined) != value.compiler_definition.is_some()) {
        return None();
    }
    return Some(frontend::ExternalMacroMaterialization {
        .name                = rstd::move(value.name),
        .dependency_key      = rstd::move(value.dependency_key),
        .value_identity      = rstd::move(value.value_identity),
        .state               = *state,
        .compiler_definition = rstd::move(value.compiler_definition),
    });
}

auto restore_resolved_include(scan_cache_wire::ResolvedLookup value)
    -> frontend::ResolvedIncludeCandidate {
    return frontend::ResolvedIncludeCandidate {
        .requested_path = PathBuf::from(value.requested.as_str()),
        .canonical_path = PathBuf::from(value.canonical.as_str()),
        .search_index   = usize(static_cast<size_t>(value.search_index.to_primitive())),
    };
}

auto restore_include_lookup(scan_cache_wire::IncludeLookup value)
    -> Option<frontend::IncludeLookupDependency> {
    auto kind = parse_include_kind(value.kind.as_str());
    if (kind.is_none() || value.name.is_empty()) return None();
    auto missing = Vec<PathBuf>::with_capacity(value.missing.len());
    for (auto& path : value.missing) missing.push(PathBuf::from(path.as_str()));
    auto previous = Option<usize> {};
    if (value.previous_search_index.is_some()) {
        previous = Some(usize(static_cast<size_t>(value.previous_search_index->to_primitive())));
    }
    auto resolved = Option<frontend::ResolvedIncludeCandidate> {};
    if (value.resolved.is_some()) {
        resolved = Some(restore_resolved_include(rstd::move(value.resolved).unwrap_unchecked()));
    }
    return Some(frontend::IncludeLookupDependency {
        .kind                  = *kind,
        .name                  = rstd::move(value.name),
        .including_path        = PathBuf::from(value.including.as_str()),
        .previous_search_index = previous,
        .missing_candidates    = rstd::move(missing),
        .resolved              = rstd::move(resolved),
    });
}

auto restore_embed_lookup(scan_cache_wire::EmbedLookup value)
    -> Option<frontend::EmbedLookupDependency> {
    auto kind = parse_embed_kind(value.kind.as_str());
    if (kind.is_none() || value.name.is_empty()) return None();
    auto missing = Vec<PathBuf>::with_capacity(value.missing.len());
    for (auto& path : value.missing) missing.push(PathBuf::from(path.as_str()));
    auto resolved = Option<frontend::ResolvedEmbedCandidate> {};
    if (value.resolved.is_some()) {
        auto stored = rstd::move(value.resolved).unwrap_unchecked();
        resolved    = Some(frontend::ResolvedEmbedCandidate {
            .requested_path = PathBuf::from(stored.requested.as_str()),
            .canonical_path = PathBuf::from(stored.canonical.as_str()),
            .search_index   = usize(static_cast<size_t>(stored.search_index.to_primitive())),
        });
    }
    return Some(frontend::EmbedLookupDependency {
        .kind               = *kind,
        .name               = rstd::move(value.name),
        .including_path     = PathBuf::from(value.including.as_str()),
        .missing_candidates = rstd::move(missing),
        .resolved           = rstd::move(resolved),
    });
}

auto restore_snapshot(scan_cache_wire::Snapshot value) -> Option<frontend::FrontendSnapshot> {
    auto provided = Option<frontend::ProvidedModule> {};
    if (value.provided.is_some()) {
        auto stored = rstd::move(value.provided).unwrap_unchecked();
        provided    = Some(frontend::ProvidedModule {
            .logical_name = rstd::move(stored.logical_name),
            .is_interface = stored.interface,
        });
    }
    auto imports = Vec<frontend::ModuleImport>::with_capacity(value.imports.len());
    for (auto& stored : value.imports) {
        imports.push(frontend::ModuleImport {
            .logical_name = rstd::move(stored.logical_name),
            .location =
                frontend::DependencyLocation {
                    .path = PathBuf::from(stored.location.path.as_str()),
                    .line = usize(static_cast<size_t>(stored.location.line.to_primitive())),
                },
            .exported = stored.exported,
        });
    }
    auto headers = Vec<PathBuf>::with_capacity(value.header_inputs.len());
    for (auto& path : value.header_inputs) headers.push(PathBuf::from(path.as_str()));
    auto embedded = Vec<frontend::EmbeddedInput>::with_capacity(value.embedded_inputs.len());
    for (auto& stored : value.embedded_inputs) {
        if (stored.digest.is_empty()) return None();
        embedded.push(frontend::EmbeddedInput {
            .path   = PathBuf::from(stored.path.as_str()),
            .size   = stored.size,
            .digest = rstd::move(stored.digest),
            .offset = usize(static_cast<size_t>(stored.offset.to_primitive())),
            .length = usize(static_cast<size_t>(stored.length.to_primitive())),
        });
    }
    auto macros =
        Vec<frontend::ExternalMacroMaterialization>::with_capacity(value.external_macros.len());
    for (auto& stored : value.external_macros) {
        auto macro = restore_external_macro(rstd::move(stored));
        if (macro.is_none()) return None();
        macros.push(rstd::move(macro).unwrap_unchecked());
    }
    return Some(frontend::FrontendSnapshot {
        .source                   = PathBuf::from(value.source.as_str()),
        .provided                 = rstd::move(provided),
        .implementation_module    = rstd::move(value.implementation_module),
        .imports                  = rstd::move(imports),
        .header_inputs            = rstd::move(headers),
        .embedded_inputs          = rstd::move(embedded),
        .external_macros          = rstd::move(macros),
        .preprocessor_environment = rstd::move(value.preprocessor_environment),
        .input_bytes              = usize(static_cast<size_t>(value.input_bytes.to_primitive())),
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
        case ScanCacheMissReason::EmbedLookup: ++statistics->embed_lookup; break;
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
            auto opened = rstd::fs::File::open(path);
            if (opened.is_err()) {
                return Err(rstd::sync::Arc<CacheError>::make(
                    CacheError::SharedIo(String::make("hash scan cache input"_str),
                                         PathBuf::from(path),
                                         rstd::sync::Arc<rstd::io::error::Error>::make(
                                             rstd::move(opened).unwrap_err()))));
            }
            auto file   = rstd::move(opened).unwrap_unchecked();
            auto digest = lito::crypto::Sha256::make();
            auto buffer = array<u8, 65536> {};
            while (true) {
                auto read = file.read(buffer.as_mut_slice());
                if (read.is_err()) {
                    return Err(rstd::sync::Arc<CacheError>::make(
                        CacheError::SharedIo(String::make("hash scan cache input"_str),
                                             PathBuf::from(path),
                                             rstd::sync::Arc<rstd::io::error::Error>::make(
                                                 rstd::move(read).unwrap_err()))));
                }
                if (*read == usize {}) break;
                digest.update(slice<u8>::from_raw_parts(buffer.as_ptr(), *read));
            }
            return Ok(FileFingerprint {
                .path        = PathBuf::from(path),
                .size        = metadata->size(),
                .fingerprint = lito::crypto::sha256_hex(rstd::move(digest).finalize()),
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
                 const Vec<frontend::EmbedLookupDependency>&                 embed_lookups,
                 const frontend::FrontendResult& result) const -> CacheResult<String> {
        auto hash     = cache::FNV_OFFSET;
        auto add_path = [&](ref<rstd::path::Path> path) -> CacheResult<empty> {
            auto text = path.to_str();
            if (text.is_none()) {
                return cache_failure<empty>(rstd::format("path '{}' is not valid UTF-8", path));
            }
            cache::add_text(hash, *text);
            return Ok(empty {});
        };
        cache::add_text(hash, "lito-scan-receipt-v1"_str);
        cache::add_text(hash, state_->environment.as_str());
        cache::add_text(hash, input.target.as_str());
        cache::add_text(hash, input.context_identity.as_str());
        cache::add_text(hash, input.external_macro_schema.as_str());
        rstd_try(add_path(input.working_directory.as_path()));
        rstd_try(add_path(input.source.as_path()));
        rstd_try(add_path(input.relative_source.as_path()));
        cache::add_text(hash, input.source_origin_identity.as_str());
        cache::add_text(hash, source.fingerprint.as_str());
        auto file_iter = files.iter();
        for (auto item : file_iter) {
            const auto& file = *item.template get<1>();
            rstd_try(add_path(file.path.as_path()));
            cache::add_text(hash, file.fingerprint.as_str());
        }
        for (const auto& lookup : lookups) {
            cache::add_text(hash, scan_cache_wire::include_kind_name(lookup.kind));
            cache::add_text(hash, lookup.name.as_str());
            rstd_try(add_path(lookup.including_path.as_path()));
            cache::add_text(hash,
                            lookup.previous_search_index.is_some()
                                ? rstd::format("{}", *lookup.previous_search_index).as_str()
                                : "none"_str);
            for (const auto& candidate : lookup.missing_candidates) {
                rstd_try(add_path(candidate.as_path()));
            }
            if (lookup.resolved.is_some()) {
                rstd_try(add_path(lookup.resolved->requested_path.as_path()));
                rstd_try(add_path(lookup.resolved->canonical_path.as_path()));
                cache::add_text(hash, rstd::format("{}", lookup.resolved->search_index).as_str());
            } else {
                cache::add_text(hash, "unresolved"_str);
            }
        }
        for (const auto& lookup : embed_lookups) {
            cache::add_text(hash, scan_cache_wire::embed_kind_name(lookup.kind));
            cache::add_text(hash, lookup.name.as_str());
            rstd_try(add_path(lookup.including_path.as_path()));
            for (const auto& candidate : lookup.missing_candidates) {
                rstd_try(add_path(candidate.as_path()));
            }
            if (lookup.resolved.is_some()) {
                rstd_try(add_path(lookup.resolved->requested_path.as_path()));
                rstd_try(add_path(lookup.resolved->canonical_path.as_path()));
                cache::add_text(hash, rstd::format("{}", lookup.resolved->search_index).as_str());
            } else {
                cache::add_text(hash, "unresolved"_str);
            }
        }
        auto encoded = rstd::json::encode_direct(scan_cache_wire::WriteSnapshot {
            rstd::addressof(result),
        });
        if (encoded.is_err()) {
            return cache_failure<String>(
                rstd::format("serialize scan cache result: {}", rstd::move(encoded).unwrap_err()));
        }
        cache::add_text(hash, encoded->as_str());
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
        auto decoded = rstd::json::decode_direct<scan_cache_wire::Receipt>(contents->as_str());
        if (decoded.is_err()) return miss(ScanCacheMissReason::Corrupt);
        auto document    = rstd::move(decoded).unwrap_unchecked();
        auto source_path = path_string(input.source.as_path());
        auto relative    = path_string(input.relative_source.as_path());
        auto working     = path_string(input.working_directory.as_path());
        if (source_path.is_err()) return Err(rstd::move(source_path).unwrap_err());
        if (relative.is_err()) return Err(rstd::move(relative).unwrap_err());
        if (working.is_err()) return Err(rstd::move(working).unwrap_err());
        if (document.state.as_str() != "complete"_str || document.recipe.is_empty() ||
            document.environment.is_empty() || document.target.is_empty() ||
            document.context.is_empty() || document.source_origin.is_empty() ||
            document.working_directory.is_empty() || document.receipt.is_empty() ||
            document.external_macro_schema.is_empty()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        if (document.version != CACHE_VERSION) return miss(ScanCacheMissReason::Version);
        if (document.recipe.as_str() != SCAN_RECIPE) return miss(ScanCacheMissReason::Recipe);
        if (document.environment.as_str() != state_->environment.as_str()) {
            return miss(ScanCacheMissReason::Environment);
        }
        if (document.target.as_str() != input.target.as_str() ||
            document.context.as_str() != input.context_identity.as_str() ||
            document.source_origin.as_str() != input.source_origin_identity.as_str() ||
            document.working_directory.as_str() != working->as_str()) {
            return miss(ScanCacheMissReason::Context);
        }
        if (document.external_macro_schema.as_str() != input.external_macro_schema.as_str()) {
            return miss(ScanCacheMissReason::ExternalMacro);
        }
        if (document.source.path.as_str() != source_path->as_str() ||
            document.source.relative.as_str() != relative->as_str() ||
            document.source.fingerprint.is_empty()) {
            return miss(ScanCacheMissReason::Source);
        }
        auto source_file = file_fingerprint(input.source.as_path());
        if (source_file.is_err()) return Err(rstd::move(source_file).unwrap_err());
        if (source_file->size != document.source.size ||
            source_file->fingerprint.as_str() != document.source.fingerprint.as_str()) {
            return miss(ScanCacheMissReason::Source);
        }
        auto files = rstd::collections::BTreeMap<String, FileFingerprint>::make();
        for (auto& item : document.files) {
            if (item.path.is_empty() || item.fingerprint.is_empty() ||
                files.contains_key(item.path.as_str())) {
                return miss(ScanCacheMissReason::Corrupt);
            }
            auto path   = PathBuf::from(item.path.as_str());
            auto exists = rstd::fs::exists(path.as_path());
            if (exists.is_err()) {
                return cache_io_failure<ScanCacheLookup>(
                    "inspect scan input"_str, path.as_path(), rstd::move(exists).unwrap_err());
            }
            if (! *exists) return miss(ScanCacheMissReason::FileDependency);
            auto metadata = rstd::fs::metadata(path.as_path());
            if (metadata.is_err()) {
                return cache_io_failure<ScanCacheLookup>(
                    "inspect scan input"_str, path.as_path(), rstd::move(metadata).unwrap_err());
            }
            if (! metadata->is_file()) return miss(ScanCacheMissReason::FileDependency);
            auto current = file_fingerprint(path.as_path());
            if (current.is_err()) return Err(rstd::move(current).unwrap_err());
            if (current->size != item.size ||
                current->fingerprint.as_str() != item.fingerprint.as_str()) {
                return miss(ScanCacheMissReason::FileDependency);
            }
            files.insert(rstd::move(item.path), rstd::move(current).unwrap());
        }
        auto lookups =
            Vec<frontend::IncludeLookupDependency>::with_capacity(document.include_lookups.len());
        for (auto& item : document.include_lookups) {
            auto lookup = restore_include_lookup(rstd::move(item));
            if (lookup.is_none()) return miss(ScanCacheMissReason::Corrupt);
            auto valid = frontend::validate(*lookup);
            if (valid.is_err()) {
                return cache_failure<ScanCacheLookup>(rstd::move(valid).unwrap_err());
            }
            if (! *valid) {
                return miss(ScanCacheMissReason::IncludeLookup);
            }
            lookups.push(rstd::move(lookup).unwrap());
        }
        auto embed_lookups =
            Vec<frontend::EmbedLookupDependency>::with_capacity(document.embed_lookups.len());
        for (auto& item : document.embed_lookups) {
            auto lookup = restore_embed_lookup(rstd::move(item));
            if (lookup.is_none()) return miss(ScanCacheMissReason::Corrupt);
            auto valid = frontend::validate(*lookup);
            if (valid.is_err()) {
                return cache_failure<ScanCacheLookup>(rstd::move(valid).unwrap_err());
            }
            if (! *valid) return miss(ScanCacheMissReason::EmbedLookup);
            embed_lookups.push(rstd::move(lookup).unwrap());
        }
        auto stored_snapshot = restore_snapshot(rstd::move(document.result));
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
        auto dependency_paths = rstd::move(header_paths);
        for (const auto& embedded : restored->embedded_inputs) {
            auto path = path_string(embedded.path.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (! files.contains_key(path->as_str())) return miss(ScanCacheMissReason::Corrupt);
            dependency_paths.insert(rstd::move(path).unwrap(), empty {});
        }
        if (dependency_paths.len() != files.len()) {
            return miss(ScanCacheMissReason::Corrupt);
        }
        auto expected_receipt =
            receipt(input, *source_file, files, lookups, embed_lookups, *restored);
        if (expected_receipt.is_err()) return Err(rstd::move(expected_receipt).unwrap_err());
        if (expected_receipt->as_str() != document.receipt.as_str()) {
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
        for (const auto& embedded : value.result.embedded_inputs) {
            auto path = path_string(embedded.path.as_path());
            if (path.is_err()) return Err(rstd::move(path).unwrap_err());
            if (files.contains_key(path->as_str())) continue;
            auto file = file_fingerprint(embedded.path.as_path());
            if (file.is_err()) return Err(rstd::move(file).unwrap_err());
            files.insert(rstd::move(path).unwrap(), rstd::move(file).unwrap());
        }
        auto scan_receipt = receipt(
            input, *source_file, files, value.include_lookups, value.embed_lookups, value.result);
        if (scan_receipt.is_err()) return Err(rstd::move(scan_receipt).unwrap_err());
        auto cacheable = value.result.preprocessor_environment.as_str() ==
                         input.preprocessor_environment.as_str();
        if (cacheable) {
            auto working = input.working_directory.as_path().to_str();
            if (working.is_none()) {
                return cache_failure<frontend::FrontendAnalysis>(rstd::format(
                    "path '{}' is not valid UTF-8", input.working_directory.as_path()));
            }
            auto document = scan_cache_wire::WriteReceipt<decltype(files)> {
                .version               = CACHE_VERSION,
                .state                 = "complete"_str,
                .recipe                = SCAN_RECIPE,
                .environment           = state_->environment.as_str(),
                .target                = input.target.as_str(),
                .context               = input.context_identity.as_str(),
                .source_origin         = input.source_origin_identity.as_str(),
                .working_directory     = *working,
                .external_macro_schema = input.external_macro_schema.as_str(),
                .source =
                    scan_cache_wire::WriteSource {
                        .path =
                            scan_cache_wire::WritePath {
                                input.source.as_path(),
                            },
                        .relative =
                            scan_cache_wire::WritePath {
                                input.relative_source.as_path(),
                            },
                        .size        = source_file->size,
                        .fingerprint = source_file->fingerprint.as_str(),
                    },
                .files           = rstd::addressof(files),
                .include_lookups = rstd::addressof(value.include_lookups),
                .embed_lookups   = rstd::addressof(value.embed_lookups),
                .result          = rstd::addressof(value.result),
                .receipt         = scan_receipt->as_str(),
            };
            auto written = write_typed_json(input.record.as_path(), document);
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
