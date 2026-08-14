module;
#include <rstd/macro.hpp>

export module lito.install.store;

import rstd;
import rstd.json;
import lito.error;
import lito.install.catalog;
import lito.install.contract;
import lito.install.identity;
import lito.install.path;
import lito.install.publication;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonMap   = rstd::json::Map;
using JsonArray = rstd::json::Array;

export namespace lito
{

auto create_install_layout(InstallRoot root) -> InstallStoreResult<InstallLayout>;

} // namespace lito

namespace lito
{

struct TransactionItem {
    PathBuf relative;
    PathBuf staged;
    PathBuf backup;
    bool    had_existing {};
    bool    publish {};
};

template<typename T>
auto store_failure(String message) -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(InstallStoreCause::Message(rstd::move(message))));
}

template<typename T>
auto store_failure(ref<str> message) -> InstallStoreResult<T> {
    return store_failure<T>(String::make(message));
}

template<typename T>
auto store_io_failure(ref<str> operation, ref<rstd::path::Path> path, rstd::io::error::Error error)
    -> InstallStoreResult<T> {
    return Err(InstallStoreError::Cause(
        InstallStoreCause::Io(String::make(operation), PathBuf::from(path), rstd::move(error))));
}

auto path_metadata(ref<rstd::path::Path> path) -> InstallStoreResult<Option<rstd::fs::Metadata>> {
    auto metadata = rstd::fs::symlink_metadata(path);
    if (metadata.is_ok()) return Ok(Some(rstd::move(metadata).unwrap()));
    auto error = rstd::move(metadata).unwrap_err();
    if (error.kind() == rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::NotFound }) {
        return Ok(None());
    }
    return store_io_failure<Option<rstd::fs::Metadata>>(
        "inspect install path"_str, path, rstd::move(error));
}

auto validate_directory(ref<rstd::path::Path> path, ref<str> role) -> InstallStoreResult<empty> {
    auto metadata = rstd_try(path_metadata(path));
    if (metadata.is_none() || ! metadata->is_dir() || metadata->is_symlink()) {
        return store_failure<empty>(
            rstd::format("{} directory '{}' is not a real directory", role, path));
    }
    return Ok(empty {});
}

auto validate_parent_tree(ref<rstd::path::Path> root, ref<rstd::path::Path> relative)
    -> InstallStoreResult<empty> {
    auto parent = relative.parent();
    if (parent.is_none() || parent->is_empty()) return Ok(empty {});
    auto current    = PathBuf::from(root);
    auto components = parent->components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        current.push(PathBuf::from(component->as_os_str()).as_path());
        auto metadata = rstd_try(path_metadata(current.as_path()));
        if (metadata.is_none()) return Ok(empty {});
        if (! metadata->is_dir() || metadata->is_symlink()) {
            return store_failure<empty>(rstd::format(
                "install destination parent '{}' is not a real directory", current.as_path()));
        }
    }
    return Ok(empty {});
}

auto ensure_parent_tree(ref<rstd::path::Path> root,
                        ref<rstd::path::Path> relative,
                        Vec<PathBuf>*         created = nullptr) -> InstallStoreResult<empty> {
    auto parent = relative.parent();
    if (parent.is_none() || parent->is_empty()) return Ok(empty {});
    auto current    = PathBuf::from(root);
    auto components = parent->components();
    for (auto component = components.next(); component.is_some(); component = components.next()) {
        current.push(PathBuf::from(component->as_os_str()).as_path());
        auto metadata = rstd_try(path_metadata(current.as_path()));
        if (metadata.is_some()) {
            if (! metadata->is_dir() || metadata->is_symlink()) {
                return store_failure<empty>(rstd::format(
                    "install destination parent '{}' is not a real directory", current.as_path()));
            }
            continue;
        }
        auto made = rstd::fs::create_dir(current.as_path());
        if (made.is_err()) {
            return store_io_failure<empty>("create install destination parent"_str,
                                           current.as_path(),
                                           rstd::move(made).unwrap_err());
        }
        if (created != nullptr) created->push(current.clone());
    }
    return Ok(empty {});
}

auto same_file(ref<rstd::path::Path> left, ref<rstd::path::Path> right)
    -> InstallStoreResult<bool> {
    auto left_metadata  = rstd::fs::metadata(left);
    auto right_metadata = rstd::fs::metadata(right);
    if (left_metadata.is_err()) {
        return store_io_failure<bool>(
            "inspect staged entry"_str, left, rstd::move(left_metadata).unwrap_err());
    }
    if (right_metadata.is_err()) {
        return store_io_failure<bool>(
            "inspect installed entry"_str, right, rstd::move(right_metadata).unwrap_err());
    }
    if (left_metadata->len() != right_metadata->len() ||
        left_metadata->permissions().mode() != right_metadata->permissions().mode()) {
        return Ok(false);
    }
    auto left_contents  = rstd::fs::read(left);
    auto right_contents = rstd::fs::read(right);
    if (left_contents.is_err()) {
        return store_io_failure<bool>(
            "read staged entry"_str, left, rstd::move(left_contents).unwrap_err());
    }
    if (right_contents.is_err()) {
        return store_io_failure<bool>(
            "read installed entry"_str, right, rstd::move(right_contents).unwrap_err());
    }
    if (left_contents->len() != right_contents->len()) return Ok(false);
    for (usize index {}; index < left_contents->len(); ++index) {
        if ((*left_contents)[index].get() != (*right_contents)[index].get()) return Ok(false);
    }
    return Ok(true);
}

auto same_link(ref<rstd::path::Path> left, ref<rstd::path::Path> right)
    -> InstallStoreResult<bool> {
    auto left_target  = rstd::fs::read_link(left);
    auto right_target = rstd::fs::read_link(right);
    if (left_target.is_err()) {
        return store_io_failure<bool>(
            "read staged install link"_str, left, rstd::move(left_target).unwrap_err());
    }
    if (right_target.is_err()) {
        return store_io_failure<bool>(
            "read installed link"_str, right, rstd::move(right_target).unwrap_err());
    }
    return Ok(left_target->as_path() == right_target->as_path());
}

