export module fixture.scanner.kinds;

export import :iface;
import :internal;

export auto scanner_value() -> int;

module :private;

auto scanner_private_value() -> int {
    return 1;
}
