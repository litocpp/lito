module;
#include <rstd/macro.hpp>

module lito.driver:build.generated.execution;

import :build.generated;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::system;

namespace lito
{

auto ToolActionSession::publish_actions(BuildActionGraph&                graph,
                                        const ExecutionDomainId&         domain,
                                        cpp::GeneratedSourceAvailability availability)
    -> BuildScriptResult<Vec<PublishedGeneratedAction>> {
    auto selected = Vec<u8>::with_capacity(actions_.len());
    auto required = Vec<u8>::with_capacity(actions_.len());
    for (usize index {}; index < actions_.len(); ++index) {
        auto included = action_availability(index) == availability ? u8(1) : u8 {};
        selected.emplace_back(included);
        required.emplace_back(included);
    }
    for (usize cursor = actions_.len(); cursor > usize {}; --cursor) {
        auto index = cursor - usize(1);
        if (required[index] == u8 {}) continue;
        for (const auto& input : actions_[index].inputs) {
            if (input.producer.is_some()) required[*input.producer] = u8(1);
        }
    }
    auto outputs = Vec<Vec<BuildArtifactId>>::with_capacity(actions_.len());
    for (usize index {}; index < actions_.len(); ++index) {
        if (required[index] == u8 {}) {
            outputs.emplace_back();
            continue;
        }
        const auto& action    = actions_[index];
        auto        generated = layout_->generated_package_directory(action.package.as_str());
        if (generated.is_err()) {
            return Err(BuildScriptError::Message(rstd::format("{}", generated.unwrap_err())));
        }
        auto action_outputs = Vec<BuildArtifactId>::with_capacity(action.outputs.len());
        for (const auto& output : action.outputs) {
            auto artifact = graph.add_artifact(BuildArtifactSpec {
                .identity =
                    rstd::format("generated:{}:{}", action.identity.as_str(), output.as_path()),
                .domain = domain.clone(),
                .kind   = BuildArtifactKind::Generated,
                .path   = Some(generated->join(output.as_path())),
            });
            if (artifact.is_err()) {
                return Err(BuildScriptError::Message(rstd::format("{}", artifact.unwrap_err())));
            }
            action_outputs.emplace_back(*artifact);
        }
        outputs.push(rstd::move(action_outputs));
    }

    auto action_ids = Vec<PublishedGeneratedAction>::make();
    for (usize index {}; index < actions_.len(); ++index) {
        if (selected[index] == u8 {}) continue;
        const auto& action      = actions_[index];
        auto        inputs      = Vec<BuildArtifactId>::make();
        const auto  append_tool = [&](const ResolvedActionTool& tool) -> BuildScriptResult<empty> {
            if (tool.artifact.is_some()) {
                inputs.emplace_back(*tool.artifact);
                return Ok(empty {});
            }
            auto artifact = graph.add_artifact(BuildArtifactSpec {
                .identity        = rstd::format("tool:{}:{}:{}",
                                                tool.identity.as_str(),
                                                tool.digest.as_str(),
                                                tool.executable.as_path()),
                .domain          = domain.clone(),
                .kind            = BuildArtifactKind::Executable,
                .path            = Some(tool.executable.clone()),
                .initially_ready = true,
            });
            if (artifact.is_err()) {
                return Err(BuildScriptError::Message(rstd::format("{}", artifact.unwrap_err())));
            }
            inputs.emplace_back(*artifact);
            return Ok(empty {});
        };
        if (action.tool.is_some()) rstd_try(append_tool(*action.tool));
        for (const auto& tool : action.tools) rstd_try(append_tool(tool));
        for (const auto& root : action.input_roots) {
            auto artifact = graph.add_artifact(BuildArtifactSpec {
                .identity = rstd::format(
                    "input-root:{}:{}", root.source->identity.as_str(), root.path.as_path()),
                .domain          = domain.clone(),
                .kind            = BuildArtifactKind::External,
                .path            = Some(root.path.clone()),
                .initially_ready = true,
            });
            if (artifact.is_err()) {
                return Err(BuildScriptError::Message(rstd::format("{}", artifact.unwrap_err())));
            }
            inputs.emplace_back(*artifact);
        }
        for (const auto& input : action.inputs) {
            if (input.producer.is_some()) {
                if (*input.producer >= outputs.len()) {
                    return Err(
                        BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                            "generated action input refers to an unknown producer"_Str)));
                }
                auto producer_root = layout_->generated_package_directory(
                    actions_[*input.producer].package.as_str());
                if (producer_root.is_err()) {
                    return Err(
                        BuildScriptError::Message(rstd::format("{}", producer_root.unwrap_err())));
                }
                auto matched = Option<BuildArtifactId> {};
                for (usize output {}; output < actions_[*input.producer].outputs.len(); ++output) {
                    if (producer_root->join(actions_[*input.producer].outputs[output].as_path())
                            .as_path() == input.path.as_path()) {
                        matched = Some(outputs[*input.producer][output]);
                        break;
                    }
                }
                if (matched.is_none()) {
                    return Err(
                        BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                            "generated action input does not match its producer output"_Str)));
                }
                inputs.emplace_back(*matched);
                continue;
            }
            auto artifact = graph.add_artifact(BuildArtifactSpec {
                .identity =
                    rstd::format("input:{}:{}", input.digest.as_str(), input.path.as_path()),
                .domain          = domain.clone(),
                .kind            = BuildArtifactKind::External,
                .path            = Some(input.path.clone()),
                .initially_ready = true,
            });
            if (artifact.is_err()) {
                return Err(BuildScriptError::Message(rstd::format("{}", artifact.unwrap_err())));
            }
            inputs.emplace_back(*artifact);
        }
        auto kind       = action.kind == RegisteredActionKind::Process ? BuildActionKind::RunTool
                          : action.kind == RegisteredActionKind::Write ? BuildActionKind::Write
                          : action.kind == RegisteredActionKind::Copy  ? BuildActionKind::Copy
                                                                       : BuildActionKind::Transform;
        auto registered = graph.add_action(BuildActionSpec {
            .identity = action.identity.clone(),
            .domain   = domain.clone(),
            .kind     = kind,
            .inputs   = rstd::move(inputs),
            .outputs  = outputs[index].clone(),
        });
        if (registered.is_err()) {
            return Err(BuildScriptError::Message(rstd::format("{}", registered.unwrap_err())));
        }
        action_ids.push(PublishedGeneratedAction { .action = index, .id = *registered });
    }
    auto valid = graph.validate();
    if (valid.is_err()) {
        return Err(BuildScriptError::Message(rstd::format("{}", valid.unwrap_err())));
    }
    return Ok(rstd::move(action_ids));
}

