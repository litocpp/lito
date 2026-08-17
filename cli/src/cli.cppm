module;
#include <rstd/enum.hpp>

export module lito.executable:cli;

import rstd;
import rstd.argparse;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::argparse;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

export namespace lito::cli
{

struct BuildOptions {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              targets;
    Option<PathBuf>                          output;
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    bool                                     verbose {};
    Option<PathBuf>                          timing_file;
    bool                                     no_timing {};
    Option<usize>                            jobs;
    lito::package::FeatureSelection          features;
};

struct ScanOptions {
    PathBuf                                  source;
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              targets;
    ScanOutputFormat                         format { ScanOutputFormat::Lito };
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    lito::package::FeatureSelection          features;
};

struct DocOptions {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              targets;
    Option<PathBuf>                          output;
    Option<PathBuf>                          data_output;
    Option<PathBuf>                          frontend;
    bool                                     data_only {};
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    bool                                     verbose {};
    Option<PathBuf>                          timing_file;
    bool                                     no_timing {};
    Option<usize>                            jobs;
    lito::package::FeatureSelection          features;
};

struct InstallOptions {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              binaries;
    InstallDestinationRequirement            destination;
    bool                                     force {};
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    bool                                     verbose {};
    Option<PathBuf>                          timing_file;
    bool                                     no_timing {};
    Option<usize>                            jobs;
    lito::package::FeatureSelection          features;
};

struct TestOptions {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              targets;
    Option<PathBuf>                          output;
    Vec<String>                              arguments;
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    bool                                     no_run {};
    bool                                     verbose {};
    Option<PathBuf>                          timing_file;
    bool                                     no_timing {};
    Option<usize>                            jobs;
    lito::package::FeatureSelection          features;
};

struct BenchOptions {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    Vec<String>                              targets;
    Option<PathBuf>                          output;
    Vec<String>                              arguments;
    bool                                     locked {};
    bool                                     offline {};
    bool                                     frozen {};
    Vec<PathBuf>                             fetch_seeds;
    bool                                     no_run {};
    bool                                     verbose {};
    Option<PathBuf>                          timing_file;
    bool                                     no_timing {};
    Option<usize>                            jobs;
    lito::package::FeatureSelection          features;
};

struct FormatOptions {
    Vec<String> packages;
    bool        check {};
};

struct UpdateOptions {
    bool         offline {};
    Vec<PathBuf> fetch_seeds;
};

struct LockExportOptions {
    String  format;
    PathBuf output;
};

struct ConfigGetOptions {
    Option<String> key;
};

struct ConfigSetOptions {
    String key;
    String value;
};

struct ConfigUnsetOptions {
    String key;
};

class LockCommand {
    RSTD_ENUM(LockCommand, (Export, (LockExportOptions options;)))
};

class ConfigCommand {
    RSTD_ENUM(ConfigCommand,
              (Path),
              (Get, (ConfigGetOptions options;)),
              (Set, (ConfigSetOptions options;)),
              (Unset, (ConfigUnsetOptions options;)))
};

class CliCommand {
    RSTD_ENUM(CliCommand,
              (Build, (BuildOptions options;)),
              (Install, (InstallOptions options;)),
              (Test, (TestOptions options;)),
              (Bench, (BenchOptions options;)),
              (Doc, (DocOptions options;)),
              (Scan, (ScanOptions options;)),
              (Format, (FormatOptions options;)),
              (Update, (UpdateOptions options;)),
              (Lock, (LockCommand command;)),
              (Config, (ConfigCommand command;)))
};

class CliOutcome {
    RSTD_ENUM(CliOutcome,
              (Parsed,
               (PathBuf working_directory; bool no_config; Vec<String> config_overrides;
                lito::config::ToolchainOverride                        toolchain;
                Option<String>                                         toolchain_standard_library;
                CliCommand                                             command;)),
              (Exit, (String output; bool standard_error; i32 exit_code;)))
};

} // namespace lito::cli

namespace lito::cli
{

inline constexpr auto LITO_VERSION_TEXT = LITO_PKG_VERSION;
inline constexpr auto LITO_VERSION_SIZE = sizeof(LITO_PKG_VERSION) - sizeof(char);

auto lito_version() noexcept -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(
        reinterpret_cast<const byte*>(LITO_VERSION_TEXT), usize(LITO_VERSION_SIZE));
}

class BuildProfileParser {
public:
    auto parse(ref<OsStr> value) const -> Result<lito::manifest::BuildProfileName, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        auto profile = lito::manifest::parse_build_profile(*text);
        if (profile.is_ok()) return Ok(rstd::move(profile).unwrap());
        return Err(ValueError::Message(rstd::format("{}", rstd::move(profile).unwrap_err())));
    }
};

class ScanOutputFormatParser {
public:
    auto parse(ref<OsStr> value) const -> Result<ScanOutputFormat, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        auto format = parse_scan_output_format(*text);
        if (format.is_ok()) return Ok(rstd::move(format).unwrap());
        return Err(ValueError::Message(rstd::format("{}", rstd::move(format).unwrap_err())));
    }

    auto possible_values() const -> Vec<String> {
        auto values = Vec<String>::with_capacity(usize(2));
        values.push(String::make(scan_output_format_name(ScanOutputFormat::Lito)));
        values.push(String::make(scan_output_format_name(ScanOutputFormat::P1689)));
        return values;
    }
};

class CliDecodeError {
    RSTD_ENUM(CliDecodeError,
              (MatchAccess, (MatchAccessError error;)),
              (MissingValue, (String argument;)),
              (CommandMismatch, (String command;)),
              (InvalidUsage, (String message;)))
};

struct PackageProfileArgs {
    ArgKey<String>                           package;
    ArgKey<lito::manifest::BuildProfileName> profile;
    ArgKey<String>                           features;
    ArgKey<bool>                             no_default_features;
};

struct SourceAcquisitionArgs {
    ArgKey<bool>   locked;
    ArgKey<bool>   offline;
    ArgKey<bool>   frozen;
    ArgKey<String> fetch_seed;
};

struct BuildExecutionArgs {
    ArgKey<bool>   verbose;
    ArgKey<String> timing_file;
    ArgKey<bool>   no_timing;
    ArgKey<usize>  jobs;
};

struct ToolchainArgs {
    ArgKey<String> cc;
    ArgKey<String> cxx;
    ArgKey<String> ld;
    ArgKey<String> ar;
    ArgKey<String> strip;
    ArgKey<String> format;
    ArgKey<String> stdlib;
};

struct RootArgs {
    ArgKey<String> directory;
    ArgKey<bool>   no_config;
    ArgKey<String> config;
    ToolchainArgs  toolchain;
};

