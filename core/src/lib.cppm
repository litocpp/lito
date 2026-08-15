export module lito.core;

import rstd;

export import lito.system;
export import :config;
export import :configure_template;
export import :dependency;
export import :lock;
export import :lock.flatpak;
export import :manifest;
export import :package;
export import :source;
export import :workspace;

using namespace rstd::prelude;

export namespace lito
{

using PathBuf  = rstd::path::PathBuf;
using ErrorBox = Box<dyn<rstd::error::Error>>;

template<typename E>
auto erase_error(E error) -> ErrorBox {
    return ErrorBox::make(rstd::move(error));
}

} // namespace lito