auto stage_entry(const InstallEntry& entry, ref<rstd::path::Path> staged)
    -> InstallStoreResult<empty> {
    auto parent = staged.parent();
    if (parent.is_none()) return store_failure<empty>("staged entry has no parent"_str);
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return store_io_failure<empty>(
            "create staging parent"_str, *parent, rstd::move(created).unwrap_err());
    }
    if (entry.payload.is_CopyFile()) {
        const auto& source   = entry.payload.as_CopyFile().source;
        auto        metadata = rstd_try(path_metadata(source.as_path()));
        if (metadata.is_none() || ! metadata->is_file() || metadata->is_symlink()) {
            return store_failure<empty>(rstd::format(
                "install source '{}' is not a regular non-symlink file", source.as_path()));
        }
        auto copied = rstd::fs::copy(source.as_path(), staged);
        if (copied.is_err()) {
            return store_io_failure<empty>(
                "stage install entry"_str, source.as_path(), rstd::move(copied).unwrap_err());
        }
        auto permissions = rstd::fs::set_permissions(staged, metadata->permissions());
        if (permissions.is_err()) {
            return store_io_failure<empty>("preserve install entry permissions"_str,
                                           source.as_path(),
                                           rstd::move(permissions).unwrap_err());
        }
        return Ok(empty {});
    }
    const auto& bytes   = entry.payload.as_Bytes();
    auto        written = rstd::fs::write(staged, bytes.contents.as_slice());
    if (written.is_err()) {
        return store_io_failure<empty>(
            "stage generated install entry"_str, staged, rstd::move(written).unwrap_err());
    }
    auto permissions =
        rstd::fs::set_permissions(staged, rstd::fs::Permissions::from_mode(bytes.permissions));
    if (permissions.is_err()) {
        return store_io_failure<empty>("set generated install entry permissions"_str,
                                       staged,
                                       rstd::move(permissions).unwrap_err());
    }
    return Ok(empty {});
}

auto stage_link(ref<rstd::path::Path> relative_target, ref<rstd::path::Path> staged)
    -> InstallStoreResult<empty> {
    auto parent = staged.parent();
    if (parent.is_none()) return store_failure<empty>("staged link has no parent"_str);
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return store_io_failure<empty>(
            "create staged link parent"_str, *parent, rstd::move(created).unwrap_err());
    }
    auto linked = rstd::fs::soft_link(relative_target, staged);
    if (linked.is_err()) {
        return store_io_failure<empty>(
            "stage install link"_str, staged, rstd::move(linked).unwrap_err());
    }
    return Ok(empty {});
}

auto remove_installed_path(ref<rstd::path::Path> path) -> InstallStoreResult<empty> {
    auto metadata = rstd_try(path_metadata(path));
    if (metadata.is_none()) return Ok(empty {});
    if (metadata->is_dir() && ! metadata->is_symlink()) {
        return store_failure<empty>(
            rstd::format("install transaction path '{}' unexpectedly became a directory", path));
    }
    auto removed = rstd::fs::remove_file(path);
    if (removed.is_err()) {
        return store_io_failure<empty>(
            "remove install transaction path"_str, path, rstd::move(removed).unwrap_err());
    }
    return Ok(empty {});
}

auto rollback_transaction(ref<rstd::path::Path>       root,
                          ref<rstd::path::Path>       transaction,
                          const Vec<TransactionItem>& items) -> Vec<InstallRollbackFailure> {
    auto failures = Vec<InstallRollbackFailure>::make();
    for (auto index = items.len(); index > usize {}; --index) {
        const auto& item            = items[index - usize(1)];
        auto        destination     = PathBuf::from(root).join(item.relative.as_path());
        auto        backup          = PathBuf::from(transaction).join(item.backup.as_path());
        auto        staged          = PathBuf::from(transaction).join(item.staged.as_path());
        auto        backup_metadata = path_metadata(backup.as_path());
        if (backup_metadata.is_err()) {
            auto error = rstd::move(backup_metadata).unwrap_err();
            if (error.is_Cause()) {
                auto cause = rstd::move(error).as_Cause().source;
                if (! cause.is_Io()) continue;
                auto io = rstd::move(cause).as_Io();
                failures.push(InstallRollbackFailure {
                    .operation = String::make("inspect backup"_str),
                    .path      = backup.clone(),
                    .source    = rstd::move(io.source),
                });
            }
            continue;
        }
        if (item.had_existing && backup_metadata->is_some()) {
            auto removed = remove_installed_path(destination.as_path());
            if (removed.is_err()) {
                auto error = rstd::move(removed).unwrap_err();
                if (error.is_Cause()) {
                    auto cause = rstd::move(error).as_Cause().source;
                    if (! cause.is_Io()) continue;
                    auto io = rstd::move(cause).as_Io();
                    failures.push(InstallRollbackFailure {
                        .operation = String::make("remove published entry"_str),
                        .path      = destination.clone(),
                        .source    = rstd::move(io.source),
                    });
                }
                continue;
            }
            auto prepared = ensure_parent_tree(root, item.relative.as_path());
            if (prepared.is_err()) continue;
            auto restored = rstd::fs::rename(backup.as_path(), destination.as_path());
            if (restored.is_err()) {
                failures.push(InstallRollbackFailure {
                    .operation = String::make("restore installed entry"_str),
                    .path      = destination.clone(),
                    .source    = rstd::move(restored).unwrap_err(),
                });
            }
            continue;
        }
        if (! item.had_existing && item.publish) {
            auto staged_metadata = path_metadata(staged.as_path());
            if (staged_metadata.is_ok() && staged_metadata->is_none()) {
                auto removed = remove_installed_path(destination.as_path());
                if (removed.is_err()) {
                    auto error = rstd::move(removed).unwrap_err();
                    if (error.is_Cause()) {
                        auto cause = rstd::move(error).as_Cause().source;
                        if (! cause.is_Io()) continue;
                        auto io = rstd::move(cause).as_Io();
                        failures.push(InstallRollbackFailure {
                            .operation = String::make("remove published entry"_str),
                            .path      = destination.clone(),
                            .source    = rstd::move(io.source),
                        });
                    }
                }
            }
        }
    }
    return failures;
}