struct BuildSchema {
    CommandKey            command;
    PackageProfileArgs    package;
    ArgKey<String>        target;
    ArgKey<String>        output;
    SourceAcquisitionArgs source_acquisition;
    BuildExecutionArgs    execution;

    auto decode(const Matches& matches) const -> Result<BuildOptions, CliDecodeError>;
};

struct InstallSchema {
    CommandKey            command;
    PackageProfileArgs    package;
    ArgKey<String>        binary;
    ArgKey<String>        root;
    ArgKey<String>        prefix;
    ArgKey<bool>          force;
    SourceAcquisitionArgs source_acquisition;
    BuildExecutionArgs    execution;

    auto decode(const Matches& matches) const -> Result<InstallOptions, CliDecodeError>;
};

struct TestSchema {
    CommandKey            command;
    PackageProfileArgs    package;
    ArgKey<String>        target;
    ArgKey<String>        output;
    SourceAcquisitionArgs source_acquisition;
    ArgKey<bool>          no_run;
    BuildExecutionArgs    execution;
    ArgKey<String>        arguments;

    auto decode(const Matches& matches) const -> Result<TestOptions, CliDecodeError>;
};

struct BenchSchema {
    CommandKey            command;
    PackageProfileArgs    package;
    ArgKey<String>        target;
    ArgKey<String>        output;
    SourceAcquisitionArgs source_acquisition;
    ArgKey<bool>          no_run;
    BuildExecutionArgs    execution;
    ArgKey<String>        arguments;

    auto decode(const Matches& matches) const -> Result<BenchOptions, CliDecodeError>;
};

struct ScanSchema {
    CommandKey               command;
    ArgKey<String>           source;
    PackageProfileArgs       package;
    ArgKey<String>           target;
    ArgKey<ScanOutputFormat> format;
    SourceAcquisitionArgs    source_acquisition;

    auto decode(const Matches& matches) const -> Result<ScanOptions, CliDecodeError>;
};

struct DocSchema {
    CommandKey            command;
    PackageProfileArgs    package;
    ArgKey<String>        target;
    ArgKey<String>        output;
    ArgKey<String>        data_output;
    ArgKey<String>        frontend;
    ArgKey<bool>          data_only;
    SourceAcquisitionArgs source_acquisition;
    BuildExecutionArgs    execution;

    auto decode(const Matches& matches) const -> Result<DocOptions, CliDecodeError>;
};

struct FormatSchema {
    CommandKey     command;
    ArgKey<String> package;
    ArgKey<bool>   check;

    auto decode(const Matches& matches) const -> Result<FormatOptions, CliDecodeError>;
};

struct UpdateSchema {
    CommandKey     command;
    ArgKey<bool>   offline;
    ArgKey<String> fetch_seed;

    auto decode(const Matches& matches) const -> Result<UpdateOptions, CliDecodeError>;
};

struct LockExportSchema {
    CommandKey     command;
    ArgKey<String> format;
    ArgKey<String> output;

    auto decode(const Matches& matches) const -> Result<LockExportOptions, CliDecodeError>;
};

struct LockSchema {
    CommandKey       command;
    LockExportSchema export_command;

    auto decode(const Matches& matches) const -> Result<LockCommand, CliDecodeError>;
};

struct ConfigGetSchema {
    CommandKey     command;
    ArgKey<String> key;

    auto decode(const Matches& matches) const -> Result<ConfigGetOptions, CliDecodeError>;
};

struct ConfigSetSchema {
    CommandKey     command;
    ArgKey<String> key;
    ArgKey<String> value;

    auto decode(const Matches& matches) const -> Result<ConfigSetOptions, CliDecodeError>;
};

struct ConfigUnsetSchema {
    CommandKey     command;
    ArgKey<String> key;

    auto decode(const Matches& matches) const -> Result<ConfigUnsetOptions, CliDecodeError>;
};

struct ConfigSchema {
    CommandKey        command;
    CommandKey        path;
    ConfigGetSchema   get;
    ConfigSetSchema   set;
    ConfigUnsetSchema unset;

    auto decode(const Matches& matches) const -> Result<ConfigCommand, CliDecodeError>;
};

struct CliSchema {
    RootArgs      root;
    BuildSchema   build;
    InstallSchema install;
    TestSchema    test;
    BenchSchema   bench;
    DocSchema     doc;
    ScanSchema    scan;
    FormatSchema  format;
    UpdateSchema  update;
    LockSchema    lock;
    ConfigSchema  config;
    Parser        parser;
};

template<typename Schema>
struct CommandDefinition {
    Schema  schema;
    Command command;
};

auto package_arg() -> Arg<String> {
    return Arg<String>::value("package"_str, string_parser())
        .short_name(u8('p'))
        .long_name("package"_str)
        .value_name("NAME"_str)
        .help("Select a package"_str)
        .append();
}

auto profile_arg() -> Arg<lito::manifest::BuildProfileName> {
    return Arg<lito::manifest::BuildProfileName>::value("profile"_str, BuildProfileParser {})
        .long_name("profile"_str)
        .value_name("PROFILE"_str)
        .help("Select the build profile"_str);
}

auto features_arg() -> Arg<String> {
    return Arg<String>::value("features"_str, string_parser())
        .long_name("features"_str)
        .value_name("FEATURES"_str)
        .help("Enable comma-separated package features"_str)
        .append();
}

auto no_default_features_arg() -> Arg<bool> {
    return Arg<bool>::flag("no-default-features"_str)
        .long_name("no-default-features"_str)
        .help("Disable default package features"_str);
}

auto target_arg() -> Arg<String> {
    return Arg<String>::value("target"_str, string_parser())
        .long_name("target"_str)
        .value_name("NAME"_str)
        .help("Select a target"_str)
        .append();
}

auto output_arg() -> Arg<String> {
    return Arg<String>::value("out"_str, string_parser())
        .long_name("out"_str)
        .value_name("DIRECTORY"_str)
        .help("Override the build output directory"_str);
}

auto locked_arg() -> Arg<bool> {
    return Arg<bool>::flag("locked"_str)
        .long_name("locked"_str)
        .help("Require an unchanged lock file"_str);
}

auto offline_arg() -> Arg<bool> {
    return Arg<bool>::flag("offline"_str)
        .long_name("offline"_str)
        .help("Forbid network source acquisition"_str);
}

auto frozen_arg() -> Arg<bool> {
    return Arg<bool>::flag("frozen"_str)
        .long_name("frozen"_str)
        .help("Require an unchanged lock file and forbid network access"_str);
}

