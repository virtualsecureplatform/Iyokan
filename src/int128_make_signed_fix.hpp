// Workaround for GCC < 14 where std::make_signed<__int128> is not supported.
// TFHEpp's lvl3param uses __uint128_t as its T type, and bootsSymEncrypt
// uses std::make_signed_t<typename P::T>, which fails without this fix.
#ifndef VIRTUALSECUREPLATFORM_IYOKAN_INT128_MAKE_SIGNED_FIX_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_INT128_MAKE_SIGNED_FIX_HPP

#include <type_traits>

#if defined(__SIZEOF_INT128__) && !defined(__clang__)
namespace std {
template <>
struct make_signed<__uint128_t> {
    using type = __int128_t;
};
template <>
struct make_signed<__int128_t> {
    using type = __int128_t;
};
template <>
struct make_unsigned<__int128_t> {
    using type = __uint128_t;
};
template <>
struct make_unsigned<__uint128_t> {
    using type = __uint128_t;
};
}  // namespace std
#endif

#endif
