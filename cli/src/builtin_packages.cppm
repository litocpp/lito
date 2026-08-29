export module lito.executable:builtin_packages;

import lito.core;

export auto lito_embedded_packages() noexcept -> lito::registry::EmbeddedPackageProvider;