auto fetch_seed_arg() -> Arg<String> {
    return Arg<String>::value("fetch-seed"_str, string_parser())
        .long_name("fetch-seed"_str)
        .value_name("DIRECTORY"_str)
        .help("Use a read-only fetch seed directory"_str)
        .append();
}

auto no_config_arg() -> Arg<bool> {
    return Arg<bool>::flag("no-config"_str)
        .long_name("no-config"_str)
        .help("Ignore .lito/config.toml"_str)
        .global();
}

auto config_override_arg() -> Arg<String> {
    return Arg<String>::value("config"_str, string_parser())
        .short_name(u8('c'))
        .long_name("config"_str)
        .value_name("KEY=VALUE"_str)
        .help("Override a configuration value for this invocation"_str)
        .append()
        .global();
}

auto toolchain_arg(ref<str> id, ref<str> name, ref<str> help) -> Arg<String> {
    return Arg<String>::value(id, string_parser())
        .long_name(name)
        .value_name("TOOL"_str)
        .help(help)
        .help_heading("Toolchain"_str)
        .global();
}

auto add_toolchain_args(Command& command) -> ToolchainArgs {
    return ToolchainArgs {
        .cc = command.add_arg(
            toolchain_arg("toolchain-cc"_str, "toolchain.cc"_str, "Set the C compiler"_str)),
        .cxx = command.add_arg(
            toolchain_arg("toolchain-cxx"_str, "toolchain.cxx"_str, "Set the C++ compiler"_str)),
        .ld = command.add_arg(
            toolchain_arg("toolchain-ld"_str, "toolchain.ld"_str, "Set the LLD linker"_str)),
        .ar = command.add_arg(
            toolchain_arg("toolchain-ar"_str, "toolchain.ar"_str, "Set the LLVM archiver"_str)),
        .strip  = command.add_arg(toolchain_arg(
            "toolchain-strip"_str, "toolchain.strip"_str, "Set the LLVM strip executable"_str)),
        .format = command.add_arg(toolchain_arg(
            "toolchain-format"_str, "toolchain.format"_str, "Set the ClangFormat executable"_str)),
        .stdlib = command.add_arg(Arg<String>::value("toolchain-stdlib"_str, string_parser())
                                      .long_name("toolchain.stdlib"_str)
                                      .value_name("STDLIB"_str)
                                      .help("Set the C++ standard library"_str)
                                      .help_heading("Toolchain"_str)
                                      .global()),
    };
}

auto timing_file_arg() -> Arg<String> {
    return Arg<String>::value("timing-file"_str, string_parser())
        .long_name("timing-file"_str)
        .value_name("FILE"_str)
        .help("Write the detailed timing report to a file"_str);
}

auto no_timing_arg() -> Arg<bool> {
    return Arg<bool>::flag("no-timing"_str)
        .long_name("no-timing"_str)
        .help("Hide timing output on stdout"_str);
}

auto jobs_arg() -> Arg<usize> {
    return Arg<usize>::value("jobs"_str, from_str_parser<usize>())
        .short_name(u8('j'))
        .long_name("jobs"_str)
        .value_name("N"_str)
        .help("Set the scan and compile worker count"_str);
}

auto add_package_profile_args(Command& command) -> PackageProfileArgs {
    return PackageProfileArgs {
        .package = command.add_arg(package_arg()),
        .profile = command.add_arg(profile_arg()),
        .features = command.add_arg(features_arg()),
        .no_default_features = command.add_arg(no_default_features_arg()),
    };
}

auto add_source_acquisition_args(Command& command) -> SourceAcquisitionArgs {
    return SourceAcquisitionArgs {
        .locked     = command.add_arg(locked_arg()),
        .offline    = command.add_arg(offline_arg()),
        .frozen     = command.add_arg(frozen_arg()),
        .fetch_seed = command.add_arg(fetch_seed_arg()),
    };
}

auto add_build_execution_args(Command& command) -> BuildExecutionArgs {
    return BuildExecutionArgs {
        .verbose = command.add_arg(
            Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str)),
        .timing_file = command.add_arg(timing_file_arg()),
        .no_timing   = command.add_arg(no_timing_arg()),
        .jobs        = command.add_arg(jobs_arg()),
    };
}

auto make_build_definition() -> CommandDefinition<BuildSchema> {
    auto command = Command::make("build"_str);
    command.about("Build packages"_str);
    auto key                = command.key();
    auto package            = add_package_profile_args(command);
    auto target             = command.add_arg(target_arg());
    auto output             = command.add_arg(output_arg());
    auto source_acquisition = add_source_acquisition_args(command);
    auto execution          = add_build_execution_args(command);
    return {
        BuildSchema {
            .command            = key,
            .package            = rstd::move(package),
            .target             = target,
            .output             = output,
            .source_acquisition = rstd::move(source_acquisition),
            .execution          = rstd::move(execution),
        },
        rstd::move(command),
    };
}

auto make_install_definition() -> CommandDefinition<InstallSchema> {
    auto command = Command::make("install"_str);
    command.about("Build and install packages"_str);
    auto key     = command.key();
    auto package = add_package_profile_args(command);
    auto binary  = command.add_arg(Arg<String>::value("bin"_str, string_parser())
                                       .long_name("bin"_str)
                                       .value_name("NAME"_str)
                                       .help("Install only the selected binary"_str)
                                       .append());
    auto root    = command.add_arg(Arg<String>::value("root"_str, string_parser())
                                       .long_name("root"_str)
                                       .value_name("DIRECTORY"_str)
                                       .help("Set the Lito-managed install root"_str));
    auto prefix  = command.add_arg(Arg<String>::value("prefix"_str, string_parser())
                                       .long_name("prefix"_str)
                                       .value_name("DIRECTORY"_str)
                                       .help("Install an untracked prefix tree"_str));
    command.conflicts(root, prefix);
    auto force = command.add_arg(
        Arg<bool>::flag("force"_str).long_name("force"_str).help("Replace conflicting files"_str));
    auto source_acquisition = add_source_acquisition_args(command);
    auto execution          = add_build_execution_args(command);
    return {
        InstallSchema {
            .command            = key,
            .package            = rstd::move(package),
            .binary             = binary,
            .root               = root,
            .prefix             = prefix,
            .force              = force,
            .source_acquisition = rstd::move(source_acquisition),
            .execution          = rstd::move(execution),
        },
        rstd::move(command),
    };
}

