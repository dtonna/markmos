// Backend Concept Test — compile-time validation
// @zero_virtual Concept check at compile time, zero runtime cost
#include <cassert>
#include "../core/mm_expected.hpp"

// Expected<T,E> satisfies ExpectedLike
using HExpected = Expected<unsigned, int>;
using VExpected = Expected<void, int>;

static_assert(ExpectedLike<HExpected, unsigned, int>);
static_assert(ExpectedLike<VExpected, void, int>);

// make_unexpected as return values (runtime test)
HExpected make_val() { return 42u; }
HExpected make_err_val() { return make_unexpected(-1); }
VExpected make_void() { return {}; }
VExpected make_err_void() { return make_unexpected(-2); }

int main() {
    auto a = make_val();
    assert(a.has_value());
    assert(*a == 42u);

    auto b = make_err_val();
    assert(!b.has_value());
    assert(b.error() == -1);

    auto c = make_void();
    assert(c.has_value());

    auto d = make_err_void();
    assert(!d.has_value());
    assert(d.error() == -2);

    (void)a; (void)b; (void)c; (void)d;
    return 0;
}
