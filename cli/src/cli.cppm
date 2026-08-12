module;
#include <rstd/enum.hpp>

export module lito.executable:cli;

import rstd;
import rstd.argparse;
import lito;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::argparse;
using rstd::ffi::OsStr;

namespace lito::cli
{

class BuildProfileParser {
public:
    auto parse(ref<OsStr> value) const -> rstd::Result<BuildProfileName, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        auto profile = parse_build_profile(*text);
        if (profile.is_ok()) return Ok(rstd::move(profile).unwrap());
        return Err(ValueError::Message(rstd::move(profile).unwrap_err().message));
    }
};

class ScanOutputFormatParser {
public:
    auto parse(ref<OsStr> value) const -> rstd::Result<ScanOutputFormat, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        auto format = parse_scan_output_format(*text);
        if (format.is_ok()) return Ok(rstd::move(format).unwrap());
        return Err(ValueError::Message(rstd::move(format).unwrap_err().message));
    }

    auto possible_values() const -> Vec<String> {
        auto values = Vec<String>::with_capacity(usize(2));
        values.push(String::make(scan_output_format_name(ScanOutputFormat::Lito)));
        values.push(String::make(scan_output_format_name(ScanOutputFormat::P1689)));
        return values;
    }
};

struct CliSchema {
    ArgKey<String>           directory;
    ArgKey<bool>             no_config;
    ArgKey<String>           build_package;
    ArgKey<BuildProfileName> build_profile;
    ArgKey<String>           build_target;
    ArgKey<String>           build_output;
    ArgKey<bool>             build_locked;
    ArgKey<bool>             build_verbose;
    ArgKey<String>           build_timing_file;
    ArgKey<bool>             build_no_timing;
    ArgKey<usize>            build_jobs;
    ArgKey<String>           test_package;
    ArgKey<BuildProfileName> test_profile;
    ArgKey<String>           test_target;
    ArgKey<String>           test_output;
    ArgKey<bool>             test_locked;
    ArgKey<bool>             test_no_run;
    ArgKey<bool>             test_verbose;
    ArgKey<String>           test_timing_file;
    ArgKey<bool>             test_no_timing;
    ArgKey<usize>            test_jobs;
    ArgKey<String>           test_arguments;
    ArgKey<String>           bench_package;
    ArgKey<BuildProfileName> bench_profile;
    ArgKey<String>           bench_target;
    ArgKey<String>           bench_output;
    ArgKey<bool>             bench_locked;
    ArgKey<bool>             bench_no_run;
    ArgKey<bool>             bench_verbose;
    ArgKey<String>           bench_timing_file;
    ArgKey<bool>             bench_no_timing;
    ArgKey<usize>            bench_jobs;
    ArgKey<String>           bench_arguments;
    ArgKey<String>           scan_source;
    ArgKey<String>           scan_package;
    ArgKey<BuildProfileName> scan_profile;
    ArgKey<String>           scan_target;
    ArgKey<ScanOutputFormat> scan_format;
    ArgKey<bool>             scan_locked;
    ArgKey<String>           format_package;
    ArgKey<bool>             format_check;
    Parser                   parser;
};

auto package_arg() -> Arg<String> {
    return Arg<String>::value("package"_str, string_parser())
        .short_name(u8('p'))
        .long_name("package"_str)
        .value_name("NAME"_str)
        .help("Select a package"_str)
        .append();
}

auto profile_arg() -> Arg<BuildProfileName> {
    return Arg<BuildProfileName>::value("profile"_str, BuildProfileParser {})
        .long_name("profile"_str)
        .value_name("PROFILE"_str)
        .help("Select the build profile"_str);
}

auto target_arg() -> Arg<String> {
    return Arg<String>::value("target"_str, string_parser())
        .long_name("target"_str)
        .value_name("NAME"_str)
        .help("Select a target"_str)
        .append();
}