auto make_test_definition() -> CommandDefinition<TestSchema> {
    auto command = Command::make("test"_str);
    command.about("Build and run test packages"_str);
    auto key                = command.key();
    auto package            = add_package_profile_args(command);
    auto target             = command.add_arg(target_arg());
    auto output             = command.add_arg(output_arg());
    auto source_acquisition = add_source_acquisition_args(command);
    auto no_run             = command.add_arg(Arg<bool>::flag("no-run"_str)
                                                  .long_name("no-run"_str)
                                                  .help("Build tests without running"_str));
    auto execution          = add_build_execution_args(command);
    auto arguments          = command.add_arg(Arg<String>::value("arguments"_str, string_parser())
                                                  .value_name("ARGS"_str)
                                                  .num_args(NumArgs::any())
                                                  .allow_hyphen_values());
    return {
        TestSchema {
            .command            = key,
            .package            = rstd::move(package),
            .target             = target,
            .output             = output,
            .source_acquisition = rstd::move(source_acquisition),
            .no_run             = no_run,
            .execution          = rstd::move(execution),
            .arguments          = arguments,
        },
        rstd::move(command),
    };
}

auto make_bench_definition() -> CommandDefinition<BenchSchema> {
    auto command = Command::make("bench"_str);
    command.about("Build and run benchmarks"_str);
    auto key                = command.key();
    auto package            = add_package_profile_args(command);
    auto target             = command.add_arg(target_arg());
    auto output             = command.add_arg(output_arg());
    auto source_acquisition = add_source_acquisition_args(command);
    auto no_run             = command.add_arg(Arg<bool>::flag("no-run"_str)
                                                  .long_name("no-run"_str)
                                                  .help("Build benchmarks without running"_str));
    auto execution          = add_build_execution_args(command);
    auto arguments          = command.add_arg(Arg<String>::value("arguments"_str, string_parser())
                                                  .value_name("ARGS"_str)
                                                  .num_args(NumArgs::any())
                                                  .allow_hyphen_values());
    return {
        BenchSchema {
            .command            = key,
            .package            = rstd::move(package),
            .target             = target,
            .output             = output,
            .source_acquisition = rstd::move(source_acquisition),
            .no_run             = no_run,
            .execution          = rstd::move(execution),
            .arguments          = arguments,
        },
        rstd::move(command),
    };
}

auto make_scan_definition() -> CommandDefinition<ScanSchema> {
    auto command = Command::make("scan"_str);
    command.about("Scan one source file"_str);
    auto key     = command.key();
    auto source  = command.add_arg(Arg<String>::value("source"_str, string_parser())
                                       .value_name("SOURCE"_str)
                                       .help("Source file to scan"_str)
                                       .required());
    auto package = add_package_profile_args(command);
    auto target  = command.add_arg(target_arg());
    auto format =
        command.add_arg(Arg<ScanOutputFormat>::value("format"_str, ScanOutputFormatParser {})
                            .long_name("format"_str)
                            .value_name("FORMAT"_str)
                            .help("Select the JSON output format"_str));
    auto source_acquisition = add_source_acquisition_args(command);
    return {
        ScanSchema {
            .command            = key,
            .source             = source,
            .package            = rstd::move(package),
            .target             = target,
            .format             = format,
            .source_acquisition = rstd::move(source_acquisition),
        },
        rstd::move(command),
    };
}

