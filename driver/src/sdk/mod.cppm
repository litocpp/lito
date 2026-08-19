module;
#include <rstd/enum.hpp>

export module lito.driver:sdk;

import rstd;
import rstd.json;
import lito.core;
import lito.toolchain;

using namespace rstd::prelude;

export namespace lito
{

class SdkError {
    RSTD_ENUM(SdkError,
              (Catalog, (LlvmSdkCatalogError source;)),
              (Acquisition, (lito::acquisition::AcquisitionError source;)),
              (Toolchain, (ToolchainError source;)),
              (Platform, (lito::system::PlatformError source;)),
              (System, (lito::system::SystemError source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Io, (String operation; PathBuf path; rstd::io::error::Error source;)),
              (Message, (String message;)))
};

template<typename T>
using SdkResult = Result<T, SdkError>;

enum class SdkEventKind
{
    Fetch,
    Extract,
};

struct SdkEvent {
    SdkEventKind          kind { SdkEventKind::Fetch };
    ref<str>              version;
    ref<str>              source;
    ref<rstd::path::Path> destination;
};

struct SdkEventSink {
    void* context {};
    void (*notify)(void*, const SdkEvent&) noexcept {};
};

enum class SdkListStatus
{
    Available,
    Installed,
    InstalledUnavailable,
    Invalid,
};

struct SdkListEntry {
    String          version;
    String          host;
    SdkListStatus   status { SdkListStatus::Available };
    Option<PathBuf> prefix;
    Option<String>  issue;
};

struct SdkListSummary {
    String            host;
    Vec<SdkListEntry> entries;
};

struct SdkInstallRequest {
    String                                       version;
    lito::system::ProcessEnvironmentSpec         environment;
    lito::system::ToolSpec                       tools;
    Option<lito::system::HostToolResolutionSink> tool_reporter;
    Option<SdkEventSink>                         observer;
};

struct SdkInstallSummary {
    String  version;
    String  host;
    PathBuf prefix;
    bool    reused { false };
};

auto list_llvm_sdks() -> SdkResult<SdkListSummary>;
auto install_llvm_sdk(SdkInstallRequest request) -> SdkResult<SdkInstallSummary>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<fmt::Display, lito::SdkError> : ImplBase<lito::SdkError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Catalog()) {
            return formatter.write_raw("LLVM SDK catalog operation failed",
                                       sizeof("LLVM SDK catalog operation failed") - 1);
        }
        if (error.is_Acquisition()) {
            return formatter.write_raw("LLVM SDK acquisition failed",
                                       sizeof("LLVM SDK acquisition failed") - 1);
        }
        if (error.is_Toolchain()) {
            return formatter.write_raw("LLVM SDK certification failed",
                                       sizeof("LLVM SDK certification failed") - 1);
        }
        if (error.is_Platform()) {
            return formatter.write_raw("LLVM SDK host detection failed",
                                       sizeof("LLVM SDK host detection failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("LLVM SDK environment operation failed",
                                       sizeof("LLVM SDK environment operation failed") - 1);
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse LLVM SDK descriptor '{}'",
                                                            error.as_Json().path.as_path()));
        }
        if (error.is_Io()) {
            const auto& value = error.as_Io();
            return formatter.write_fmt(
                fmt::Arguments::make("cannot {} '{}'", value.operation, value.path.as_path()));
        }
        return formatter.write_str(error.as_Message().message.as_str());
    }
};

template<>
struct Impl<fmt::Debug, lito::SdkError> : ImplBase<lito::SdkError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, lito::SdkError> : ImplBase<lito::SdkError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        const auto& error = this->self();
        if (error.is_Catalog()) return Some(dyn<error::Error>::from_ref(error.as_Catalog().source));
        if (error.is_Acquisition()) {
            return Some(dyn<error::Error>::from_ref(error.as_Acquisition().source));
        }
        if (error.is_Toolchain()) {
            return Some(dyn<error::Error>::from_ref(error.as_Toolchain().source));
        }
        if (error.is_Platform()) {
            return Some(dyn<error::Error>::from_ref(error.as_Platform().source));
        }
        if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
