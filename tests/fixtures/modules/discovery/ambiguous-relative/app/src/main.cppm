import fixture.alpha;
import fixture.beta;
import fixture.error;

auto main() -> int {
    return alpha_value() + beta_value() + error_value() == 6 ? 0 : 1;
}