auto make_doc_definition() -> CommandDefinition<DocSchema> {
    auto command = Command::make("doc"_str);
    command.about("Build package documentation"_str);
    auto key     = command.key();
    auto package = add_package_profile_args(command);
    auto target  = command.add_arg(target_arg());
    auto output  = command.add_arg(Arg<String>::value("doc-output"_str, string_parser())
                                       .long_name("output"_str)
                                       .value_name("DIRECTORY"_str)
                                       .help("Write the documentation site to a directory"_str));
    auto data_output = command.add_arg(Arg<String>::value("doc-data-output"_str, string_parser())
                                           .long_name("data-output"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Write documentation data to a directory"_str));
    auto frontend    = command.add_arg(Arg<String>::value("doc-frontend"_str, string_parser())
                                           .long_name("frontend"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Use a custom documentation frontend"_str));
    auto data_only   = command.add_arg(Arg<bool>::flag("doc-data-only"_str)
                                           .long_name("data-only"_str)
                                           .help("Generate documentation data without a site"_str));
    auto source_acquisition = add_source_acquisition_args(command);
    auto execution          = add_build_execution_args(command);
    return {
        DocSchema {
            .command            = key,
            .package            = rstd::move(package),
            .target             = target,
            .output             = output,
            .data_output        = data_output,
            .frontend           = frontend,
            .data_only          = data_only,
            .source_acquisition = rstd::move(source_acquisition),
            .execution          = rstd::move(execution),
        },
        rstd::move(command),
    };
}

auto make_format_definition() -> CommandDefinition<FormatSchema> {
    auto command = Command::make("format"_str);
    command.about("Format package sources"_str);
    auto key     = command.key();
    auto package = command.add_arg(package_arg());
    auto check   = command.add_arg(Arg<bool>::flag("check"_str)
                                       .long_name("check"_str)
                                       .help("Check formatting without changing files"_str));
    return {
        FormatSchema { .command = key, .package = package, .check = check },
        rstd::move(command),
    };
}

auto make_update_definition() -> CommandDefinition<UpdateSchema> {
    auto command = Command::make("update"_str);
    command.about("Update lock file"_str);
    auto key        = command.key();
    auto offline    = command.add_arg(offline_arg());
    auto fetch_seed = command.add_arg(fetch_seed_arg());
    return {
        UpdateSchema { .command = key, .offline = offline, .fetch_seed = fetch_seed },
        rstd::move(command),
    };
}

auto make_lock_definition() -> CommandDefinition<LockSchema> {
    auto export_command = Command::make("export"_str);
    export_command.about("Export locked source inputs"_str);
    auto export_key = export_command.key();
    auto format     = export_command.add_arg(Arg<String>::value("format"_str, string_parser())
                                                 .long_name("format"_str)
                                                 .value_name("FORMAT"_str)
                                                 .help("Select the exported source format"_str)
                                                 .required());
    auto output     = export_command.add_arg(Arg<String>::value("output"_str, string_parser())
                                                 .long_name("output"_str)
                                                 .value_name("FILE"_str)
                                                 .help("Write exported sources to a file"_str)
                                                 .required());

    auto command = Command::make("lock"_str);
    command.about("Inspect and export the lock file"_str);
    command.require_subcommand();
    auto key = command.key();
    command.add_subcommand(rstd::move(export_command));
    return {
        LockSchema {
            .command = key,
            .export_command =
                LockExportSchema { .command = export_key, .format = format, .output = output },
        },
        rstd::move(command),
    };
}

auto make_config_definition() -> CommandDefinition<ConfigSchema> {
    auto path_command = Command::make("path"_str);
    path_command.about("Print the project configuration path"_str);
    auto path_key = path_command.key();

    auto get_command = Command::make("get"_str);
    get_command.about("Read stored project configuration"_str);
    auto get_key      = get_command.key();
    auto get_argument = get_command.add_arg(Arg<String>::value("key"_str, string_parser())
                                                .value_name("KEY"_str)
                                                .help("Read one stored configuration key"_str));

    auto set_command = Command::make("set"_str);
    set_command.about("Set a stored project configuration value"_str);
    auto set_key          = set_command.key();
    auto set_key_argument = set_command.add_arg(Arg<String>::value("key"_str, string_parser())
                                                    .value_name("KEY"_str)
                                                    .help("Configuration key to set"_str)
                                                    .required());
    auto set_value_argument =
        set_command.add_arg(Arg<String>::value("value"_str, string_parser())
                                .value_name("VALUE"_str)
                                .help("TOML value or unquoted string to store"_str)
                                .allow_hyphen_values()
                                .required());

    auto unset_command = Command::make("unset"_str);
    unset_command.about("Remove a stored project configuration value"_str);
    auto unset_key      = unset_command.key();
    auto unset_argument = unset_command.add_arg(Arg<String>::value("key"_str, string_parser())
                                                    .value_name("KEY"_str)
                                                    .help("Configuration key to remove"_str)
                                                    .required());

    auto command = Command::make("config"_str);
    command.about("Read and write project configuration"_str);
    command.require_subcommand();
    auto key = command.key();
    command.add_subcommand(rstd::move(path_command));
    command.add_subcommand(rstd::move(get_command));
    command.add_subcommand(rstd::move(set_command));
    command.add_subcommand(rstd::move(unset_command));
    return {
        ConfigSchema {
            .command = key,
            .path    = path_key,
            .get     = ConfigGetSchema { .command = get_key, .key = get_argument },
            .set =
                ConfigSetSchema {
                    .command = set_key,
                    .key     = set_key_argument,
                    .value   = set_value_argument,
                },
            .unset = ConfigUnsetSchema { .command = unset_key, .key = unset_argument },
        },
        rstd::move(command),
    };
}

auto make_schema() -> Result<CliSchema, DefinitionError> {
    auto build   = make_build_definition();
    auto install = make_install_definition();
    auto test    = make_test_definition();
    auto bench   = make_bench_definition();
    auto doc     = make_doc_definition();
    auto scan    = make_scan_definition();
    auto format  = make_format_definition();
    auto update  = make_update_definition();
    auto lock    = make_lock_definition();
    auto config  = make_config_definition();

    auto root = Command::make("lito"_str);
    root.about("Module-first C++ builder"_str);
    root.version(lito_version());
    root.require_subcommand();
    auto root_args = RootArgs {
        .directory = root.add_arg(Arg<String>::value("directory"_str, string_parser())
                                      .short_name(u8('C'))
                                      .value_name("DIRECTORY"_str)
                                      .help("Change the working directory"_str)
                                      .default_value("."_str)),
        .no_config = root.add_arg(no_config_arg()),
        .config    = root.add_arg(config_override_arg()),
        .toolchain = add_toolchain_args(root),
    };
    root.add_subcommand(rstd::move(build.command));
    root.add_subcommand(rstd::move(install.command));
    root.add_subcommand(rstd::move(test.command));
    root.add_subcommand(rstd::move(bench.command));
    root.add_subcommand(rstd::move(doc.command));
    root.add_subcommand(rstd::move(scan.command));
    root.add_subcommand(rstd::move(format.command));
    root.add_subcommand(rstd::move(update.command));
    root.add_subcommand(rstd::move(lock.command));
    root.add_subcommand(rstd::move(config.command));

    auto parser = rstd::move(root).build();
    if (parser.is_err()) return Err(rstd::move(parser).unwrap_err());
    return Ok(CliSchema {
        .root    = rstd::move(root_args),
        .build   = rstd::move(build.schema),
        .install = rstd::move(install.schema),
        .test    = rstd::move(test.schema),
        .bench   = rstd::move(bench.schema),
        .doc     = rstd::move(doc.schema),
        .scan    = rstd::move(scan.schema),
        .format  = rstd::move(format.schema),
        .update  = rstd::move(update.schema),
        .lock    = rstd::move(lock.schema),
        .config  = rstd::move(config.schema),
        .parser  = rstd::move(parser).unwrap(),
    });
}

template<typename T>
auto optional_value(const Matches& matches, const ArgKey<T>& key)
    -> Result<Option<ref<T>>, CliDecodeError> {
    auto value = matches.get_one(key);
    if (value.is_err()) {
        return Err(CliDecodeError::MatchAccess(rstd::move(value).unwrap_err()));
    }
    return Ok(rstd::move(value).unwrap());
}

auto string_values(const Matches& matches, const ArgKey<String>& key)
    -> Result<Vec<String>, CliDecodeError> {
    auto values = matches.get_many(key);
    if (values.is_err()) {
        return Err(CliDecodeError::MatchAccess(rstd::move(values).unwrap_err()));
    }
    auto result = Vec<String>::make();
    auto parsed = rstd::move(values).unwrap();
    if (parsed.is_none()) return Ok(rstd::move(result));
    auto iterator = rstd::move(*parsed);
    for (auto value = iterator.next(); value.is_some(); value = iterator.next()) {
        result.push((**value).clone());
    }
    return Ok(rstd::move(result));
}

auto path_values(const Matches& matches, const ArgKey<String>& key)
    -> Result<Vec<PathBuf>, CliDecodeError> {
    auto strings = rstd_try(string_values(matches, key));
    auto result  = Vec<PathBuf>::with_capacity(strings.len());
    for (auto& value : strings) result.push(PathBuf::from(rstd::move(value)));
    return Ok(rstd::move(result));
}

auto flag_value(const Matches& matches, const ArgKey<bool>& key) -> Result<bool, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    return Ok(value.is_some() && **value);
}

auto optional_path(const Matches& matches, const ArgKey<String>& key)
    -> Result<Option<PathBuf>, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    if (value.is_none()) return Ok(None());
    return Ok(Some(PathBuf::from((**value).clone())));
}

auto optional_string(const Matches& matches, const ArgKey<String>& key)
    -> Result<Option<String>, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    if (value.is_none()) return Ok(None());
    return Ok(Some((**value).clone()));
}

auto optional_profile(const Matches& matches, const ArgKey<lito::manifest::BuildProfileName>& key)
    -> Result<Option<lito::manifest::BuildProfileName>, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    if (value.is_none()) return Ok(None());
    return Ok(Some<lito::manifest::BuildProfileName>((**value).clone()));
}