void rollback_created_directories(const Vec<PathBuf>&          created,
                                  Vec<InstallRollbackFailure>& failures) {
    for (auto index = created.len(); index > usize {}; --index) {
        const auto& path    = created[index - usize(1)];
        auto        removed = rstd::fs::remove_dir(path.as_path());
        if (removed.is_err()) {
            failures.push(InstallRollbackFailure {
                .operation = String::make("remove created directory"_str),
                .path      = path.clone(),
                .source    = rstd::move(removed).unwrap_err(),
            });
        }
    }
}

auto transaction_failure(ref<str>                    operation,
                         InstallStoreError           error,
                         ref<rstd::path::Path>       root,
                         ref<rstd::path::Path>       transaction,
                         const Vec<TransactionItem>& items,
                         const Vec<PathBuf>* created_directories = nullptr) -> InstallStoreError {
    auto rollback_failures = rollback_transaction(root, transaction, items);
    if (created_directories != nullptr) {
        rollback_created_directories(*created_directories, rollback_failures);
    }
    return InstallStoreError::Transaction(
        String::make(operation),
        rstd::boxed::Box<InstallStoreError>::make(rstd::move(error)),
        rstd::move(rollback_failures));
}

auto transaction_journal(const Vec<TransactionItem>& items) -> String {
    auto values = JsonArray::with_capacity(items.len());
    for (const auto& item : items) {
        auto value = JsonMap::make();
        value.insert(String::make("path"_str),
                     Json::String(item.relative.as_path().to_string_lossy()));
        value.insert(String::make("had-existing"_str), Json::Bool(item.had_existing));
        value.insert(String::make("publish"_str), Json::Bool(item.publish));
        values.push(Json::Object(rstd::move(value)));
    }
    auto root = JsonMap::make();
    root.insert(String::make("schema"_str), Json::Number(rstd::json::Number::from_u64(u64(1))));
    root.insert(String::make("items"_str), Json::Array(rstd::move(values)));
    auto text =
        rstd::json::to_string(Json::Object(rstd::move(root)),
                              rstd::json::FormatOptions { .pretty = true, .indent = usize(2) });
    text.push_ascii('\n');
    return text;
}

auto parse_transaction_journal(ref<rstd::path::Path> transaction)
    -> InstallStoreResult<Vec<TransactionItem>> {
    auto journal  = PathBuf::from(transaction).join(PathBuf::from("journal.json"_str).as_path());
    auto contents = rstd::fs::read_to_string(journal.as_path());
    if (contents.is_err()) {
        return store_io_failure<Vec<TransactionItem>>("read install transaction journal"_str,
                                                      journal.as_path(),
                                                      rstd::move(contents).unwrap_err());
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return Err(InstallStoreError::Cause(
            InstallStoreCause::Json(journal.clone(), rstd::move(parsed).unwrap_err())));
    }
    auto schema = parsed->get("schema"_str);
    auto items  = parsed->get("items"_str);
    if (schema.is_none() || (**schema).as_u64() != Some(u64(1)) || items.is_none() ||
        (**items).as_array().is_none()) {
        return store_failure<Vec<TransactionItem>>(
            rstd::format("install transaction journal '{}' is invalid", journal.as_path()));
    }
    auto result = Vec<TransactionItem>::make();
    for (const auto& value : **(**items).as_array()) {
        auto path_value = value.get("path"_str);
        auto existing   = value.get("had-existing"_str);
        auto publish    = value.get("publish"_str);
        if (path_value.is_none() || (**path_value).as_str().is_none() || existing.is_none() ||
            (**existing).as_bool().is_none() || publish.is_none() ||
            (**publish).as_bool().is_none()) {
            return store_failure<Vec<TransactionItem>>(rstd::format(
                "install transaction journal '{}' contains an invalid item", journal.as_path()));
        }
        auto relative = PathBuf::from(*(**path_value).as_str());
        if (! install_relative_destination_is_valid(relative.as_path())) {
            return store_failure<Vec<TransactionItem>>(rstd::format(
                "install transaction journal '{}' contains an unsafe path", journal.as_path()));
        }
        result.push(TransactionItem {
            .relative     = relative.clone(),
            .staged       = PathBuf::from("new"_str).join(relative.as_path()),
            .backup       = PathBuf::from("backup"_str).join(relative.as_path()),
            .had_existing = *(**existing).as_bool(),
            .publish      = *(**publish).as_bool(),
        });
    }
    return Ok(rstd::move(result));
}

auto transaction_path(ref<rstd::path::Path> transactions) -> PathBuf {
    auto time = rstd::time::SystemTime::now().as_unix_time();
    return PathBuf::from(transactions)
        .join(PathBuf::from(
                  rstd::format("{}-{}-{}", rstd::process::id(), time.seconds, time.nanoseconds))
                  .as_path());
}