auto ToolActionSession::execute(BuildActionGraph&                graph,
                                const ExecutionDomainId&         domain,
                                cpp::GeneratedSourceAvailability availability,
                                usize                            jobs) -> BuildScriptResult<empty> {
    if (jobs == usize {}) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
            "generated action jobs must be greater than zero"_Str)));
    }
    if (actions_.is_empty()) return Ok(empty {});
    auto published = rstd_try(publish_actions(graph, domain, availability));
    if (published.is_empty()) return Ok(empty {});
    auto worker_count = jobs < published.len() ? jobs : published.len();
    auto pool         = rstd::thread::ThreadPoolBuilder::make()
                            .worker_count(worker_count)
                            .thread_name("lito-generate"_Str)
                            .build();
    if (pool.is_err()) {
        return Err(BuildScriptError::Io("create generated action worker pool"_Str,
                                        PathBuf::from(script_.as_path()),
                                        rstd::move(pool).unwrap_err_unchecked()));
    }
    auto workers  = rstd::move(pool).unwrap_unchecked();
    auto task_set = rstd::thread::BlockingTaskSet<GeneratedActionWorkerResult>::make(
        workers.handle(), worker_count);
    if (task_set.is_err()) {
        rstd::move(workers).join();
        return Err(BuildScriptError::Io("create generated action task set"_Str,
                                        PathBuf::from(script_.as_path()),
                                        rstd::move(task_set).unwrap_err_unchecked()));
    }
    auto tasks     = rstd::move(task_set).unwrap_unchecked();
    auto completed = usize {};
    auto in_flight = usize {};
    while (completed < published.len()) {
        while (in_flight < worker_count) {
            auto selected = Option<usize> {};
            for (usize index {}; index < published.len(); ++index) {
                if ((**graph.action(published[index].id)).state == BuildActionState::Ready) {
                    selected = Some(index);
                    break;
                }
            }
            if (selected.is_none()) break;
            auto index  = *selected;
            auto marked = graph.mark_running(published[index].id);
            if (marked.is_err()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return Err(BuildScriptError::Message(rstd::format("{}", marked.unwrap_err())));
            }
            auto session      = this;
            auto action_index = published[index].action;
            auto submitted =
                tasks.try_submit([session, index, action_index]() -> GeneratedActionWorkerResult {
                    return GeneratedActionWorkerResult {
                        .action  = index,
                        .outcome = session->execute_action(session->actions_[action_index]),
                    };
                });
            if (submitted.is_err()) {
                tasks.cancel_pending();
                tasks.close();
                rstd::move(workers).join();
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "cannot submit generated action to worker pool"_Str)));
            }
            ++in_flight;
        }
        if (in_flight == usize {}) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "generated action graph has no ready action"_Str)));
        }
        auto received = tasks.recv();
        if (received.is_none()) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "generated action worker pool closed before completion"_Str)));
        }
        auto task = rstd::move(received).unwrap_unchecked();
        if (task.is_cancelled()) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::BuildToolAction(
                BuildToolActionError::InvalidRequest("generated action was cancelled"_Str)));
        }
        auto value = rstd::move(task).into_value();
        if (value.is_none()) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "generated action completed without a result"_Str)));
        }
        auto result = rstd::move(value).unwrap_unchecked();
        --in_flight;
        if (result.action >= published.len()) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "generated action completion does not match a running action"_Str)));
        }
        if (result.outcome.is_err()) {
            static_cast<void>(graph.mark_failed(published[result.action].id));
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(rstd::move(result.outcome).unwrap_err());
        }
        auto marked = graph.mark_succeeded(published[result.action].id);
        if (marked.is_err()) {
            tasks.cancel_pending();
            tasks.close();
            rstd::move(workers).join();
            return Err(BuildScriptError::Message(rstd::format("{}", marked.unwrap_err())));
        }
        ++completed;
    }
    tasks.close();
    rstd::move(workers).join();
    return Ok(empty {});
}

