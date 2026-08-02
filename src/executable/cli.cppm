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
    ArgKey<String>       build_target;
    ArgKey<String>       build_output;
    ArgKey<bool>         build_locked;
    ArgKey<bool>         build_verbose;
    ArgKey<String>       test_package;
    ArgKey<BuildProfile> test_profile;
    ArgKey<String>       test_output;
    ArgKey<bool>         test_locked;
    ArgKey<bool>         test_no_run;
    ArgKey<bool>         test_verbose;
    ArgKey<String>       test_arguments;
    ArgKey<String>       scan_source;
    ArgKey<String>       scan_package;
    ArgKey<BuildProfile> scan_profile;
    ArgKey<String>       scan_target;
    ArgKey<bool>         scan_locked;
    ArgKey<String>       format_package;
    Parser               parser;
};

auto package_arg() -> Arg<String> {
    return Arg<String>::value("package"_str, string_parser())
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

    auto test = Command::make("test"_str);
    test.about("Build and run test packages"_str);
    auto test_package = test.add_arg(package_arg());
    auto test_profile = test.add_arg(profile_arg());
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
    auto test_arguments = test.add_arg(Arg<String>::value("arguments"_str, string_parser())
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
    auto scan_locked  = scan.add_arg(locked_arg());

    auto format = Command::make("format"_str);
    format.about("Format package sources"_str);
    auto format_package = format.add_arg(package_arg());

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
    auto parser = rstd::move(root).build();
    if (parser.is_err()) return Err(rstd::move(parser).unwrap_err());
    return Ok(CliSchema {
        .directory      = directory,
        .build_package  = build_package,
        .build_profile  = build_profile,
        .build_target   = build_target,
        .build_output   = build_output,
        .build_locked   = build_locked,
        .build_verbose  = build_verbose,
        .test_package   = test_package,
        .test_profile   = test_profile,
        .test_output    = test_output,
        .test_locked    = test_locked,
        .test_no_run    = test_no_run,
        .test_verbose   = test_verbose,
        .test_arguments = test_arguments,
        .scan_source    = scan_source,
        .scan_package   = scan_package,
        .scan_profile   = scan_profile,
        .scan_target    = scan_target,
        .scan_locked    = scan_locked,
        .format_package = format_package,
        .parser         = rstd::move(parser).unwrap(),
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
    bool                 locked {};
    bool                 verbose {};
};

struct ScanOptions {
    PathBuf              source;
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Vec<String>          targets;
    bool                 locked {};
};

struct TestOptions {
    Vec<String>          packages;
    Option<BuildProfile> profile;
    Option<PathBuf>      output;
    Vec<String>          arguments;
    bool                 locked {};
    bool                 no_run {};
    bool                 verbose {};
};

struct FormatOptions {
    Vec<String> packages;
};

class CliCommand {
    RSTD_ENUM(CliCommand,
              (Build, (BuildOptions options;)),
              (Test, (TestOptions options;)),
              (Scan, (ScanOptions options;)),
              (Format, (FormatOptions options;)))
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
        auto child   = subcommand->get<1>();
        auto profile = optional_value(*child, schema.build_profile);
        auto output  = optional_value(*child, schema.build_output);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Build(BuildOptions {
                .packages = string_values(*child, schema.build_package),
                .profile  = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .targets  = string_values(*child, schema.build_target),
                .output   = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .locked   = flag_value(*child, schema.build_locked),
                .verbose  = flag_value(*child, schema.build_verbose),
            }));
    }
    if (subcommand->get<0>() == "scan"_str) {
        auto child   = subcommand->get<1>();
        auto source  = optional_value(*child, schema.scan_source);
        auto profile = optional_value(*child, schema.scan_profile);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Scan(ScanOptions {
                .source   = PathBuf::from((**source).clone()),
                .packages = string_values(*child, schema.scan_package),
                .profile  = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .targets  = string_values(*child, schema.scan_target),
                .locked   = flag_value(*child, schema.scan_locked),
            }));
    }
    if (subcommand->get<0>() == "test"_str) {
        auto child   = subcommand->get<1>();
        auto profile = optional_value(*child, schema.test_profile);
        auto output  = optional_value(*child, schema.test_output);
        return CliOutcome::Parsed(
            rstd::move(working_directory),
            CliCommand::Test(TestOptions {
                .packages  = string_values(*child, schema.test_package),
                .profile   = profile.is_some() ? Some<BuildProfile>(**profile) : None(),
                .output    = output.is_some() ? Some(PathBuf::from((**output).clone())) : None(),
                .arguments = string_values(*child, schema.test_arguments),
                .locked    = flag_value(*child, schema.test_locked),
                .no_run    = flag_value(*child, schema.test_no_run),
                .verbose   = flag_value(*child, schema.test_verbose),
            }));
    }
    auto child = subcommand->get<1>();
    return CliOutcome::Parsed(rstd::move(working_directory),
                              CliCommand::Format(FormatOptions {
                                  .packages = string_values(*child, schema.format_package),
                              }));
}

} // namespace tenon::cli