auto recover_managed_transactions(const InstallLayout& layout) -> InstallStoreResult<empty> {
    auto metadata = rstd_try(path_metadata(layout.transactions.as_path()));
    if (metadata.is_none()) return Ok(empty {});
    if (! metadata->is_dir() || metadata->is_symlink()) {
        return store_failure<empty>(rstd::format("install transaction directory '{}' is unsafe",
                                                 layout.transactions.as_path()));
    }
    auto opened = rstd::fs::read_dir(layout.transactions.as_path());
    if (opened.is_err()) {
        return store_io_failure<empty>("read install transactions"_str,
                                       layout.transactions.as_path(),
                                       rstd::move(opened).unwrap_err());
    }
    auto entries = rstd::move(opened).unwrap();
    for (auto next = entries.next(); next.is_some(); next = entries.next()) {
        auto item = rstd::move(next).unwrap();
        if (item.is_err()) {
            return store_io_failure<empty>("read install transaction entry"_str,
                                           layout.transactions.as_path(),
                                           rstd::move(item).unwrap_err());
        }
        auto path      = item->path();
        auto item_type = item->file_type();
        if (item_type.is_err()) {
            return store_io_failure<empty>("inspect install transaction"_str,
                                           path.as_path(),
                                           rstd::move(item_type).unwrap_err());
        }
        if (! item_type->is_dir() || item_type->is_symlink()) {
            return store_failure<empty>(
                rstd::format("install transaction '{}' is not a real directory", path.as_path()));
        }
        auto committed          = path.join(PathBuf::from("committed"_str).as_path());
        auto committed_metadata = rstd_try(path_metadata(committed.as_path()));
        if (committed_metadata.is_some()) {
            if (! committed_metadata->is_file() || committed_metadata->is_symlink()) {
                return store_failure<empty>(
                    rstd::format("install transaction marker '{}' is unsafe", committed.as_path()));
            }
            auto removed = rstd::fs::remove_dir_all(path.as_path());
            if (removed.is_err()) {
                return store_io_failure<empty>("clean committed install transaction"_str,
                                               path.as_path(),
                                               rstd::move(removed).unwrap_err());
            }
            continue;
        }
        auto journal          = path.join(PathBuf::from("journal.json"_str).as_path());
        auto journal_metadata = rstd_try(path_metadata(journal.as_path()));
        if (journal_metadata.is_none()) {
            auto removed = rstd::fs::remove_dir_all(path.as_path());
            if (removed.is_err()) {
                return store_io_failure<empty>("clean unprepared install transaction"_str,
                                               path.as_path(),
                                               rstd::move(removed).unwrap_err());
            }
            continue;
        }
        if (! journal_metadata->is_file() || journal_metadata->is_symlink()) {
            return store_failure<empty>(
                rstd::format("install transaction journal '{}' is unsafe", journal.as_path()));
        }
        auto items    = rstd_try(parse_transaction_journal(path.as_path()));
        auto failures = rollback_transaction(layout.root.path.as_path(), path.as_path(), items);
        if (! failures.is_empty()) {
            return Err(InstallStoreError::Transaction(
                String::make("install transaction recovery"_str),
                rstd::boxed::Box<InstallStoreError>::make(
                    InstallStoreError::Cause(InstallStoreCause::Message(
                        String::make("cannot recover interrupted install transaction"_str)))),
                rstd::move(failures)));
        }
        auto removed = rstd::fs::remove_dir_all(path.as_path());
        if (removed.is_err()) {
            return store_io_failure<empty>("clean recovered install transaction"_str,
                                           path.as_path(),
                                           rstd::move(removed).unwrap_err());
        }
    }
    (void)rstd::fs::remove_dir(layout.transactions.as_path());
    return Ok(empty {});
}

auto create_transaction(ref<rstd::path::Path> path) -> InstallStoreResult<empty> {
    auto made = rstd::fs::create_dir(path);
    if (made.is_err()) {
        return store_io_failure<empty>(
            "create install transaction"_str, path, rstd::move(made).unwrap_err());
    }
    auto staging = PathBuf::from(path).join(PathBuf::from("new"_str).as_path());
    auto backup  = PathBuf::from(path).join(PathBuf::from("backup"_str).as_path());
    auto created = rstd::fs::create_dir(staging.as_path());
    if (created.is_err()) {
        return store_io_failure<empty>("create install staging directory"_str,
                                       staging.as_path(),
                                       rstd::move(created).unwrap_err());
    }
    created = rstd::fs::create_dir(backup.as_path());
    if (created.is_err()) {
        return store_io_failure<empty>("create install backup directory"_str,
                                       backup.as_path(),
                                       rstd::move(created).unwrap_err());
    }
    return Ok(empty {});
}

auto add_transaction_item(Vec<TransactionItem>& items,
                          ref<rstd::path::Path> relative,
                          bool                  had_existing,
                          bool                  publish) -> void {
    items.push(TransactionItem {
        .relative     = PathBuf::from(relative),
        .staged       = PathBuf::from("new"_str).join(relative),
        .backup       = PathBuf::from("backup"_str).join(relative),
        .had_existing = had_existing,
        .publish      = publish,
    });
}

