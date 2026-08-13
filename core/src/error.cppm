export module lito.error;

import rstd;

using namespace rstd::prelude;

export namespace lito
{

using String  = rstd::string::String;
using PathBuf = rstd::path::PathBuf;

template<typename T>
using Vec = rstd::vec::Vec<T>;

} // namespace lito
