module;
#include <rstd/enum.hpp>

export module lito.driver:sdk;

import rstd;
import lito.tools;
import rstd.json;
import lito.core;
import :config.project;
import lito.toolchain;
import :build.error;

using namespace rstd::prelude;

export namespace lito
{

class SdkError {
    RSTD_ENUM(SdkError,
              (Catalog, (LlvmSdkCatalogError source;)),
              (Acquisition, (lito::tools::acquisition::AcquisitionError source;)),
              (Toolchain, (ToolchainError source;)),
              (Build, (BuildError source;)),
              (Tools, (lito::tools::ToolError source;)),
              (SourceTree, (lito::source::SourceTreeError source;)),
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
    Build,
    Link,
    Install,
    Certify,
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
    bool            active { false };
};

struct SdkListSummary {
    String            host;
    Vec<SdkListEntry> entries;
    Option<String>    active_issue;
};

struct SdkInstallRequest {
    String                                      version;
    lito::system::ProcessEnvironmentSpec        environment;
    lito::tools::ToolSpec                       tools;
    lito::config::ToolchainSpec                 toolchain;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
    Option<SdkEventSink>                        observer;
};

struct SdkInstallSummary {
    String  version;
    String  host;
    PathBuf prefix;
    bool    reused { false };
};

struct SdkActivateRequest {
    String version;
};

struct SdkActivateSummary {
    String  version;
    String  host;
    PathBuf prefix;
    bool    unchanged { false };
};

struct SdkDeactivateSummary {
    Option<String>  version;
    Option<String>  host;
    Option<PathBuf> prefix;
    bool            invalid_state { false };
    bool            unchanged { false };
};

struct SdkUninstallRequest {
    String version;
};

struct SdkUninstallSummary {
    String         version;
    Option<String> host;
    PathBuf        prefix;
    bool           was_active { false };
    bool           invalid_entry { false };
    bool           recovered { false };
};

class ActiveSdkLease {
    String                              m_version;
    String                              m_host;
    PathBuf                             m_prefix;
    lito::config::ProjectConfigDefaults m_defaults;
    rstd::fs::FileLock                  m_lock;

    ActiveSdkLease(String                              version,
                   String                              host,
                   PathBuf                             prefix,
                   lito::config::ProjectConfigDefaults defaults,
                   rstd::fs::FileLock                  lock) noexcept;
    friend auto acquire_active_llvm_sdk() -> SdkResult<Option<ActiveSdkLease>>;

public:
    ActiveSdkLease(const ActiveSdkLease&)                        = delete;
    auto operator=(const ActiveSdkLease&) -> ActiveSdkLease&     = delete;
    ActiveSdkLease(ActiveSdkLease&&) noexcept                    = default;
    auto operator=(ActiveSdkLease&&) noexcept -> ActiveSdkLease& = default;
    ~ActiveSdkLease()                                            = default;

    auto version() const noexcept -> ref<str> { return m_version.as_str(); }
    auto host() const noexcept -> ref<str> { return m_host.as_str(); }
    auto prefix() const noexcept -> ref<rstd::path::Path> { return m_prefix.as_path(); }
    auto project_defaults() const -> lito::config::ProjectConfigDefaults;
};

auto list_llvm_sdks() -> SdkResult<SdkListSummary>;
auto install_llvm_sdk(SdkInstallRequest request) -> SdkResult<SdkInstallSummary>;
auto activate_llvm_sdk(SdkActivateRequest request) -> SdkResult<SdkActivateSummary>;
auto deactivate_llvm_sdk() -> SdkResult<SdkDeactivateSummary>;
auto uninstall_llvm_sdk(SdkUninstallRequest request) -> SdkResult<SdkUninstallSummary>;
auto acquire_active_llvm_sdk() -> SdkResult<Option<ActiveSdkLease>>;

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
            return formatter.write_raw("LLVM SDK toolchain operation failed",
                                       sizeof("LLVM SDK toolchain operation failed") - 1);
        }
        if (error.is_Build()) {
            return formatter.write_raw("LLVM SDK component build failed",
                                       sizeof("LLVM SDK component build failed") - 1);
        }
        if (error.is_Tools()) {
            return formatter.write_raw("LLVM SDK host tool operation failed",
                                       sizeof("LLVM SDK host tool operation failed") - 1);
        }
        if (error.is_SourceTree()) {
            return formatter.write_raw("LLVM SDK recipe materialization failed",
                                       sizeof("LLVM SDK recipe materialization failed") - 1);
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
        if (error.is_Build()) return Some(dyn<error::Error>::from_ref(error.as_Build().source));
        if (error.is_Tools()) return Some(dyn<error::Error>::from_ref(error.as_Tools().source));
        if (error.is_SourceTree()) {
            return Some(dyn<error::Error>::from_ref(error.as_SourceTree().source));
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
