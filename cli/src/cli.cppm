module;
#include <rstd/enum.hpp>

export module tenon.executable:cli;

import rstd;
import rstd.argparse;
import tenon;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::argparse;
using rstd::ffi::OsStr;

namespace tenon::cli
{

class BuildProfileParser {
public:
    auto parse(ref<OsStr> value) const -> rstd::Result<BuildProfile, ValueError> {
        auto text = value.to_str();
        if (text.is_none()) return Err(ValueError::InvalidUtf8());
        auto profile = parse_build_profile(*text);
        if (profile.is_ok()) return Ok(rstd::move(profile).unwrap());
        return Err(ValueError::Message(rstd::move(profile).unwrap_err().message));
    }

    auto possible_values() const -> Vec<String> {
        auto values = Vec<String>::with_capacity(usize(2));
        values.push(String::make("debug"_str));
        values.push(String::make("release"_str));
        return values;
    }
};

struct CliSchema {
    ArgKey<String>       directory;
    ArgKey<String>       build_package;
    ArgKey<BuildProfile> build_profile;
    ArgKey<bool>         build_exceptions;
    ArgKey<String>       build_target;
    ArgKey<String>       build_output;
    ArgKey<bool>         build_locked;
    ArgKey<bool>         build_verbose;
    ArgKey<String>       build_timing_file;
    ArgKey<bool>         build_no_timing;
    ArgKey<String>       test_package;
    ArgKey<BuildProfile> test_profile;
    ArgKey<bool>         test_exceptions;
    ArgKey<String>       test_output;
    ArgKey<bool>         test_locked;
    ArgKey<bool>         test_no_run;
    ArgKey<bool>         test_verbose;
    ArgKey<String>       test_timing_file;
    ArgKey<bool>         test_no_timing;
    ArgKey<String>       test_arguments;
    ArgKey<String>       scan_source;
    ArgKey<String>       scan_package;
    ArgKey<BuildProfile> scan_profile;
    ArgKey<bool>         scan_exceptions;
    ArgKey<String>       scan_target;
    ArgKey<bool>         scan_locked;
    ArgKey<String>       format_package;
    ArgKey<String>       doc_package;
    ArgKey<BuildProfile> doc_profile;
    ArgKey<bool>         doc_exceptions;
    ArgKey<String>       doc_target;
    ArgKey<String>       doc_output;
    ArgKey<String>       doc_data_output;
    ArgKey<String>       doc_frontend;
    ArgKey<String>       doc_from_data;
    ArgKey<bool>         doc_data_only;
    ArgKey<bool>         doc_locked;
    Parser               parser;
};

auto package_arg() -> Arg<String> {
    return Arg<String>::value("package"_str, string_parser())
        .short_name(u8('p'))
        .long_name("package"_str)
        .value_name("NAME"_str)
        .help("Select a package"_str)
        .append();
}

auto profile_arg() -> Arg<BuildProfile> {
    return Arg<BuildProfile>::value("profile"_str, BuildProfileParser {})
        .long_name("profile"_str)
        .value_name("PROFILE"_str)
        .help("Select the build profile"_str);
}

auto exceptions_arg() -> Arg<bool> {
    return Arg<bool>::flag("exceptions"_str)
        .long_name("exceptions"_str)
        .help("Enable C++ exceptions for the build graph"_str);
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

auto make_schema() -> rstd::Result<CliSchema, DefinitionError> {
    auto build = Command::make("build"_str);
    build.about("Build packages"_str);
    auto build_package    = build.add_arg(package_arg());
    auto build_profile    = build.add_arg(profile_arg());
    auto build_exceptions = build.add_arg(exceptions_arg());
    auto build_target     = build.add_arg(target_arg());
    auto build_output     = build.add_arg(Arg<String>::value("out"_str, string_parser())
                                              .long_name("out"_str)
                                              .value_name("DIRECTORY"_str)
                                              .help("Override the build output directory"_str));
    auto build_locked     = build.add_arg(locked_arg());
    auto build_verbose    = build.add_arg(
        Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str));
    auto build_timing_file = build.add_arg(timing_file_arg());
    auto build_no_timing   = build.add_arg(no_timing_arg());

    auto test = Command::make("test"_str);
    test.about("Build and run test packages"_str);
    auto test_package    = test.add_arg(package_arg());
    auto test_profile    = test.add_arg(profile_arg());
    auto test_exceptions = test.add_arg(exceptions_arg());
    auto test_output     = test.add_arg(Arg<String>::value("out"_str, string_parser())
                                            .long_name("out"_str)
                                            .value_name("DIRECTORY"_str)
                                            .help("Override the build output directory"_str));
    auto test_locked     = test.add_arg(locked_arg());
    auto test_no_run     = test.add_arg(Arg<bool>::flag("no-run"_str)
                                            .long_name("no-run"_str)
                                            .help("Build tests without running"_str));
    auto test_verbose    = test.add_arg(
        Arg<bool>::flag("verbose"_str).long_name("verbose"_str).help("Show build events"_str));
    auto test_timing_file = test.add_arg(timing_file_arg());
    auto test_no_timing   = test.add_arg(no_timing_arg());
    auto test_arguments   = test.add_arg(Arg<String>::value("arguments"_str, string_parser())
                                             .value_name("ARGS"_str)
                                             .num_args(NumArgs::any())
                                             .allow_hyphen_values());

    auto scan = Command::make("scan"_str);
    scan.about("Scan one source file"_str);
    auto scan_source     = scan.add_arg(Arg<String>::value("source"_str, string_parser())
                                            .value_name("SOURCE"_str)
                                            .help("Source file to scan"_str)
                                            .required());
    auto scan_package    = scan.add_arg(package_arg());
    auto scan_profile    = scan.add_arg(profile_arg());
    auto scan_exceptions = scan.add_arg(exceptions_arg());
    auto scan_target     = scan.add_arg(target_arg());
    auto scan_locked     = scan.add_arg(locked_arg());

    auto format = Command::make("format"_str);
    format.about("Format package sources"_str);
    auto format_package = format.add_arg(package_arg());

    auto doc = Command::make("doc"_str);
    doc.about("Generate package documentation"_str);
    auto doc_package    = doc.add_arg(package_arg());
    auto doc_profile    = doc.add_arg(profile_arg());
    auto doc_exceptions = doc.add_arg(exceptions_arg());
    auto doc_target     = doc.add_arg(target_arg());
    auto doc_output     = doc.add_arg(Arg<String>::value("out"_str, string_parser())
                                          .long_name("out"_str)
                                          .value_name("DIRECTORY"_str)
                                          .help("Override the documentation output directory"_str));
    auto doc_data_output = doc.add_arg(Arg<String>::value("data-out"_str, string_parser())
                                           .long_name("data-out"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Override the documentation data directory"_str));
    auto doc_frontend    = doc.add_arg(Arg<String>::value("frontend"_str, string_parser())
                                           .long_name("frontend"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Render with a frontend bundle directory"_str));
    auto doc_from_data   = doc.add_arg(Arg<String>::value("from-data"_str, string_parser())
                                           .long_name("from-data"_str)
                                           .value_name("DIRECTORY"_str)
                                           .help("Render an existing documentation dataset"_str));
    auto doc_data_only   = doc.add_arg(Arg<bool>::flag("data-only"_str)
                                           .long_name("data-only"_str)
                                           .help("Publish documentation data without a site"_str));
    auto doc_locked      = doc.add_arg(locked_arg());

    auto root = Command::make("tenon"_str);
    root.about("Module-first C++ builder"_str);
    root.require_subcommand();
    auto directory = root.add_arg(Arg<String>::value("directory"_str, string_parser())
                                      .short_name(u8('C'))
                                      .value_name("DIRECTORY"_str)
                                      .help("Change the working directory"_str)
                                      .default_value("."_str));
    root.add_subcommand(rstd::move(build));
    root.add_subcommand(rstd::move(test));
    root.add_subcommand(rstd::move(scan));
    root.add_subcommand(rstd::move(format));
    root.add_subcommand(rstd::move(doc));
    auto parser = rstd::move(root).build();
    if (parser.is_err()) return Err(rstd::move(parser).unwrap_err());
    return Ok(CliSchema {
        .directory         = directory,
        .build_package     = build_package,
        .build_profile     = build_profile,
        .build_exceptions  = build_exceptions,
        .build_target      = build_target,
        .build_output      = build_output,
        .build_locked      = build_locked,
        .build_verbose     = build_verbose,
        .build_timing_file = build_timing_file,
        .build_no_timing   = build_no_timing,
        .test_package      = test_package,
        .test_profile      = test_profile,
        .test_exceptions   = test_exceptions,
        .test_output       = test_output,
        .test_locked       = test_locked,
        .test_no_run       = test_no_run,
        .test_verbose      = test_verbose,
        .test_timing_file  = test_timing_file,
        .test_no_timing    = test_no_timing,
        .test_arguments    = test_arguments,
        .scan_source       = scan_source,
        .scan_package      = scan_package,
        .scan_profile      = scan_profile,
        .scan_exceptions   = scan_exceptions,
        .scan_target       = scan_target,
        .scan_locked       = scan_locked,
        .format_package    = format_package,
        .doc_package       = doc_package,
        .doc_profile       = doc_profile,
        .doc_exceptions    = doc_exceptions,
        .doc_target        = doc_target,
        .doc_output        = doc_output,
        .doc_data_output   = doc_data_output,
        .doc_frontend      = doc_frontend,
        .doc_from_data     = doc_from_data,
        .doc_data_only     = doc_data_only,
        .doc_locked        = doc_locked,
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

} // namespace tenon::cli

export namespace tenon::cli
{

struct BuildOptions {
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Vec<String>          targets;
    Option<PathBuf>      output;
    bool                 exceptions {};
    bool                 locked {};
    bool                 verbose {};
    Option<PathBuf>      timing_file;
    bool                 no_timing {};
};

struct ScanOptions {
    PathBuf              source;
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Vec<String>          targets;
    bool                 exceptions {};
    bool                 locked {};
};

struct TestOptions {
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Option<PathBuf>      output;
    Vec<String>          arguments;
    bool                 exceptions {};
    bool                 locked {};
    bool                 no_run {};
    bool                 verbose {};
    Option<PathBuf>      timing_file;
    bool                 no_timing {};
};

struct FormatOptions {
    Vec<String> packages;
};

struct DocOptions {
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Vec<String>          targets;
    Option<PathBuf>      output;
    Option<PathBuf>      data_output;
    Option<PathBuf>      frontend;
    Option<PathBuf>      from_data;
    bool                 exceptions {};
    bool                 data_only {};
    bool                 locked {};
};

class CliCommand {
    RSTD_ENUM(CliCommand,
              (Build, (BuildOptions options;)),
              (Test, (TestOptions options;)),
              (Scan, (ScanOptions options;)),
              (Format, (FormatOptions options;)),
              (Doc, (DocOptions options;)))
};

class CliOutcome {
    RSTD_ENUM(CliOutcome,
              (Parsed, (PathBuf working_directory; CliCommand command;)),
              (Exit, (String output; bool standard_error; i32 exit_code;)))
};

auto parse() -> CliOutcome {
    auto schema_result = make_schema();
    if (schema_result.is_err()) {
        return CliOutcome::Exit(rstd::format("tenon: invalid command definition: {}\n",
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
    auto subcommand        = matches.subcommand();
    if (subcommand->get<0>() == "build"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.build_profile);
        auto output      = optional_value(*child, schema.build_output);
        auto timing_file = optional_value(*child, schema.build_timing_file);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Build(BuildOptions {
                .packages   = string_values(*child, schema.build_package),
                .profile    = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .targets    = string_values(*child, schema.build_target),
                .output     = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .exceptions = flag_value(*child, schema.build_exceptions),
                .locked     = flag_value(*child, schema.build_locked),
                .verbose    = flag_value(*child, schema.build_verbose),
                .timing_file =
                    timing_file.is_some() ? Some(PathBuf::from((**timing_file).clone())) : None(),
                .no_timing = flag_value(*child, schema.build_no_timing),
            }));
    }
    if (subcommand->get<0>() == "scan"_str) {
        auto child   = subcommand->get<1>();
        auto source  = optional_value(*child, schema.scan_source);
        auto profile = optional_value(*child, schema.scan_profile);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Scan(ScanOptions {
                .source     = PathBuf::from((**source).clone()),
                .packages   = string_values(*child, schema.scan_package),
                .profile    = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .targets    = string_values(*child, schema.scan_target),
                .exceptions = flag_value(*child, schema.scan_exceptions),
                .locked     = flag_value(*child, schema.scan_locked),
            }));
    }
    if (subcommand->get<0>() == "test"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.test_profile);
        auto output      = optional_value(*child, schema.test_output);
        auto timing_file = optional_value(*child, schema.test_timing_file);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Test(TestOptions {
                .packages   = string_values(*child, schema.test_package),
                .profile    = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .output     = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .arguments  = string_values(*child, schema.test_arguments),
                .exceptions = flag_value(*child, schema.test_exceptions),
                .locked     = flag_value(*child, schema.test_locked),
                .no_run     = flag_value(*child, schema.test_no_run),
                .verbose    = flag_value(*child, schema.test_verbose),
                .timing_file =
                    timing_file.is_some() ? Some(PathBuf::from((**timing_file).clone())) : None(),
                .no_timing = flag_value(*child, schema.test_no_timing),
            }));
    }
    if (subcommand->get<0>() == "doc"_str) {
        auto child       = subcommand->get<1>();
        auto profile     = optional_value(*child, schema.doc_profile);
        auto output      = optional_value(*child, schema.doc_output);
        auto data_output = optional_value(*child, schema.doc_data_output);
        auto frontend    = optional_value(*child, schema.doc_frontend);
        auto from_data   = optional_value(*child, schema.doc_from_data);
        auto packages    = string_values(*child, schema.doc_package);
        auto targets     = string_values(*child, schema.doc_target);
        auto data_only   = flag_value(*child, schema.doc_data_only);
        auto locked      = flag_value(*child, schema.doc_locked);
        auto exceptions  = flag_value(*child, schema.doc_exceptions);
        if (from_data.is_some() &&
            (! packages.is_empty() || profile.is_some() || ! targets.is_empty() ||
             data_output.is_some() || data_only || locked || exceptions)) {
            return CliOutcome::Exit(
                String::make("tenon: --from-data cannot be combined with --package, --profile, "
                             "--exceptions, --target, --data-out, --data-only, or --locked\n"_str),
                true,
                i32(2));
        }
        if (data_only && frontend.is_some()) {
            return CliOutcome::Exit(
                String::make("tenon: --frontend cannot be used with --data-only\n"_str),
                true,
                i32(2));
        }
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Doc(DocOptions {
                .packages = rstd::move(packages),
                .profile  = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .targets  = rstd::move(targets),
                .output   = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .data_output =
                    data_output.is_some() ? Some(PathBuf::from((**data_output).clone())) : None(),
                .frontend = frontend.is_some() ? Some(PathBuf::from((**frontend).clone())) : None(),
                .from_data =
                    from_data.is_some() ? Some(PathBuf::from((**from_data).clone())) : None(),
                .exceptions = exceptions,
                .data_only  = data_only,
                .locked     = locked,
            }));
    }
    auto child = subcommand->get<1>();
    return CliOutcome::Parsed(rstd::move(working_directory),
                              CliCommand::Format(FormatOptions {
                                  .packages = string_values(*child, schema.format_package),
                              }));
}

} // namespace tenon::cli