auto execute_transaction(ref<rstd::path::Path>       root,
                         ref<rstd::path::Path>       transaction,
                         const Vec<TransactionItem>& items) -> InstallStoreResult<empty> {
    auto journal = PathBuf::from(transaction).join(PathBuf::from("journal.json"_str).as_path());
    auto journal_text = transaction_journal(items);
    auto written      = rstd::fs::write_atomic(journal.as_path(), journal_text.as_str().as_bytes());
    if (written.is_err()) {
        return store_io_failure<empty>("write install transaction journal"_str,
                                       journal.as_path(),
                                       rstd::move(written).unwrap_err());
    }

    for (const auto& item : items) {
        if (! item.had_existing) continue;
        auto destination = PathBuf::from(root).join(item.relative.as_path());
        auto backup      = PathBuf::from(transaction).join(item.backup.as_path());
        auto prepared    = ensure_parent_tree(transaction, item.backup.as_path());
        if (prepared.is_err()) {
            return Err(transaction_failure(
                "install backup"_str, rstd::move(prepared).unwrap_err(), root, transaction, items));
        }
        auto moved = rstd::fs::rename(destination.as_path(), backup.as_path());
        if (moved.is_err()) {
            auto error = InstallStoreError::Cause(
                InstallStoreCause::Io(String::make("back up install destination"_str),
                                      destination.clone(),
                                      rstd::move(moved).unwrap_err()));
            return Err(transaction_failure(
                "install backup"_str, rstd::move(error), root, transaction, items));
        }
    }

    auto created_directories = Vec<PathBuf>::make();
    for (const auto& item : items) {
        if (! item.publish) continue;
        auto destination = PathBuf::from(root).join(item.relative.as_path());
        auto staged      = PathBuf::from(transaction).join(item.staged.as_path());
        auto prepared =
            ensure_parent_tree(root, item.relative.as_path(), rstd::addressof(created_directories));
        if (prepared.is_err()) {
            return Err(transaction_failure("install publish"_str,
                                           rstd::move(prepared).unwrap_err(),
                                           root,
                                           transaction,
                                           items,
                                           rstd::addressof(created_directories)));
        }
        auto moved = rstd::fs::rename(staged.as_path(), destination.as_path());
        if (moved.is_err()) {
            auto error = InstallStoreError::Cause(
                InstallStoreCause::Io(String::make("publish install entry"_str),
                                      destination.clone(),
                                      rstd::move(moved).unwrap_err()));
            return Err(transaction_failure("install publish"_str,
                                           rstd::move(error),
                                           root,
                                           transaction,
                                           items,
                                           rstd::addressof(created_directories)));
        }
    }
    auto committed = PathBuf::from(transaction).join(PathBuf::from("committed"_str).as_path());
    written        = rstd::fs::write_atomic(committed.as_path(), "committed\n"_str.as_bytes());
    if (written.is_err()) {
        auto error = InstallStoreError::Cause(
            InstallStoreCause::Io(String::make("commit install transaction"_str),
                                  committed.clone(),
                                  rstd::move(written).unwrap_err()));
        return Err(transaction_failure("install commit"_str,
                                       rstd::move(error),
                                       root,
                                       transaction,
                                       items,
                                       rstd::addressof(created_directories)));
    }
    return Ok(empty {});
}

auto catalog_clone(const InstallCatalog& source) -> InstallCatalog {
    auto result = InstallCatalog {};
    result.packages.reserve(source.packages.len());
    for (const auto& package : source.packages) result.packages.push(package.clone());
    return result;
}

auto catalog_contains_physical(const InstallCatalog& catalog, ref<rstd::path::Path> path) -> bool {
    return managed_catalog_entry_owner(catalog, path).is_some();
}

auto incoming_physical_paths(const InstallPublicationPlan& publication)
    -> rstd::collections::BTreeMap<String, empty> {
    auto result = rstd::collections::BTreeMap<String, empty>::make();
    for (const auto& package : publication.packages) {
        for (const auto& entry : package.info.entries) {
            result.insert(entry.physical_destination.as_path().to_string_lossy(), empty {});
        }
    }
    return result;
}

auto next_managed_catalog(const InstallLayout&          layout,
                          const InstallCatalog&         current,
                          const InstallPublicationPlan& publication,
                          bool force) -> InstallStoreResult<InstallCatalog> {
    auto result   = catalog_clone(current);
    auto incoming = incoming_physical_paths(publication);
    for (usize package {}; package < result.packages.len();) {
        auto replaced = false;
        for (const auto& next : publication.packages) {
            if (result.packages[package].identity.id == next.info.identity.id.as_str()) {
                replaced = true;
                break;
            }
        }
        if (replaced) {
            (void)result.packages.remove(package);
            continue;
        }
        if (force) {
            for (usize entry {}; entry < result.packages[package].entries.len();) {
                auto key = result.packages[package]
                               .entries[entry]
                               .physical_destination.as_path()
                               .to_string_lossy();
                if (incoming.contains_key(key.as_str())) {
                    (void)result.packages[package].entries.remove(entry);
                } else {
                    ++entry;
                }
            }
            if (result.packages[package].entries.is_empty()) {
                (void)result.packages.remove(package);
                continue;
            }
        }
        ++package;
    }
    for (const auto& package : publication.packages) result.packages.push(package.info.clone());
    rstd_try(validate_managed_install_catalog(layout, result));
    return Ok(rstd::move(result));
}

auto stage_text(ref<str> contents, ref<rstd::path::Path> staged) -> InstallStoreResult<empty> {
    auto parent = staged.parent();
    if (parent.is_none()) return store_failure<empty>("staged text has no parent"_str);
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return store_io_failure<empty>(
            "create staged text parent"_str, *parent, rstd::move(created).unwrap_err());
    }
    auto written = rstd::fs::write(staged, contents.as_bytes());
    if (written.is_err()) {
        return store_io_failure<empty>(
            "stage install metadata"_str, staged, rstd::move(written).unwrap_err());
    }
    return Ok(empty {});
}

auto validate_managed_existing(const InstallCatalog&             catalog,
                               ref<str>                          incoming_id,
                               ref<rstd::path::Path>             relative,
                               const Option<rstd::fs::Metadata>& metadata,
                               bool force) -> InstallStoreResult<empty> {
    auto owner = managed_catalog_entry_owner(catalog, relative);
    if (owner.is_some() && catalog.packages[*owner].identity.id != incoming_id && ! force) {
        return store_failure<empty>(
            rstd::format("destination '{}' is already installed by package '{}'",
                         relative,
                         catalog.packages[*owner].identity.name.as_str()));
    }
    if (metadata.is_some() && owner.is_none() && ! force) {
        return store_failure<empty>(rstd::format(
            "destination '{}' is not managed by Lito; use --force to replace it", relative));
    }
    if (metadata.is_some() && metadata->is_dir() && ! metadata->is_symlink()) {
        return store_failure<empty>(
            rstd::format("install destination '{}' is a directory", relative));
    }
    if (metadata.is_some() && metadata->is_symlink() && owner.is_none()) {
        return store_failure<empty>(
            rstd::format("install destination '{}' is an unmanaged symbolic link", relative));
    }
    return Ok(empty {});
}

