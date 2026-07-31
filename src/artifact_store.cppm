export module tenon.artifact_store;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{


inline constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
inline constexpr uint64_t FNV_PRIME  = 1099511628211ull;

auto add_bytes(uint64_t& hash, slice<u8> bytes) -> void {
    for (auto index = usize {}; index < bytes.len(); ++index) {
        auto value = bytes[index];
        hash ^= value.to_primitive();
        hash *= FNV_PRIME;
    }
}

auto add_text(uint64_t& hash, ref<str> value) -> void {
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= FNV_PRIME;
    }
    hash ^= 0;
    hash *= FNV_PRIME;
}

auto add_path(uint64_t& hash, ref<rstd::path::Path> path) -> bool {
    auto text = path.to_str();
    if (text.is_none()) return false;
    add_text(hash, *text);
    return true;
}

auto hex(uint64_t value) -> String {
    static constexpr char digits[] = "0123456789abcdef";
    char result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[value & 0xfu];
        value >>= 4u;
    }
    return String::make(ref<str>::from_raw_parts_unchecked(
        reinterpret_cast<const byte*>(result), usize(16)));
}

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Artifact, rstd::move(message)));
}

} // namespace tenon

export namespace tenon
{

class ArtifactStore {
public:
    auto key_for(const PreparedUnit& unit,
                 const ScanResult& scan,
                 const Vec<String>& direct_dependency_keys) -> Result<String> {
        auto hash = FNV_OFFSET;
        add_text(hash, "tenon-artifact-v1"_str);
        add_text(hash, unit.unit.context->id.as_str());
        if (! add_path(hash, unit.unit.source.as_path())) {
            return failure<String>(rstd::format(
                "source path '{}' is not valid UTF-8", unit.unit.source.as_path()));
        }

        auto source = file_identity(unit.unit.source.as_path());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err());
        add_text(hash, source->as_str());

        if (scan.provided.is_some()) {
            add_text(hash, (*scan.provided).logical_name.as_str());
            add_text(hash,
                     (*scan.provided).is_interface ? "interface"_str : "internal"_str);
        }
        for (const auto& required : scan.required_modules) add_text(hash, required.as_str());
        for (const auto& header : scan.header_inputs) {
            auto identity = file_identity(header.as_path());
            if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
            if (! add_path(hash, header.as_path())) {
                return failure<String>(rstd::format(
                    "header path '{}' is not valid UTF-8", header.as_path()));
            }
            add_text(hash, identity->as_str());
        }
        for (const auto& dependency : direct_dependency_keys) {
            add_text(hash, dependency.as_str());
        }
        return Ok(hex(hash));
    }

    auto current(const PreparedUnit& unit, const ScanResult& scan, ref<str> key) const
        -> bool {
        auto object_exists = rstd::fs::exists(unit.unit.object.as_path());
        if (object_exists.is_err() || ! *object_exists) return false;
        if (scan.provided.is_some()) {
            if (unit.unit.bmi.is_none()) return false;
            auto bmi_exists = rstd::fs::exists((*unit.unit.bmi).as_path());
            if (bmi_exists.is_err() || ! *bmi_exists) return false;
        }
        auto stored = rstd::fs::read_to_string(unit.unit.fingerprint.as_path());
        if (stored.is_err()) return false;
        return stored->as_str().trim_ascii() == key;
    }

    auto commit(const PreparedUnit& unit, ref<str> key) -> Result<empty> {
        auto parent = unit.unit.fingerprint.as_path().parent();
        if (parent.is_none()) {
            return failure<empty>(rstd::format(
                "fingerprint path '{}' has no parent", unit.unit.fingerprint.as_path()));
        }
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return failure<empty>(rstd::format(
                "cannot create fingerprint directory '{}': {}",
                *parent,
                rstd::move(created).unwrap_err()));
        }

        auto contents = String::make(key);
        contents.push_ascii('\n');
        auto bytes = Vec<u8>::from(contents.as_str().as_bytes());
        auto written = rstd::fs::write(unit.unit.fingerprint.as_path(), bytes.as_slice());
        if (written.is_err()) {
            return failure<empty>(rstd::format(
                "cannot write '{}': {}",
                unit.unit.fingerprint.as_path(),
                rstd::move(written).unwrap_err()));
        }
        return Ok(empty {});
    }

private:
    auto file_identity(ref<rstd::path::Path> path) -> Result<String> {
        auto text = path.to_str();
        if (text.is_none()) {
            return failure<String>(
                rstd::format("input path '{}' is not valid UTF-8", path));
        }
        auto cached = file_identities_.get(*text);
        if (cached.is_some()) return Ok((**cached).clone());

        auto contents = rstd::fs::read(path);
        if (contents.is_err()) {
            return failure<String>(rstd::format(
                "cannot hash input '{}': {}", path, rstd::move(contents).unwrap_err()));
        }
        auto hash = FNV_OFFSET;
        add_bytes(hash, contents->as_slice());
        auto identity = hex(hash);
        file_identities_.insert(String::make(*text), identity.clone());
        return Ok(rstd::move(identity));
    }

    rstd::collections::BTreeMap<String, String> file_identities_;
};

} // namespace tenon