auto optional_jobs(const Matches& matches, const ArgKey<usize>& key)
    -> Result<Option<usize>, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    if (value.is_none()) return Ok(None());
    return Ok(Some<usize>(**value));
}

auto required_string(const Matches& matches, const ArgKey<String>& key, ref<str> name)
    -> Result<String, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    if (value.is_none()) return Err(CliDecodeError::MissingValue(String::make(name)));
    return Ok((**value).clone());
}

struct PackageProfileValues {
    Vec<String>                              packages;
    Option<lito::manifest::BuildProfileName> profile;
    lito::package::FeatureSelection          features;
};

auto decode_features(const Matches& matches, const PackageProfileArgs& args)
    -> Result<lito::package::FeatureSelection, CliDecodeError> {
    auto declared = rstd_try(string_values(matches, args.features));
    auto enabled = Vec<String>::make();
    for (const auto& value : declared) {
        auto begin = usize {};
        for (usize cursor {}; cursor <= value.len(); ++cursor) {
            if (cursor != value.len() && value.as_str().as_bytes()[cursor] != u8(',')) continue;
            auto item = value.as_str().get(begin, cursor);
            if (item.is_none() || item->is_empty()) {
                return Err(CliDecodeError::InvalidUsage(
                    String::make("--features contains an empty feature name"_str)));
            }
            auto duplicate = false;
            for (const auto& existing : enabled) {
                if (existing.as_str() == *item) {
                    duplicate = true;
                    break;
                }
            }
            if (! duplicate) enabled.push(String::make(*item));
            begin = cursor + usize(1);
        }
    }
    return Ok(lito::package::FeatureSelection {
        .enabled = rstd::move(enabled),
        .default_features = ! rstd_try(flag_value(matches, args.no_default_features)),
    });
}

auto decode_package_profile(const Matches& matches, const PackageProfileArgs& args)
    -> Result<PackageProfileValues, CliDecodeError> {
    return Ok(PackageProfileValues {
        .packages = rstd_try(string_values(matches, args.package)),
        .profile  = rstd_try(optional_profile(matches, args.profile)),
        .features = rstd_try(decode_features(matches, args)),
    });
}

struct SourceAcquisitionValues {
    bool         locked {};
    bool         offline {};
    bool         frozen {};
    Vec<PathBuf> fetch_seeds;
};

auto decode_source_acquisition(const Matches& matches, const SourceAcquisitionArgs& args)
    -> Result<SourceAcquisitionValues, CliDecodeError> {
    return Ok(SourceAcquisitionValues {
        .locked      = rstd_try(flag_value(matches, args.locked)),
        .offline     = rstd_try(flag_value(matches, args.offline)),
        .frozen      = rstd_try(flag_value(matches, args.frozen)),
        .fetch_seeds = rstd_try(path_values(matches, args.fetch_seed)),
    });
}

struct BuildExecutionValues {
    bool            verbose {};
    Option<PathBuf> timing_file;
    bool            no_timing {};
    Option<usize>   jobs;
};

auto decode_build_execution(const Matches& matches, const BuildExecutionArgs& args)
    -> Result<BuildExecutionValues, CliDecodeError> {
    return Ok(BuildExecutionValues {
        .verbose     = rstd_try(flag_value(matches, args.verbose)),
        .timing_file = rstd_try(optional_path(matches, args.timing_file)),
        .no_timing   = rstd_try(flag_value(matches, args.no_timing)),
        .jobs        = rstd_try(optional_jobs(matches, args.jobs)),
    });
}

auto BuildSchema::decode(const Matches& matches) const -> Result<BuildOptions, CliDecodeError> {
    auto package   = rstd_try(decode_package_profile(matches, this->package));
    auto source    = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto execution = rstd_try(decode_build_execution(matches, this->execution));
    return Ok(BuildOptions {
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .targets     = rstd_try(string_values(matches, target)),
        .output      = rstd_try(optional_path(matches, output)),
        .locked      = source.locked,
        .offline     = source.offline,
        .frozen      = source.frozen,
        .fetch_seeds = rstd::move(source.fetch_seeds),
        .verbose     = execution.verbose,
        .timing_file = rstd::move(execution.timing_file),
        .no_timing   = execution.no_timing,
        .jobs        = rstd::move(execution.jobs),
        .features    = rstd::move(package.features),
    });
}

auto InstallSchema::decode(const Matches& matches) const -> Result<InstallOptions, CliDecodeError> {
    auto package     = rstd_try(decode_package_profile(matches, this->package));
    auto source      = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto execution   = rstd_try(decode_build_execution(matches, this->execution));
    auto root        = rstd_try(optional_path(matches, this->root));
    auto prefix      = rstd_try(optional_path(matches, this->prefix));
    auto destination = prefix.is_some()
                           ? InstallDestinationRequirement::Prefix(rstd::move(prefix).unwrap())
                           : InstallDestinationRequirement::Managed(rstd::move(root));
    return Ok(InstallOptions {
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .binaries    = rstd_try(string_values(matches, binary)),
        .destination = rstd::move(destination),
        .force       = rstd_try(flag_value(matches, force)),
        .locked      = source.locked,
        .offline     = source.offline,
        .frozen      = source.frozen,
        .fetch_seeds = rstd::move(source.fetch_seeds),
        .verbose     = execution.verbose,
        .timing_file = rstd::move(execution.timing_file),
        .no_timing   = execution.no_timing,
        .jobs        = rstd::move(execution.jobs),
        .features    = rstd::move(package.features),
    });
}

auto TestSchema::decode(const Matches& matches) const -> Result<TestOptions, CliDecodeError> {
    auto package   = rstd_try(decode_package_profile(matches, this->package));
    auto source    = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto execution = rstd_try(decode_build_execution(matches, this->execution));
    return Ok(TestOptions {
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .targets     = rstd_try(string_values(matches, target)),
        .output      = rstd_try(optional_path(matches, output)),
        .arguments   = rstd_try(string_values(matches, arguments)),
        .locked      = source.locked,
        .offline     = source.offline,
        .frozen      = source.frozen,
        .fetch_seeds = rstd::move(source.fetch_seeds),
        .no_run      = rstd_try(flag_value(matches, no_run)),
        .verbose     = execution.verbose,
        .timing_file = rstd::move(execution.timing_file),
        .no_timing   = execution.no_timing,
        .jobs        = rstd::move(execution.jobs),
        .features    = rstd::move(package.features),
    });
}