auto ToolActionSession::current_action_dependencies(const RegisteredAction& action) const
    -> BuildScriptResult<Vec<ActionDependency>> {
    auto result = Vec<ActionDependency>::with_capacity(action.inputs.len());
    for (const auto& input : action.inputs) {
        auto canonical = rstd::fs::canonicalize(input.path.as_path());
        if (canonical.is_err()) {
            return Err(BuildScriptError::Io("resolve generated action input"_Str,
                                            PathBuf::from(input.path.as_path()),
                                            rstd::move(canonical).unwrap_err()));
        }
        auto metadata = rstd::fs::symlink_metadata(canonical->as_path());
        if (metadata.is_err() || metadata->is_symlink() || ! metadata->is_file()) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidInput(
                canonical->clone(), "path is not a regular file"_Str)));
        }
        result.push(ActionDependency {
            .path   = rstd::move(canonical).unwrap(),
            .kind   = ActionDependencyKind::File,
            .digest = rstd_try(action_file_digest(input.path.as_path())),
        });
    }
    return Ok(rstd::move(result));
}

auto ToolActionSession::render_process_invocation(const RegisteredAction& action,
                                                  ref<rstd::path::Path>   staging) const
    -> BuildScriptResult<Vec<String>> {
    auto invocation = Vec<String>::make();
    if (action.tool.is_none() || action.tool->executable.is_empty()) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
            "build-tool action executable artifact is not ready"_Str)));
    }
    invocation.push(action.tool->executable.as_path().to_string_lossy());
    auto replaced_output = false;
    for (const auto& original : action.arguments) {
        auto argument = original.clone();
        for (usize output_index {}; output_index < action.outputs.len(); ++output_index) {
            auto name_marker = rstd::format("@OUTPUT_NAME:{}@", output_index + usize(1));
            auto name        = action.outputs[output_index].as_path().file_name();
            if (name.is_none()) {
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "build-tool action output has no file name"_Str)));
            }
            if (replace_all(argument, name_marker.as_str(), name->to_string_lossy().as_str()) !=
                usize {}) {
                replaced_output = true;
            }
            auto marker = rstd::format("@OUTPUT:{}@", output_index + usize(1));
            auto output = PathBuf::from(staging).join(action.outputs[output_index].as_path());
            if (replace_all(argument,
                            marker.as_str(),
                            output.as_path().to_string_lossy().as_str()) != usize {}) {
                replaced_output = true;
            }
        }
        if (argument.as_str().contains("@OUTPUT@"_str)) {
            if (action.outputs.len() != usize(1)) {
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "build-tool action with multiple outputs must use numbered output markers"_Str)));
            }
            auto output = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
            auto count =
                replace_all(argument, "@OUTPUT@"_str, output.as_path().to_string_lossy().as_str());
            if (count != usize(1)) {
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "build-tool action may use '@OUTPUT@' only once"_Str)));
            }
            replaced_output = true;
        }
        if (argument.as_str().contains("@OUTPUT_NAME@"_str)) {
            if (action.outputs.len() != usize(1)) {
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "build-tool action with multiple outputs must use numbered output name markers"_Str)));
            }
            auto name = action.outputs[usize {}].as_path().file_name();
            if (name.is_none()) {
                return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                    "build-tool action output has no file name"_Str)));
            }
            replace_all(argument, "@OUTPUT_NAME@"_str, name->to_string_lossy().as_str());
            replaced_output = true;
        }
        for (usize input_index {}; input_index < action.inputs.len(); ++input_index) {
            auto marker = rstd::format("@INPUT:{}@", input_index + usize(1));
            replace_all(argument,
                        marker.as_str(),
                        action.inputs[input_index].path.as_path().to_string_lossy().as_str());
        }
        for (usize tool_index {}; tool_index < action.tools.len(); ++tool_index) {
            auto marker = rstd::format("@TOOL:{}@", tool_index + usize(1));
            replace_all(argument,
                        marker.as_str(),
                        action.tools[tool_index].executable.as_path().to_string_lossy().as_str());
        }
        for (usize root_index {}; root_index < action.input_roots.len(); ++root_index) {
            auto marker = rstd::format("@INPUT_ROOT:{}@", root_index + usize(1));
            replace_all(argument,
                        marker.as_str(),
                        action.input_roots[root_index].path.as_path().to_string_lossy().as_str());
        }
        if (argument.as_str().contains("@INPUT:"_str) || argument.as_str().contains("@TOOL@"_str) ||
            argument.as_str().contains("@TOOL:"_str) ||
            argument.as_str().contains("@INPUT_ROOT@"_str) ||
            argument.as_str().contains("@INPUT_ROOT:"_str) ||
            argument.as_str().contains("@OUTPUT:"_str) ||
            argument.as_str().contains("@OUTPUT_NAME:"_str)) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
                "build-tool action contains an unresolved input, tool, root, or output marker"_Str)));
        }
        invocation.push(rstd::move(argument));
    }
    if (! replaced_output) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidRequest(
            "build-tool action args must contain '@OUTPUT@'"_Str)));
    }
    return Ok(rstd::move(invocation));
}