auto prepare_managed_payload(const InstallLayout&    layout,
                             const InstallCatalog&   catalog,
                             InstallPublicationPlan& publication,
                             ref<rstd::path::Path>   transaction,
                             bool                    force,
                             Vec<TransactionItem>&   items) -> InstallStoreResult<empty> {
    for (auto& package : publication.packages) {
        for (usize index {}; index < package.record.entries.len(); ++index) {
            auto& entry    = package.record.entries[index];
            auto& owned    = package.info.entries[index];
            auto  relative = owned.physical_destination.as_path();
            rstd_try(validate_parent_tree(layout.root.path.as_path(), relative));
            auto destination = layout.root.path.join(relative);
            auto existing    = rstd_try(path_metadata(destination.as_path()));
            rstd_try(validate_managed_existing(
                catalog, package.info.identity.id.as_str(), relative, existing, force));
            auto staged =
                PathBuf::from(transaction).join(PathBuf::from("new"_str).as_path()).join(relative);
            rstd_try(stage_entry(entry, staged.as_path()));
            auto unchanged = false;
            if (existing.is_some() && existing->is_file() && ! existing->is_symlink()) {
                unchanged = rstd_try(same_file(staged.as_path(), destination.as_path()));
            }
            entry.action =
                unchanged ? InstallAction::Unchanged
                          : (existing.is_some() ? InstallAction::Replaced : InstallAction::Created);
            if (! unchanged) add_transaction_item(items, relative, existing.is_some(), true);
        }
        for (auto& link : package.links) {
            auto relative = link.physical_destination.as_path();
            rstd_try(validate_parent_tree(layout.root.path.as_path(), relative));
            auto destination = layout.root.path.join(relative);
            auto existing    = rstd_try(path_metadata(destination.as_path()));
            rstd_try(validate_managed_existing(
                catalog, package.info.identity.id.as_str(), relative, existing, force));
            auto staged =
                PathBuf::from(transaction).join(PathBuf::from("new"_str).as_path()).join(relative);
            rstd_try(stage_link(link.relative_target.as_path(), staged.as_path()));
            auto unchanged = false;
            if (existing.is_some() && existing->is_symlink()) {
                unchanged = rstd_try(same_link(staged.as_path(), destination.as_path()));
            }
            link.action =
                unchanged ? InstallAction::Unchanged
                          : (existing.is_some() ? InstallAction::Replaced : InstallAction::Created);
            if (! unchanged) add_transaction_item(items, relative, existing.is_some(), true);
        }
    }
    return Ok(empty {});
}

auto prepare_managed_infos(const InstallLayout&  layout,
                           const InstallCatalog& current,
                           const InstallCatalog& next,
                           ref<rstd::path::Path> transaction,
                           Vec<TransactionItem>& items) -> InstallStoreResult<empty> {
    for (const auto& package : next.packages) {
        auto relative = PathBuf::from("packages"_str);
        relative.push(
            PathBuf::from(rstd::format("{}.info", package.identity.id.as_str())).as_path());
        auto destination = layout.root.path.join(relative.as_path());
        auto existing    = rstd_try(path_metadata(destination.as_path()));
        if (existing.is_some() && (! existing->is_file() || existing->is_symlink())) {
            return store_failure<empty>(
                rstd::format("install package info '{}' is not a regular non-symlink file",
                             destination.as_path()));
        }
        auto staged = PathBuf::from(transaction)
                          .join(PathBuf::from("new"_str).as_path())
                          .join(relative.as_path());
        auto text   = rstd_try(serialize_install_package_info(package));
        rstd_try(stage_text(text.as_str(), staged.as_path()));
        auto unchanged =
            existing.is_some() && rstd_try(same_file(staged.as_path(), destination.as_path()));
        if (! unchanged) {
            add_transaction_item(items, relative.as_path(), existing.is_some(), true);
        }
    }
    for (const auto& package : current.packages) {
        auto retained = false;
        for (const auto& next_package : next.packages) {
            if (next_package.identity.id == package.identity.id.as_str()) retained = true;
        }
        if (retained) continue;
        auto relative = PathBuf::from("packages"_str);
        relative.push(
            PathBuf::from(rstd::format("{}.info", package.identity.id.as_str())).as_path());
        auto destination = layout.root.path.join(relative.as_path());
        auto existing    = rstd_try(path_metadata(destination.as_path()));
        if (existing.is_some()) add_transaction_item(items, relative.as_path(), true, false);
    }
    return Ok(empty {});
}

auto prepare_managed_orphans(const InstallLayout&  layout,
                             const InstallCatalog& current,
                             const InstallCatalog& next,
                             Vec<TransactionItem>& items) -> InstallStoreResult<Vec<PathBuf>> {
    auto orphans = Vec<PathBuf>::make();
    for (const auto& package : current.packages) {
        for (const auto& entry : package.entries) {
            if (catalog_contains_physical(next, entry.physical_destination.as_path())) continue;
            auto already_planned = false;
            for (const auto& item : items) {
                if (item.relative.as_path() == entry.physical_destination.as_path()) {
                    already_planned = true;
                    break;
                }
            }
            if (already_planned) continue;
            auto destination = layout.root.path.join(entry.physical_destination.as_path());
            auto existing    = rstd_try(path_metadata(destination.as_path()));
            if (existing.is_some()) {
                add_transaction_item(items, entry.physical_destination.as_path(), true, false);
                orphans.push(entry.physical_destination.clone());
            }
        }
    }
    return Ok(rstd::move(orphans));
}

