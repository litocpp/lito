export module fixture.test.attach:registration;

import :internal;

static_assert(FIXTURE_ATTACH_PRIVATE == 17);

namespace
{

struct Registrar {
    Registrar() noexcept { fixture::attach::internal::register_test(); }
};

Registrar registrar;

} // namespace