auto BenchSchema::decode(const Matches& matches) const -> Result<BenchOptions, CliDecodeError> {
    auto package   = rstd_try(decode_package_profile(matches, this->package));
    auto source    = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto execution = rstd_try(decode_build_execution(matches, this->execution));
    return Ok(BenchOptions {
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .targets     = rstd_try(string_values(matches, target)),
        .output      = rstd_try(optional_path(matches, output)),
        .arguments   = rstd_try(string_values(matches, arguments)),
        .locked      = source.locked,
        .offline     = source.offline,
        .frozen      = source.frozen,
        .fetch_seeds = rstd::move(source.fetch_seeds),
        .no_run      = rstd_try(flag_value(matches, no_run)),
        .verbose     = execution.verbose,
        .timing_file = rstd::move(execution.timing_file),
        .no_timing   = execution.no_timing,
        .jobs        = rstd::move(execution.jobs),
        .features    = rstd::move(package.features),
    });
}

auto ScanSchema::decode(const Matches& matches) const -> Result<ScanOptions, CliDecodeError> {
    auto package       = rstd_try(decode_package_profile(matches, this->package));
    auto source_values = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto input         = rstd_try(required_string(matches, this->source, "source"_str));
    auto format_value  = rstd_try(optional_value(matches, format));
    return Ok(ScanOptions {
        .source      = PathBuf::from(rstd::move(input)),
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .targets     = rstd_try(string_values(matches, target)),
        .format      = format_value.is_some() ? **format_value : ScanOutputFormat::Lito,
        .locked      = source_values.locked,
        .offline     = source_values.offline,
        .frozen      = source_values.frozen,
        .fetch_seeds = rstd::move(source_values.fetch_seeds),
        .features    = rstd::move(package.features),
    });
}

auto DocSchema::decode(const Matches& matches) const -> Result<DocOptions, CliDecodeError> {
    auto package   = rstd_try(decode_package_profile(matches, this->package));
    auto source    = rstd_try(decode_source_acquisition(matches, source_acquisition));
    auto execution = rstd_try(decode_build_execution(matches, this->execution));
    return Ok(DocOptions {
        .packages    = rstd::move(package.packages),
        .profile     = rstd::move(package.profile),
        .targets     = rstd_try(string_values(matches, target)),
        .output      = rstd_try(optional_path(matches, output)),
        .data_output = rstd_try(optional_path(matches, data_output)),
        .frontend    = rstd_try(optional_path(matches, frontend)),
        .data_only   = rstd_try(flag_value(matches, data_only)),
        .locked      = source.locked,
        .offline     = source.offline,
        .frozen      = source.frozen,
        .fetch_seeds = rstd::move(source.fetch_seeds),
        .verbose     = execution.verbose,
        .timing_file = rstd::move(execution.timing_file),
        .no_timing   = execution.no_timing,
        .jobs        = rstd::move(execution.jobs),
        .features    = rstd::move(package.features),
    });
}

auto FormatSchema::decode(const Matches& matches) const -> Result<FormatOptions, CliDecodeError> {
    return Ok(FormatOptions {
        .packages = rstd_try(string_values(matches, package)),
        .check    = rstd_try(flag_value(matches, check)),
    });
}

auto UpdateSchema::decode(const Matches& matches) const -> Result<UpdateOptions, CliDecodeError> {
    return Ok(UpdateOptions {
        .offline     = rstd_try(flag_value(matches, offline)),
        .fetch_seeds = rstd_try(path_values(matches, fetch_seed)),
    });
}

auto LockExportSchema::decode(const Matches& matches) const
    -> Result<LockExportOptions, CliDecodeError> {
    auto format_value = rstd_try(required_string(matches, format, "format"_str));
    auto output_value = rstd_try(required_string(matches, output, "output"_str));
    return Ok(LockExportOptions {
        .format = rstd::move(format_value),
        .output = PathBuf::from(rstd::move(output_value)),
    });
}

auto LockSchema::decode(const Matches& matches) const -> Result<LockCommand, CliDecodeError> {
    auto export_matches = matches.subcommand_matches(export_command.command);
    if (export_matches.is_some()) {
        auto options = rstd_try(export_command.decode(**export_matches));
        return Ok(LockCommand::Export(rstd::move(options)));
    }
    return Err(CliDecodeError::CommandMismatch(String::make("lock"_str)));
}

auto ConfigGetSchema::decode(const Matches& matches) const
    -> Result<ConfigGetOptions, CliDecodeError> {
    auto value = rstd_try(optional_value(matches, key));
    return Ok(ConfigGetOptions {
        .key = value.is_some() ? Some((**value).clone()) : None(),
    });
}

auto ConfigSetSchema::decode(const Matches& matches) const
    -> Result<ConfigSetOptions, CliDecodeError> {
    return Ok(ConfigSetOptions {
        .key   = rstd_try(required_string(matches, key, "key"_str)),
        .value = rstd_try(required_string(matches, value, "value"_str)),
    });
}

auto ConfigUnsetSchema::decode(const Matches& matches) const
    -> Result<ConfigUnsetOptions, CliDecodeError> {
    return Ok(ConfigUnsetOptions {
        .key = rstd_try(required_string(matches, key, "key"_str)),
    });
}

auto ConfigSchema::decode(const Matches& matches) const -> Result<ConfigCommand, CliDecodeError> {
    if (matches.subcommand_matches(path).is_some()) return Ok(ConfigCommand::Path());
    if (auto child = matches.subcommand_matches(get.command); child.is_some()) {
        return Ok(ConfigCommand::Get(rstd_try(get.decode(**child))));
    }
    if (auto child = matches.subcommand_matches(set.command); child.is_some()) {
        return Ok(ConfigCommand::Set(rstd_try(set.decode(**child))));
    }
    if (auto child = matches.subcommand_matches(unset.command); child.is_some()) {
        return Ok(ConfigCommand::Unset(rstd_try(unset.decode(**child))));
    }
    return Err(CliDecodeError::CommandMismatch(String::make("config"_str)));
}