auto ToolActionSession::write_action_staging(const RegisteredAction& action,
                                             ref<rstd::path::Path>   staging) const
    -> BuildScriptResult<empty> {
    auto output  = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
    auto written = rstd::fs::write_atomic(output.as_path(), action.content.as_str().as_bytes());
    if (written.is_err()) {
        return Err(BuildScriptError::BuildToolAction(
            BuildToolActionError::Publication("stage generated file"_Str,
                                              PathBuf::from(output.as_path()),
                                              rstd::move(written).unwrap_err())));
    }
    return Ok(empty {});
}

auto ToolActionSession::copy_action_staging(const RegisteredAction& action,
                                            ref<rstd::path::Path>   staging) const
    -> BuildScriptResult<empty> {
    auto contents = rstd::fs::read(action.inputs[usize {}].path.as_path());
    if (contents.is_err()) {
        return Err(BuildScriptError::Io("read copy input"_Str,
                                        PathBuf::from(action.inputs[usize {}].path.as_path()),
                                        rstd::move(contents).unwrap_err()));
    }
    auto output  = PathBuf::from(staging).join(action.outputs[usize {}].as_path());
    auto written = rstd::fs::write_atomic(output.as_path(), contents->as_slice());
    if (written.is_err()) {
        return Err(BuildScriptError::BuildToolAction(
            BuildToolActionError::Publication("stage copied file"_Str,
                                              PathBuf::from(output.as_path()),
                                              rstd::move(written).unwrap_err())));
    }
    return Ok(empty {});
}