auto clean_empty_parents(ref<rstd::path::Path> root, ref<rstd::path::Path> relative) -> void {
    auto path   = PathBuf::from(root).join(relative);
    auto parent = path.as_path().parent();
    if (parent.is_none()) return;
    auto current = PathBuf::from(*parent);
    while (current.as_path() != root) {
        if (rstd::fs::remove_dir(current.as_path()).is_err()) break;
        auto next = current.as_path().parent();
        if (next.is_none()) break;
        current = PathBuf::from(*next);
    }
}

auto summarize_publication(InstallPublicationPlan publication) -> InstallStoreSummary {
    auto packages = Vec<String>::make();
    auto binaries = Vec<InstallBinary>::make();
    auto entries  = Vec<InstallEntry>::make();
    auto links    = Vec<InstallLink>::make();
    for (auto& package : publication.packages) {
        packages.push(package.info.identity.name.clone());
        for (auto& binary : package.record.binaries) {
            for (const auto& link : package.links) {
                if (link.target != binary.target) continue;
                binary.destination = PathBuf::from(publication.destination.path())
                                         .join(link.physical_destination.as_path());
                binary.action      = link.action;
            }
            if (binary.destination.is_empty()) {
                for (const auto& entry : package.record.entries) {
                    if (! entry.origin.is_BuildArtifact() ||
                        entry.origin.as_BuildArtifact().target != binary.target) {
                        continue;
                    }
                    binary.destination = entry.destination.clone();
                    binary.action      = entry.action;
                    break;
                }
            }
            binaries.push(rstd::move(binary));
        }
        for (auto& link : package.links) {
            links.push(InstallLink {
                .target          = rstd::move(link.target),
                .destination     = PathBuf::from(publication.destination.path())
                                       .join(link.physical_destination.as_path()),
                .relative_target = rstd::move(link.relative_target),
                .action          = link.action,
            });
        }
        for (auto& entry : package.record.entries) entries.push(rstd::move(entry));
    }
    return InstallStoreSummary {
        .destination    = publication.destination.clone(),
        .managed_layout = rstd::move(publication.managed_layout),
        .packages       = rstd::move(packages),
        .binaries       = rstd::move(binaries),
        .entries        = rstd::move(entries),
        .links          = rstd::move(links),
    };
}

auto managed_install(InstallStoreRequest request) -> InstallStoreResult<InstallStoreSummary> {
    auto root   = rstd::move(request.destination).as_Managed().root;
    auto layout = rstd_try(create_install_layout(rstd::move(root)));
    auto destination =
        InstallDestination::Managed(InstallRoot { .path = layout.root.path.clone() });

    auto lock = rstd::fs::OpenOptions::make().read(true).write(true).create(true).open(
        layout.lock.as_path());
    if (lock.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "open install lock"_str, layout.lock.as_path(), rstd::move(lock).unwrap_err());
    }
    auto locked =
        rstd::fs::FileLock::acquire(rstd::move(lock).unwrap(), rstd::fs::FileLockMode::Exclusive);
    if (locked.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "lock install store"_str, layout.lock.as_path(), rstd::move(locked).unwrap_err());
    }
    auto lock_guard = rstd::move(locked).unwrap();
    rstd_try(recover_managed_transactions(layout));
    auto current     = rstd_try(load_managed_install_catalog(layout));
    auto publication = rstd_try(
        plan_install_publication(rstd::move(destination),
                                 Some(InstallLayout {
                                     .root = InstallRoot { .path = layout.root.path.clone() },
                                     .bin_directory      = layout.bin_directory.clone(),
                                     .packages_directory = layout.packages_directory.clone(),
                                     .lock               = layout.lock.clone(),
                                     .transactions       = layout.transactions.clone(),
                                 }),
                                 rstd::move(request.packages)));
    auto next = rstd_try(next_managed_catalog(layout, current, publication, request.force));

    auto transactions_created = rstd::fs::create_dir_all(layout.transactions.as_path());
    if (transactions_created.is_err()) {
        return store_io_failure<InstallStoreSummary>("create install transaction directory"_str,
                                                     layout.transactions.as_path(),
                                                     rstd::move(transactions_created).unwrap_err());
    }
    rstd_try(validate_directory(layout.transactions.as_path(), "install transaction"_str));
    auto transaction = transaction_path(layout.transactions.as_path());
    rstd_try(create_transaction(transaction.as_path()));

    auto items    = Vec<TransactionItem>::make();
    auto prepared = prepare_managed_payload(
        layout, current, publication, transaction.as_path(), request.force, items);
    if (prepared.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(rstd::move(prepared).unwrap_err());
    }
    prepared = prepare_managed_infos(layout, current, next, transaction.as_path(), items);
    if (prepared.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(rstd::move(prepared).unwrap_err());
    }
    auto orphans = prepare_managed_orphans(layout, current, next, items);
    if (orphans.is_err()) {
        (void)rstd::fs::remove_dir_all(transaction.as_path());
        return Err(rstd::move(orphans).unwrap_err());
    }
    auto executed = execute_transaction(layout.root.path.as_path(), transaction.as_path(), items);
    if (executed.is_err()) {
        auto error = rstd::move(executed).unwrap_err();
        if (! error.is_Transaction() || error.as_Transaction().rollback_failures.is_empty()) {
            (void)rstd::fs::remove_dir_all(transaction.as_path());
        }
        return Err(rstd::move(error));
    }
    auto removed = rstd::fs::remove_dir_all(transaction.as_path());
    if (removed.is_err()) {
        return store_io_failure<InstallStoreSummary>("clean install transaction"_str,
                                                     transaction.as_path(),
                                                     rstd::move(removed).unwrap_err());
    }
    (void)rstd::fs::remove_dir(layout.transactions.as_path());
    for (const auto& orphan : *orphans) {
        clean_empty_parents(layout.root.path.as_path(), orphan.as_path());
    }
    return Ok(summarize_publication(rstd::move(publication)));
}