auto locked_arg() -> Arg<bool> {
    return Arg<bool>::flag("locked"_str)
        .long_name("locked"_str)
        .help("Require an unchanged lock file"_str);
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

auto make_schema() -> rstd::Result<CliSchema, DefinitionError> {
    auto build = Command::make("build"_str);
    build.about("Build packages"_str);
    auto build_package = build.add_arg(package_arg());
    auto build_profile = build.add_arg(profile_arg());
    auto build_target  = build.add_arg(target_arg());
    auto build_output  = build.add_arg(Arg<String>::value("out"_str, string_parser())
                                           .long_name("out"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Override the build output directory"_str));
    auto build_locked  = build.add_arg(locked_arg());
    auto build_verbose = build.add_arg(
        Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str));
    auto build_timing_file = build.add_arg(timing_file_arg());
    auto build_no_timing   = build.add_arg(no_timing_arg());
    auto build_jobs        = build.add_arg(jobs_arg());

    auto test = Command::make("test"_str);
    test.about("Build and run test packages"_str);
    auto test_package = test.add_arg(package_arg());
    auto test_profile = test.add_arg(profile_arg());
    auto test_target  = test.add_arg(target_arg());
    auto test_output  = test.add_arg(Arg<String>::value("out"_str, string_parser())
                                         .long_name("out"_str)
                                         .value_name("DIRECTORY"_str)
                                         .help("Override the build output directory"_str));
    auto test_locked  = test.add_arg(locked_arg());
    auto test_no_run  = test.add_arg(Arg<bool>::flag("no-run"_str)
                                         .long_name("no-run"_str)
                                         .help("Build tests without running"_str));
    auto test_verbose = test.add_arg(
        Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str));
    auto test_timing_file = test.add_arg(timing_file_arg());
    auto test_no_timing   = test.add_arg(no_timing_arg());
    auto test_jobs        = test.add_arg(jobs_arg());
    auto test_arguments   = test.add_arg(Arg<String>::value("arguments"_str, string_parser())
                                             .value_name("ARGS"_str)
                                             .num_args(NumArgs::any())
                                             .allow_hyphen_values());

    auto bench = Command::make("bench"_str);
    bench.about("Build and run benchmarks"_str);
    auto bench_package = bench.add_arg(package_arg());
    auto bench_profile = bench.add_arg(profile_arg());
    auto bench_target  = bench.add_arg(target_arg());
    auto bench_output  = bench.add_arg(Arg<String>::value("out"_str, string_parser())
                                           .long_name("out"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Override the build output directory"_str));
    auto bench_locked  = bench.add_arg(locked_arg());
    auto bench_no_run  = bench.add_arg(Arg<bool>::flag("no-run"_str)
                                           .long_name("no-run"_str)
                                           .help("Build benchmarks without running"_str));
    auto bench_verbose = bench.add_arg(
        Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str));
    auto bench_timing_file = bench.add_arg(timing_file_arg());
    auto bench_no_timing   = bench.add_arg(no_timing_arg());
    auto bench_jobs        = bench.add_arg(jobs_arg());
    auto bench_arguments   = bench.add_arg(Arg<String>::value("arguments"_str, string_parser())
                                               .value_name("ARGS"_str)
                                               .num_args(NumArgs::any())
                                               .allow_hyphen_values());

    auto scan = Command::make("scan"_str);
    scan.about("Scan one source file"_str);
    auto scan_source  = scan.add_arg(Arg<String>::value("source"_str, string_parser())
                                         .value_name("SOURCE"_str)
                                         .help("Source file to scan"_str)
                                         .required());
    auto scan_package = scan.add_arg(package_arg());
    auto scan_profile = scan.add_arg(profile_arg());
    auto scan_target  = scan.add_arg(target_arg());
    auto scan_format =
        scan.add_arg(Arg<ScanOutputFormat>::value("format"_str, ScanOutputFormatParser {})
                         .long_name("format"_str)
                         .value_name("FORMAT"_str)
                         .help("Select the JSON output format"_str));
    auto scan_locked = scan.add_arg(locked_arg());

    auto format = Command::make("format"_str);
    format.about("Format package sources"_str);
    auto format_package = format.add_arg(package_arg());
    auto format_check   = format.add_arg(Arg<bool>::flag("check"_str)
                                             .long_name("check"_str)
                                             .help("Check formatting without changing files"_str));

    auto update = Command::make("update"_str);
    update.about("Update Git dependencies and the lock file"_str);

    auto root = Command::make("lito"_str);
    root.about("Module-first C++ builder"_str);
    root.require_subcommand();
    auto directory = root.add_arg(Arg<String>::value("directory"_str, string_parser())
                                      .short_name(u8('C'))
                                      .value_name("DIRECTORY"_str)
                                      .help("Change the working directory"_str)
                                      .default_value("."_str));
    auto no_config = root.add_arg(Arg<bool>::flag("no-config"_str)
                                      .long_name("no-config"_str)
                                      .help("Ignore .lito/config.toml"_str));
    root.add_subcommand(rstd::move(build));
    root.add_subcommand(rstd::move(test));
    root.add_subcommand(rstd::move(bench));
    root.add_subcommand(rstd::move(scan));
    root.add_subcommand(rstd::move(format));
    root.add_subcommand(rstd::move(update));
    auto parser = rstd::move(root).build();
    if (parser.is_err()) return Err(rstd::move(parser).unwrap_err());
    return Ok(CliSchema {
        .directory         = directory,
        .no_config         = no_config,
        .build_package     = build_package,
        .build_profile     = build_profile,
        .build_target      = build_target,
        .build_output      = build_output,
        .build_locked      = build_locked,
        .build_verbose     = build_verbose,
        .build_timing_file = build_timing_file,
        .build_no_timing   = build_no_timing,
        .build_jobs        = build_jobs,
        .test_package      = test_package,
        .test_profile      = test_profile,
        .test_target       = test_target,
        .test_output       = test_output,
        .test_locked       = test_locked,
        .test_no_run       = test_no_run,
        .test_verbose      = test_verbose,
        .test_timing_file  = test_timing_file,
        .test_no_timing    = test_no_timing,
        .test_jobs         = test_jobs,
        .test_arguments    = test_arguments,
        .bench_package     = bench_package,
        .bench_profile     = bench_profile,
        .bench_target      = bench_target,
        .bench_output      = bench_output,
        .bench_locked      = bench_locked,
        .bench_no_run      = bench_no_run,
        .bench_verbose     = bench_verbose,
        .bench_timing_file = bench_timing_file,
        .bench_no_timing   = bench_no_timing,
        .bench_jobs        = bench_jobs,
        .bench_arguments   = bench_arguments,
        .scan_source       = scan_source,
        .scan_package      = scan_package,
        .scan_profile      = scan_profile,
        .scan_target       = scan_target,
        .scan_format       = scan_format,
        .scan_locked       = scan_locked,
        .format_package    = format_package,
        .format_check      = format_check,
        .parser            = rstd::move(parser).unwrap(),
    });
}

template<typename T>
auto optional_value(const Matches& matches, const ArgKey<T>& key) -> Option<ref<T>> {
    return matches.get_one(key).unwrap();
}

auto string_values(const Matches& matches, const ArgKey<String>& key) -> Vec<String> {
    auto result = Vec<String>::make();
    auto values = matches.get_many(key).unwrap();
    if (values.is_none()) return result;
    auto iterator = rstd::move(*values);
    for (auto value = iterator.next(); value.is_some(); value = iterator.next()) {
        result.push((**value).clone());
    }
    return result;
}

auto flag_value(const Matches& matches, const ArgKey<bool>& key) -> bool {
    auto value = optional_value(matches, key);
    return value.is_some() && **value;
}

} // namespace lito::cli

export namespace lito::cli
{

struct BuildOptions {
    Vec<String>              packages;
    Option<BuildProfileName> profile;
    Vec<String>              targets;
    Option<PathBuf>          output;
    bool                     locked {};
    bool                     verbose {};
    Option<PathBuf>          timing_file;
    bool                     no_timing {};
    Option<usize>            jobs;
};

struct ScanOptions {
    PathBuf                  source;
    Vec<String>              packages;
    Option<BuildProfileName> profile;
    Vec<String>              targets;
    ScanOutputFormat         format { ScanOutputFormat::Lito };
    bool                     locked {};
};

struct TestOptions {
    Vec<String>              packages;
    Option<BuildProfileName> profile;
    Vec<String>              targets;
    Option<PathBuf>          output;
    Vec<String>              arguments;
    bool                     locked {};
    bool                     no_run {};
    bool                     verbose {};
    Option<PathBuf>          timing_file;
    bool                     no_timing {};
    Option<usize>            jobs;
};

struct BenchOptions {
    Vec<String>              packages;
    Option<BuildProfileName> profile;
    Vec<String>              targets;
    Option<PathBuf>          output;
    Vec<String>              arguments;
    bool                     locked {};
    bool                     no_run {};
    bool                     verbose {};
    Option<PathBuf>          timing_file;
    bool                     no_timing {};
    Option<usize>            jobs;
};

struct FormatOptions {
    Vec<String> packages;
    bool        check {};
};

class CliCommand {
    RSTD_ENUM(CliCommand,
              (Build, (BuildOptions options;)),
              (Test, (TestOptions options;)),
              (Bench, (BenchOptions options;)),
              (Scan, (ScanOptions options;)),
              (Format, (FormatOptions options;)),
              (Update))
};

class CliOutcome {
    RSTD_ENUM(CliOutcome,
              (Parsed, (PathBuf working_directory; bool no_config; CliCommand command;)),
              (Exit, (String output; bool standard_error; i32 exit_code;)))
};

auto parse() -> CliOutcome {
    auto schema_result = make_schema();
    if (schema_result.is_err()) {
        return CliOutcome::Exit(rstd::format("lito: invalid command definition: {}\n",
                                             rstd::move(schema_result).unwrap_err()),
                                true,
                                i32(1));
    }
    auto schema = rstd::move(schema_result).unwrap();
    auto parsed = schema.parser.parse_env();
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
        return CliOutcome::Exit(String::make(request.text()),
                                request.target() == OutputTarget::Tag::Stderr,
                                request.exit_code());
    }

    auto matches           = rstd::move(outcome).as_Parsed().value;
    auto directory_value   = optional_value(matches, schema.directory);
    auto working_directory = PathBuf::from((**directory_value).clone());
    auto no_config         = flag_value(matches, schema.no_config);
    auto subcommand        = matches.subcommand();
    if (subcommand->get<0>() == "build"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.build_profile);
        auto output      = optional_value(*child, schema.build_output);
        auto timing_file = optional_value(*child, schema.build_timing_file);
        auto jobs        = optional_value(*child, schema.build_jobs);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            no_config,
            CliCommand::Build(BuildOptions {
                .packages = string_values(*child, schema.build_package),
                .profile = profile.is_some() ? Some<BuildProfileName>((**profile).clone()) : None(),
                .targets = string_values(*child, schema.build_target),
                .output  = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .locked  = flag_value(*child, schema.build_locked),
                .verbose = flag_value(*child, schema.build_verbose),
                .timing_file =
                    timing_file.is_some() ? Some(PathBuf::from((**timing_file).clone())) : None(),
                .no_timing = flag_value(*child, schema.build_no_timing),
                .jobs      = jobs.is_some() ? Some<usize>(**jobs) : None(),
            }));
    }
    if (subcommand->get<0>() == "scan"_str) {
        auto child   = subcommand->get<1>();
        auto source  = optional_value(*child, schema.scan_source);
        auto profile = optional_value(*child, schema.scan_profile);
        auto format  = optional_value(*child, schema.scan_format);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            no_config,
            CliCommand::Scan(ScanOptions {
                .source   = PathBuf::from((**source).clone()),
                .packages = string_values(*child, schema.scan_package),
                .profile = profile.is_some() ? Some<BuildProfileName>((**profile).clone()) : None(),
                .targets = string_values(*child, schema.scan_target),
                .format  = format.is_some() ? **format : ScanOutputFormat::Lito,
                .locked  = flag_value(*child, schema.scan_locked),
            }));
    }
    if (subcommand->get<0>() == "test"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.test_profile);
        auto output      = optional_value(*child, schema.test_output);
        auto timing_file = optional_value(*child, schema.test_timing_file);
        auto jobs        = optional_value(*child, schema.test_jobs);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            no_config,
            CliCommand::Test(TestOptions {
                .packages = string_values(*child, schema.test_package),
                .profile = profile.is_some() ? Some<BuildProfileName>((**profile).clone()) : None(),
                .targets = string_values(*child, schema.test_target),
                .output  = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .arguments = string_values(*child, schema.test_arguments),
                .locked    = flag_value(*child, schema.test_locked),
                .no_run    = flag_value(*child, schema.test_no_run),
                .verbose   = flag_value(*child, schema.test_verbose),
                .timing_file =
                    timing_file.is_some() ? Some(PathBuf::from((**timing_file).clone())) : None(),
                .no_timing = flag_value(*child, schema.test_no_timing),
                .jobs      = jobs.is_some() ? Some<usize>(**jobs) : None(),
            }));
    }
    if (subcommand->get<0>() == "bench"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.bench_profile);
        auto output      = optional_value(*child, schema.bench_output);
        auto timing_file = optional_value(*child, schema.bench_timing_file);
        auto jobs        = optional_value(*child, schema.bench_jobs);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            no_config,
            CliCommand::Bench(BenchOptions {
                .packages = string_values(*child, schema.bench_package),
                .profile = profile.is_some() ? Some<BuildProfileName>((**profile).clone()) : None(),
                .targets = string_values(*child, schema.bench_target),
                .output  = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .arguments = string_values(*child, schema.bench_arguments),
                .locked    = flag_value(*child, schema.bench_locked),
                .no_run    = flag_value(*child, schema.bench_no_run),
                .verbose   = flag_value(*child, schema.bench_verbose),
                .timing_file =
                    timing_file.is_some() ? Some(PathBuf::from((**timing_file).clone())) : None(),
                .no_timing = flag_value(*child, schema.bench_no_timing),
                .jobs      = jobs.is_some() ? Some<usize>(**jobs) : None(),
            }));
    }
    if (subcommand->get<0>() == "update"_str) {
        return CliOutcome::Parsed(rstd::move(working_directory), no_config, CliCommand::Update());
    }
    auto child = subcommand->get<1>();
    return CliOutcome::Parsed(rstd::move(working_directory),
                              no_config,
                              CliCommand::Format(FormatOptions {
                                  .packages = string_values(*child, schema.format_package),
                                  .check    = flag_value(*child, schema.format_check),
                              }));
}

} // namespace lito::cli
