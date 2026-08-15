export module lito.toolchain.clang:preprocessor_provider;

import rstd;
import lito.core;
import lito.frontend;
import :preprocessor_model;
import :preprocessor_query;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::toolchain
{

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
    ClangBuiltinProvider(const PreprocessorEnvironment& environment,
                         ref<rstd::path::Path>          working_directory)
        : environment_(environment), working_directory_(PathBuf::from(working_directory)) {}

    auto predefined_macros() -> preprocessor::Result<Vec<preprocessor::PredefinedMacroOperation>> {
        auto result = Vec<preprocessor::PredefinedMacroOperation>::with_capacity(
            environment_.builtin_environment->definitions.len() +
            environment_.native_definitions.len() + environment_.command_line_macros.len());
        for (const auto& definition : environment_.builtin_environment->definitions) {
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
        auto cached = environment_.builtin_environment->capabilities.get(key.as_str());
        if (cached.is_some()) return Ok(**cached);
        return Ok(i64 {});
    }

    auto text(preprocessor::BuiltinTextKind kind) -> preprocessor::Result<String> {
        const auto& value =
            kind == preprocessor::BuiltinTextKind::Date ? environment_.date : environment_.time;
        return Ok(value.clone());
    }

private:
    const PreprocessorEnvironment& environment_;
    PathBuf                        working_directory_;
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

} // namespace lito::toolchain