auto ToolActionSession::transform_action_staging(const RegisteredAction& action,
                                                 ref<rstd::path::Path>   staging) const
    -> BuildScriptResult<empty> {
    auto contents = rstd::fs::read_to_string(action.inputs[usize {}].path.as_path());
    if (contents.is_err()) {
        return Err(BuildScriptError::Io("read transform input"_Str,
                                        PathBuf::from(action.inputs[usize {}].path.as_path()),
                                        rstd::move(contents).unwrap_err()));
    }
    auto preamble       = String::make();
    auto implementation = String::make();
    auto in_preamble    = true;
    auto has_code       = false;
    auto text           = contents->as_str();
    auto cursor         = usize {};
    while (cursor < text.len()) {
        auto end = cursor;
        while (end < text.len() && text.as_bytes()[end] != u8('\n')) ++end;
        auto line  = text.get(cursor, end).unwrap();
        auto blank = true;
        for (auto byte : line.as_bytes()) {
            if (byte == u8(' ') || byte == u8('\t') || byte == u8('\r')) continue;
            blank = false;
            break;
        }
        auto preamble_line = blank || line.starts_with("#include"_str) ||
                             line.starts_with("/*"_str) || line.starts_with("*"_str) ||
                             line.starts_with("//"_str);
        if (in_preamble && ! preamble_line) in_preamble = false;
        auto& destination = in_preamble ? preamble : implementation;
        destination.push_str(line);
        destination.push_ascii('\n');
        if (! in_preamble && ! blank) has_code = true;
        cursor = end < text.len() ? end + usize(1) : end;
    }
    if (preamble.is_empty() || ! has_code) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidOutput(
            action.inputs[usize {}].path.clone(),
            "generated C++ file has no separable leading preamble"_Str)));
    }
    ref<str> values[] = { preamble.as_str(), implementation.as_str() };
    for (usize index {}; index < action.outputs.len(); ++index) {
        auto output = PathBuf::from(staging).join(action.outputs[index].as_path());
        auto written =
            rstd::fs::write_atomic(output.as_path(), values[index.to_primitive()].as_bytes());
        if (written.is_err()) {
            return Err(BuildScriptError::BuildToolAction(
                BuildToolActionError::Publication("stage transform output"_Str,
                                                  PathBuf::from(output.as_path()),
                                                  rstd::move(written).unwrap_err())));
        }
    }
    return Ok(empty {});
}

