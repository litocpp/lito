export module lito.driver:install.result;

import rstd;
import lito.core;
import :build.result;
import :install.destination;
import :install.entry;
import :install.store.model;

using namespace rstd::prelude;

export namespace lito
{

struct InstallSummary {
    BuildSummary       build;
    InstallDestination destination;
    Vec<String>        packages;
    Vec<InstallBinary> binaries;
    Vec<InstallEntry>  entries;
    Vec<InstallLink>   links;
};

} // namespace lito
