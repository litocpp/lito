export module lito.system.environment_contract;

import rstd;
import lito.error;

using namespace rstd::prelude;

export namespace lito
{

struct ProcessEnvironmentSpec {
    Vec<PathBuf> append_path;

    auto clone() const -> ProcessEnvironmentSpec {
        return ProcessEnvironmentSpec {
            .append_path = as<rstd::clone::Clone>(append_path).clone(),
        };
    }
};

} // namespace lito
