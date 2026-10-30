module lito.driver:build.compile.database;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain.common;
import lito.system;
import :build.error;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

class CompilationDatabase {
    Vec<Option<rstd::json::Value>> entries_;
    Option<usize>                  published_units_;

public:
    auto contains(usize unit) const -> bool {
        if (published_units_.is_some()) return unit < *published_units_;
        return unit < entries_.len() && entries_[unit].is_some();
    }

    auto record(usize unit, ref<rstd::path::Path> source, const CompileInvocation& invocation)
        -> BuildResult<empty> {
        // Completed host units keep the invocation used before the final plan is materialized.
        if (contains(unit)) return Ok(empty {});
        if (published_units_.is_some()) {
            return Err(
                BuildError::Message("cannot append to a published compilation database"_Str));
        }
        auto file      = source.to_str();
        auto directory = invocation.working_directory.as_path().to_str();
        auto output    = invocation.staged_object.as_path().to_str();
        if (file.is_none() || directory.is_none() || output.is_none()) {
            return Err(BuildError::Message("compilation database path is not valid UTF-8"_Str));
        }
        using Json     = rstd::json::Value;
        auto arguments = rstd::json::Array::with_capacity(invocation.arguments.len());
        for (const auto& argument : invocation.arguments)
            arguments.push(Json::String(argument.clone()));
        auto entry = rstd::json::Map::make();
        entry.insert("directory"_Str, Json::String(String::make(*directory)));
        entry.insert("file"_Str, Json::String(String::make(*file)));
        entry.insert("arguments"_Str, Json::Array(rstd::move(arguments)));
        entry.insert("output"_Str, Json::String(String::make(*output)));
        while (entries_.len() <= unit) entries_.emplace_back();
        entries_[unit] = Some(Json::Object(rstd::move(entry)));
        return Ok(empty {});
    }

    auto sync(ref<rstd::path::Path> destination, usize units) -> BuildResult<empty> {
        if (entries_.len() != units) {
            return Err(BuildError::Message("compilation database has an incomplete unit list"_Str));
        }
        auto entries = rstd::json::Array::with_capacity(units);
        for (auto& entry : entries_) {
            if (entry.is_none()) {
                return Err(BuildError::Message("compilation database has an unrecorded unit"_Str));
            }
            entries.push(entry.take().unwrap());
        }
        auto text =
            rstd::json::to_string(rstd::json::Value::Array(rstd::move(entries)),
                                  rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
        text.push_ascii('\n');
        auto written = rstd::fs::write_atomic_if_changed(destination, text.as_str().as_bytes());
        if (written.is_err()) {
            return Err(BuildError::System(
                lito::system::SystemError::Io("write compilation database"_Str,
                                              PathBuf::from(destination),
                                              rstd::move(written).unwrap_err())));
        }
        entries_         = Vec<Option<rstd::json::Value>>::make();
        published_units_ = Some(units);
        return Ok(empty {});
    }
};

} // namespace lito