auto prefix_install(InstallStoreRequest request) -> InstallStoreResult<InstallStoreSummary> {
    auto prefix = rstd::move(request.destination).as_Prefix().prefix;
    if (prefix.path.is_empty()) {
        return store_failure<InstallStoreSummary>("install prefix is required"_str);
    }
    auto created = rstd::fs::create_dir_all(prefix.path.as_path());
    if (created.is_err()) {
        return store_io_failure<InstallStoreSummary>(
            "create install prefix"_str, prefix.path.as_path(), rstd::move(created).unwrap_err());
    }
    auto canonical = rstd::fs::canonicalize(prefix.path.as_path());
    if (canonical.is_err()) {
        return store_io_failure<InstallStoreSummary>("resolve install prefix"_str,
                                                     prefix.path.as_path(),
                                                     rstd::move(canonical).unwrap_err());
    }
    auto destination =
        InstallDestination::Prefix(InstallPrefix { .path = rstd::move(canonical).unwrap() });
    auto publication = rstd_try(
        plan_install_publication(destination.clone(), None(), rstd::move(request.packages)));
    auto transaction =
        PathBuf::from(destination.path()).join(PathBuf::from(".lito-install"_str).as_path());
    auto stale = rstd_try(path_metadata(transaction.as_path()));
    if (stale.is_some()) {
        return store_failure<InstallStoreSummary>(rstd::format(
            "install prefix contains an unfinished transaction '{}'", transaction.as_path()));
    }
    rstd_try(create_transaction(transaction.as_path()));

    auto items = Vec<TransactionItem>::make();
    for (auto& package : publication.packages) {
        for (auto& entry : package.record.entries) {
            auto relative = entry.relative_destination.as_path();
            rstd_try(validate_parent_tree(destination.path(), relative));
            auto installed = PathBuf::from(destination.path()).join(relative);
            auto existing  = rstd_try(path_metadata(installed.as_path()));
            if (existing.is_some() && (! existing->is_file() || existing->is_symlink())) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install prefix destination '{}' is not a regular non-symlink file",
                    installed.as_path()));
            }
            auto staged = transaction.join(PathBuf::from("new"_str).as_path()).join(relative);
            auto staged_result = stage_entry(entry, staged.as_path());
            if (staged_result.is_err()) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return Err(rstd::move(staged_result).unwrap_err());
            }
            auto unchanged =
                existing.is_some() && rstd_try(same_file(staged.as_path(), installed.as_path()));
            if (existing.is_some() && ! unchanged && ! request.force) {
                (void)rstd::fs::remove_dir_all(transaction.as_path());
                return store_failure<InstallStoreSummary>(rstd::format(
                    "install prefix destination '{}' already exists; use --force to replace it",
                    installed.as_path()));
            }
            entry.action =
                unchanged ? InstallAction::Unchanged
                          : (existing.is_some() ? InstallAction::Replaced : InstallAction::Created);
            if (! unchanged) {
                add_transaction_item(items, relative, existing.is_some(), true);
            }
        }
    }
    auto executed = execute_transaction(destination.path(), transaction.as_path(), items);
    if (executed.is_err()) {
        auto error = rstd::move(executed).unwrap_err();
        if (! error.is_Transaction() || error.as_Transaction().rollback_failures.is_empty()) {
            (void)rstd::fs::remove_dir_all(transaction.as_path());
        }
        return Err(rstd::move(error));
    }
    auto removed = rstd::fs::remove_dir_all(transaction.as_path());
    if (removed.is_err()) {
        return store_io_failure<InstallStoreSummary>("clean prefix install transaction"_str,
                                                     transaction.as_path(),
                                                     rstd::move(removed).unwrap_err());
    }
    return Ok(summarize_publication(rstd::move(publication)));
}

} // namespace lito

export namespace lito
{

auto create_install_layout(InstallRoot root) -> InstallStoreResult<InstallLayout> {
    if (root.path.is_empty()) return store_failure<InstallLayout>("install root is required"_str);
    auto created = rstd::fs::create_dir_all(root.path.as_path());
    if (created.is_err()) {
        return store_io_failure<InstallLayout>(
            "create install root"_str, root.path.as_path(), rstd::move(created).unwrap_err());
    }
    auto canonical = rstd::fs::canonicalize(root.path.as_path());
    if (canonical.is_err()) {
        return store_io_failure<InstallLayout>(
            "resolve install root"_str, root.path.as_path(), rstd::move(canonical).unwrap_err());
    }
    root.path     = rstd::move(canonical).unwrap();
    auto packages = root.path.join(PathBuf::from("packages"_str).as_path());
    created       = rstd::fs::create_dir_all(packages.as_path());
    if (created.is_err()) {
        return store_io_failure<InstallLayout>("create install packages directory"_str,
                                               packages.as_path(),
                                               rstd::move(created).unwrap_err());
    }
    rstd_try(validate_directory(root.path.as_path(), "install root"_str));
    rstd_try(validate_directory(packages.as_path(), "install packages"_str));
    return Ok(InstallLayout {
        .root               = InstallRoot { .path = root.path.clone() },
        .bin_directory      = root.path.join(PathBuf::from("bin"_str).as_path()),
        .packages_directory = packages.clone(),
        .lock               = packages.join(PathBuf::from(".install.lock"_str).as_path()),
        .transactions       = packages.join(PathBuf::from(".transactions"_str).as_path()),
    });
}

auto install_artifacts(InstallStoreRequest request) -> InstallStoreResult<InstallStoreSummary> {
    if (request.destination.is_Managed()) return managed_install(rstd::move(request));
    return prefix_install(rstd::move(request));
}

} // namespace lito