auto decode_command(const CliSchema& schema, const Matches& matches)
    -> Result<CliCommand, CliDecodeError> {
    if (auto child = matches.subcommand_matches(schema.build.command); child.is_some()) {
        auto options = rstd_try(schema.build.decode(**child));
        return Ok(CliCommand::Build(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.install.command); child.is_some()) {
        auto options = rstd_try(schema.install.decode(**child));
        return Ok(CliCommand::Install(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.test.command); child.is_some()) {
        auto options = rstd_try(schema.test.decode(**child));
        return Ok(CliCommand::Test(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.bench.command); child.is_some()) {
        auto options = rstd_try(schema.bench.decode(**child));
        return Ok(CliCommand::Bench(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.doc.command); child.is_some()) {
        auto options = rstd_try(schema.doc.decode(**child));
        return Ok(CliCommand::Doc(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.scan.command); child.is_some()) {
        auto options = rstd_try(schema.scan.decode(**child));
        return Ok(CliCommand::Scan(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.format.command); child.is_some()) {
        auto options = rstd_try(schema.format.decode(**child));
        return Ok(CliCommand::Format(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.update.command); child.is_some()) {
        auto options = rstd_try(schema.update.decode(**child));
        return Ok(CliCommand::Update(rstd::move(options)));
    }
    if (auto child = matches.subcommand_matches(schema.lock.command); child.is_some()) {
        auto command = rstd_try(schema.lock.decode(**child));
        return Ok(CliCommand::Lock(rstd::move(command)));
    }
    if (auto child = matches.subcommand_matches(schema.config.command); child.is_some()) {
        auto command = rstd_try(schema.config.decode(**child));
        return Ok(CliCommand::Config(rstd::move(command)));
    }
    return Err(CliDecodeError::CommandMismatch(String::make("lito"_str)));
}

struct CliInvocation {
    PathBuf                         working_directory;
    bool                            no_config {};
    Vec<String>                     config_overrides;
    lito::config::ToolchainOverride toolchain;
    Option<String>                  toolchain_standard_library;
    CliCommand                      command;
};

auto has_toolchain_override(const lito::config::ToolchainOverride& value,
                            const Option<String>&                  standard_library) -> bool {
    return value.cc.is_some() || value.cxx.is_some() || value.ld.is_some() || value.ar.is_some() ||
           value.strip.is_some() || value.format.is_some() || standard_library.is_some();
}

auto decode_toolchain(const Matches& matches, const ToolchainArgs& args)
    -> Result<lito::config::ToolchainOverride, CliDecodeError> {
    return Ok(lito::config::ToolchainOverride {
        .cc     = rstd_try(optional_path(matches, args.cc)),
        .cxx    = rstd_try(optional_path(matches, args.cxx)),
        .ld     = rstd_try(optional_path(matches, args.ld)),
        .ar     = rstd_try(optional_path(matches, args.ar)),
        .strip  = rstd_try(optional_path(matches, args.strip)),
        .format = rstd_try(optional_path(matches, args.format)),
    });
}

auto decode_invocation(const CliSchema& schema, const Matches& matches)
    -> Result<CliInvocation, CliDecodeError> {
    auto directory = rstd_try(required_string(matches, schema.root.directory, "directory"_str));
    auto no_config = rstd_try(flag_value(matches, schema.root.no_config));
    auto overrides = rstd_try(string_values(matches, schema.root.config));
    auto toolchain = rstd_try(decode_toolchain(matches, schema.root.toolchain));
    auto standard_library = rstd_try(optional_string(matches, schema.root.toolchain.stdlib));
    auto command          = rstd_try(decode_command(schema, matches));
    if (command.is_Config() && (no_config || ! overrides.is_empty() ||
                                has_toolchain_override(toolchain, standard_library))) {
        return Err(CliDecodeError::InvalidUsage(String::make(
            "the config command cannot be combined with --no-config, --config, or --toolchain.*"_str)));
    }
    return Ok(CliInvocation {
        .working_directory          = PathBuf::from(rstd::move(directory)),
        .no_config                  = no_config,
        .config_overrides           = rstd::move(overrides),
        .toolchain                  = rstd::move(toolchain),
        .toolchain_standard_library = rstd::move(standard_library),
        .command                    = rstd::move(command),
    });
}

auto decode_error_text(const CliDecodeError& error) -> String {
    if (error.is_MatchAccess()) {
        return rstd::format("argument access failed: {}", error.as_MatchAccess().error);
    }
    if (error.is_MissingValue()) {
        return rstd::format("required argument '{}' is missing", error.as_MissingValue().argument);
    }
    if (error.is_InvalidUsage()) return error.as_InvalidUsage().message.clone();
    return rstd::format("parsed command '{}' does not match its schema",
                        error.as_CommandMismatch().command);
}

} // namespace lito::cli

export namespace lito::cli
{

auto parse() -> CliOutcome {
    auto schema_result = make_schema();
    if (schema_result.is_err()) {
        return CliOutcome::Exit(rstd::format("lito: invalid command definition: {}\n",
                                             rstd::move(schema_result).unwrap_err()),
                                true,
                                i32(1));
    }
    auto schema = rstd::move(schema_result).unwrap();
    auto argv   = Vec<OsString>::make();
    argv.push(OsString::from("lito"_str));
    auto arguments = rstd::env::args_os();
    (void)arguments.next();
    for (auto argument = arguments.next(); argument.is_some(); argument = arguments.next()) {
        argv.push(rstd::move(argument).unwrap());
    }
    auto parsed = schema.parser.parse_from(rstd::move(argv));
    if (parsed.is_err()) {
        auto error  = rstd::move(parsed).unwrap_err();
        auto report = schema.parser.render_error(error);
        return CliOutcome::Exit(String::make(report.text()),
                                report.target() == OutputTarget::Tag::Stderr,
                                report.exit_code());
    }
    auto outcome = rstd::move(parsed).unwrap();
    if (outcome.is_Display()) {
        auto request = rstd::move(outcome).as_Display().request;
        auto output = String::make(request.text());
        if (request.kind() == DisplayKind::Tag::Version) output.push_ascii(u8('\n'));
        return CliOutcome::Exit(rstd::move(output),
                                request.target() == OutputTarget::Tag::Stderr,
                                request.exit_code());
    }

    auto matches    = rstd::move(outcome).as_Parsed().value;
    auto invocation = decode_invocation(schema, matches);
    if (invocation.is_err()) {
        auto error = rstd::move(invocation).unwrap_err();
        if (error.is_InvalidUsage()) {
            return CliOutcome::Exit(
                rstd::format("lito: {}\n", error.as_InvalidUsage().message), true, i32(2));
        }
        return CliOutcome::Exit(
            rstd::format("lito: invalid parsed command: {}\n", decode_error_text(error)),
            true,
            i32(1));
    }
    auto value = rstd::move(invocation).unwrap();
    return CliOutcome::Parsed(rstd::move(value.working_directory),
                              value.no_config,
                              rstd::move(value.config_overrides),
                              rstd::move(value.toolchain),
                              rstd::move(value.toolchain_standard_library),
                              rstd::move(value.command));
}

} // namespace lito::cli
