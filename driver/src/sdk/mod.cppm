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
              (AndroidCatalog, (AndroidNdkCatalogError source;)),
              (AndroidNdk, (AndroidNdkError source;)),
              (Acquisition, (lito::tools::acquisition::AcquisitionError source;)),
              (Toolchain, (ToolchainError source;)),
              (Build, (BuildError source;)),
              (Tools, (lito::tools::ToolError source;)),
              (SourceTree, (lito::source::SourceTreeError source;)),
              (Platform, (lito::system::PlatformError source;)),
              (System, (lito::system::SystemError source;)),
              (Json, (PathBuf path; rstd::json::Error source;)),
              (Parse, (lito::parse::Error source;)),
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
    lito::config::SdkKind sdk { lito::config::SdkKind::Llvm };
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

struct AndroidNdkInstallRequest {
    String                                      version;
    bool                                        accept_license { false };
    lito::system::ProcessEnvironmentSpec        environment;
    lito::tools::ToolSpec                       tools;
    Option<lito::tools::HostToolResolutionSink> tool_reporter;
    Option<SdkEventSink>                        observer;
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

class AndroidNdkLease {
    String             m_version;
    String             m_host;
    PathBuf            m_prefix;
    PathBuf            m_root;
    String             m_identity;
    rstd::fs::FileLock m_lock;

public:
    AndroidNdkLease(String             version,
                    String             host,
                    PathBuf            prefix,
                    PathBuf            root,
                    String             identity,
                    rstd::fs::FileLock lock) noexcept;
    friend auto acquire_active_android_ndk() -> SdkResult<Option<AndroidNdkLease>>;
    friend auto acquire_android_ndk(ref<str> version) -> SdkResult<AndroidNdkLease>;

    AndroidNdkLease(const AndroidNdkLease&)                        = delete;
    auto operator=(const AndroidNdkLease&) -> AndroidNdkLease&     = delete;
    AndroidNdkLease(AndroidNdkLease&&) noexcept                    = default;
    auto operator=(AndroidNdkLease&&) noexcept -> AndroidNdkLease& = default;
    ~AndroidNdkLease()                                             = default;

    auto version() const noexcept -> ref<str> { return m_version.as_str(); }
    auto host() const noexcept -> ref<str> { return m_host.as_str(); }
    auto prefix() const noexcept -> ref<rstd::path::Path> { return m_prefix.as_path(); }
    auto root() const noexcept -> ref<rstd::path::Path> { return m_root.as_path(); }
    auto identity() const noexcept -> ref<str> { return m_identity.as_str(); }
    auto project_defaults() const -> lito::config::ProjectConfigDefaults;
};

auto list_llvm_sdks() -> SdkResult<SdkListSummary>;
auto install_llvm_sdk(SdkInstallRequest request) -> SdkResult<SdkInstallSummary>;
auto activate_llvm_sdk(SdkActivateRequest request) -> SdkResult<SdkActivateSummary>;
auto deactivate_llvm_sdk() -> SdkResult<SdkDeactivateSummary>;
auto uninstall_llvm_sdk(SdkUninstallRequest request) -> SdkResult<SdkUninstallSummary>;
auto acquire_active_llvm_sdk() -> SdkResult<Option<ActiveSdkLease>>;
auto list_android_ndks() -> SdkResult<SdkListSummary>;
auto install_android_ndk(AndroidNdkInstallRequest request) -> SdkResult<SdkInstallSummary>;
auto activate_android_ndk(SdkActivateRequest request) -> SdkResult<SdkActivateSummary>;
auto deactivate_android_ndk() -> SdkResult<SdkDeactivateSummary>;
auto uninstall_android_ndk(SdkUninstallRequest request) -> SdkResult<SdkUninstallSummary>;
auto acquire_active_android_ndk() -> SdkResult<Option<AndroidNdkLease>>;
auto acquire_android_ndk(ref<str> version) -> SdkResult<AndroidNdkLease>;

} // namespace lito

export namespace rstd
{

template<>
struct Impl<convert::From<lito::parse::Error>, lito::SdkError> {
    static auto from(lito::parse::Error error) -> lito::SdkError {
        return lito::SdkError::Parse(rstd::move(error));
    }
};

template<>
struct Impl<fmt::Display, lito::SdkError> : ImplBase<lito::SdkError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const auto& error = this->self();
        if (error.is_Catalog()) {
            return formatter.write_raw("LLVM SDK catalog operation failed",
                                       sizeof("LLVM SDK catalog operation failed") - 1);
        }
        if (error.is_AndroidCatalog()) {
            return formatter.write_raw("Android NDK catalog operation failed",
                                       sizeof("Android NDK catalog operation failed") - 1);
        }
        if (error.is_AndroidNdk()) {
            return formatter.write_raw("Android NDK operation failed",
                                       sizeof("Android NDK operation failed") - 1);
        }
        if (error.is_Acquisition()) {
            return formatter.write_raw("SDK acquisition failed",
                                       sizeof("SDK acquisition failed") - 1);
        }
        if (error.is_Toolchain()) {
            return formatter.write_raw("SDK toolchain operation failed",
                                       sizeof("SDK toolchain operation failed") - 1);
        }
        if (error.is_Build()) {
            return formatter.write_raw("LLVM SDK component build failed",
                                       sizeof("LLVM SDK component build failed") - 1);
        }
        if (error.is_Tools()) {
            return formatter.write_raw("SDK host tool operation failed",
                                       sizeof("SDK host tool operation failed") - 1);
        }
        if (error.is_SourceTree()) {
            return formatter.write_raw("LLVM SDK recipe materialization failed",
                                       sizeof("LLVM SDK recipe materialization failed") - 1);
        }
        if (error.is_Platform()) {
            return formatter.write_raw("SDK host detection failed",
                                       sizeof("SDK host detection failed") - 1);
        }
        if (error.is_System()) {
            return formatter.write_raw("SDK environment operation failed",
                                       sizeof("SDK environment operation failed") - 1);
        }
        if (error.is_Json()) {
            return formatter.write_fmt(fmt::Arguments::make("cannot parse LLVM SDK descriptor '{}'",
                                                            error.as_Json().path.as_path()));
        }
        if (error.is_Parse()) return as<fmt::Display>(error.as_Parse().source).fmt(formatter);
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
        if (error.is_AndroidCatalog()) {
            return Some(dyn<error::Error>::from_ref(error.as_AndroidCatalog().source));
        }
        if (error.is_AndroidNdk()) {
            return Some(lito::android_ndk_error_ref(error.as_AndroidNdk().source));
        }
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
            return Some(lito::system::platform_error_ref(error.as_Platform().source));
        }
        if (error.is_System()) return Some(dyn<error::Error>::from_ref(error.as_System().source));
        if (error.is_Json()) return Some(dyn<error::Error>::from_ref(error.as_Json().source));
        if (error.is_Parse()) return Some(dyn<error::Error>::from_ref(error.as_Parse().source));
        if (error.is_Io()) return Some(dyn<error::Error>::from_ref(error.as_Io().source));
        return None();
    }
};

} // namespace rstd