auto ToolActionSession::execute_action(const RegisteredAction& action) const
    -> BuildScriptResult<empty> {
    auto generated_root =
        rstd_try(layout_->create_generated_package_directory(action.package.as_str()));
    auto action_root =
        layout_->build_tool_action_root().join(PathBuf::from(action.identity.clone()).as_path());
    auto receipt = action_root.join(PathBuf::from("receipt.json"_str).as_path());
    auto created = rstd::fs::create_dir_all(action_root.as_path());
    if (created.is_err()) {
        return Err(BuildScriptError::Io("create build-tool action directory"_Str,
                                        PathBuf::from(action_root.as_path()),
                                        rstd::move(created).unwrap_err()));
    }
    auto lock_path = action_root.join(PathBuf::from("lock"_str).as_path());
    auto opened =
        rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(lock_path.as_path());
    if (opened.is_err()) {
        return Err(BuildScriptError::Io("open build-tool action lock"_Str,
                                        PathBuf::from(lock_path.as_path()),
                                        rstd::move(opened).unwrap_err()));
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(opened).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return Err(BuildScriptError::Io("lock build-tool action"_Str,
                                        PathBuf::from(lock_path.as_path()),
                                        rstd::move(locked).unwrap_err()));
    }
    auto reusable = rstd_try(
        action_receipt_matches(receipt.as_path(),
                               action.identity.as_str(),
                               action.outputs,
                               ! action.inputs.is_empty() || action.depfile_output.is_some(),
                               generated_root.as_path()));
    if (reusable) {
        emit(BuildEventKind::BuildToolRunReuse,
             action.label.as_str(),
             action.outputs.len() == usize(1)
                 ? generated_root.join(action.outputs[usize {}].as_path()).as_path()
                 : generated_root.as_path());
        return Ok(empty {});
    }

    auto staging        = action_root.join(PathBuf::from("staging"_str).as_path());
    auto staging_exists = rstd::fs::exists(staging.as_path());
    if (staging_exists.is_err()) {
        return Err(BuildScriptError::Io("inspect build-tool action staging"_Str,
                                        PathBuf::from(staging.as_path()),
                                        rstd::move(staging_exists).unwrap_err()));
    }
    if (*staging_exists) {
        auto removed = rstd::fs::remove_dir_all(staging.as_path());
        if (removed.is_err()) {
            return Err(BuildScriptError::Io("clear build-tool action staging"_Str,
                                            PathBuf::from(staging.as_path()),
                                            rstd::move(removed).unwrap_err()));
        }
    }
    created = rstd::fs::create_dir_all(staging.as_path());
    if (created.is_err()) {
        return Err(BuildScriptError::Io("create build-tool output staging"_Str,
                                        PathBuf::from(staging.as_path()),
                                        rstd::move(created).unwrap_err()));
    }
    for (const auto& output : action.outputs) {
        auto staged_output = staging.join(output.as_path());
        auto parent        = staged_output.as_path().parent().unwrap();
        created            = rstd::fs::create_dir_all(parent);
        if (created.is_err()) {
            return Err(BuildScriptError::Io("create build-tool output parent"_Str,
                                            PathBuf::from(parent),
                                            rstd::move(created).unwrap_err()));
        }
    }

    if (action.kind == RegisteredActionKind::Process) {
        auto invocation = rstd_try(render_process_invocation(action, staging.as_path()));
        auto process_working_directory = action.working_directory.clone();
        if (action.output_working_directory.is_some()) {
            auto output = staging.join(action.outputs[*action.output_working_directory].as_path());
            process_working_directory = PathBuf::from(output.as_path().parent().unwrap());
        }
        auto executed =
            run_command(invocation, *environment_, Some(process_working_directory.as_path()));
        if (executed.is_err()) {
            return Err(BuildScriptError::BuildToolAction(BuildToolActionError::Process(
                action.label.clone(), rstd::move(executed).unwrap_err())));
        }
        if (executed->exit_code != i32 {}) {
            return Err(BuildScriptError::BuildToolAction(
                BuildToolActionError::Execution(action.label.clone(),
                                                executed->exit_code,
                                                rstd::move(executed->standard_output),
                                                rstd::move(executed->standard_error))));
        }
    } else if (action.kind == RegisteredActionKind::Write) {
        rstd_try(write_action_staging(action, staging.as_path()));
    } else if (action.kind == RegisteredActionKind::Copy) {
        rstd_try(copy_action_staging(action, staging.as_path()));
    } else {
        rstd_try(transform_action_staging(action, staging.as_path()));
    }

    auto actual_outputs = Vec<PathBuf>::make();
    rstd_try(collect_action_outputs(staging.as_path(), staging.as_path(), actual_outputs));
    auto       expected_outputs = action.outputs.clone();
    const auto order            = [](const PathBuf& left, const PathBuf& right) {
        return left.as_path().to_string_lossy() < right.as_path().to_string_lossy();
    };
    rstd::slice_::sort_unstable_by(actual_outputs.as_mut_slice().as_mut_ref(), order);
    rstd::slice_::sort_unstable_by(expected_outputs.as_mut_slice().as_mut_ref(), order);
    auto outputs_match = actual_outputs.len() == expected_outputs.len();
    for (usize index {}; outputs_match && index < actual_outputs.len(); ++index) {
        outputs_match = actual_outputs[index].as_path() == expected_outputs[index].as_path();
    }
    if (! outputs_match) {
        return Err(BuildScriptError::BuildToolAction(BuildToolActionError::InvalidOutput(
            staging.clone(), "produced files do not match the declared set"_Str)));
    }

    auto dependencies = rstd_try(current_action_dependencies(action));
    if (action.depfile_output.is_some()) {
        auto staged_depfile = staging.join(action.outputs[*action.depfile_output].as_path());
        auto direct_inputs  = action.inputs.iter()
                                  .map([](auto input) {
                                     return input->path.clone();
                                  })
                                  .collect<Vec<PathBuf>>();
        auto depfile_working_directory = action.working_directory.clone();
        if (action.output_working_directory.is_some()) {
            auto output = staging.join(action.outputs[*action.output_working_directory].as_path());
            depfile_working_directory = PathBuf::from(output.as_path().parent().unwrap());
        }
        auto depfile = rstd_try(load_action_dependencies(staged_depfile.as_path(),
                                                         depfile_working_directory.as_path(),
                                                         action.depfile_roots,
                                                         direct_inputs));
        merge_action_dependencies(dependencies, rstd::move(depfile));
    }

    auto digests = Vec<String>::make();
    for (const auto& output : action.outputs) {
        auto staged_output = staging.join(output.as_path());
        auto bytes         = rstd::fs::read(staged_output.as_path());
        if (bytes.is_err()) {
            return Err(BuildScriptError::Io("read staged build-tool output"_Str,
                                            PathBuf::from(staged_output.as_path()),
                                            rstd::move(bytes).unwrap_err()));
        }
        auto final  = generated_root.join(output.as_path());
        auto parent = final.as_path().parent().unwrap();
        created     = rstd::fs::create_dir_all(parent);
        if (created.is_err()) {
            return Err(BuildScriptError::Io("create build-tool output parent"_Str,
                                            PathBuf::from(parent),
                                            rstd::move(created).unwrap_err()));
        }
        auto written = rstd::fs::write_atomic_if_changed(final.as_path(), bytes->as_slice());
        if (written.is_err()) {
            return Err(BuildScriptError::BuildToolAction(
                BuildToolActionError::Publication("publish build-tool output"_Str,
                                                  PathBuf::from(final.as_path()),
                                                  rstd::move(written).unwrap_err())));
        }
        digests.push(licrypto::sha256_hex(bytes->as_slice()));
    }
    auto receipt_text =
        action_receipt_text(action.identity.as_str(), action.outputs, digests, dependencies);
    auto receipt_written =
        rstd::fs::write_atomic(receipt.as_path(), receipt_text.as_str().as_bytes());
    if (receipt_written.is_err()) {
        return Err(BuildScriptError::BuildToolAction(
            BuildToolActionError::Receipt("write build-tool action receipt"_Str,
                                          PathBuf::from(receipt.as_path()),
                                          rstd::move(receipt_written).unwrap_err())));
    }
    emit(BuildEventKind::BuildToolRun,
         action.label.as_str(),
         action.outputs.len() == usize(1)
             ? generated_root.join(action.outputs[usize {}].as_path()).as_path()
             : generated_root.as_path());
    return Ok(empty {});
}

} // namespace lito
