// Cereal serialization support for __int128 types.
// TFHEpp's SecretKey contains Key<lvl3param> which uses __uint128_t,
// and cereal doesn't natively support __int128 types.
#ifndef VIRTUALSECUREPLATFORM_IYOKAN_INT128_CEREAL_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_INT128_CEREAL_HPP

#include <cereal/cereal.hpp>

#ifdef __SIZEOF_INT128__

namespace cereal {

template <class Archive>
inline void save(Archive& ar, __uint128_t const& val)
{
    uint64_t lo = static_cast<uint64_t>(val);
    uint64_t hi = static_cast<uint64_t>(val >> 64);
    ar(lo, hi);
}

template <class Archive>
inline void load(Archive& ar, __uint128_t& val)
{
    uint64_t lo, hi;
    ar(lo, hi);
    val = (static_cast<__uint128_t>(hi) << 64) | lo;
}

template <class Archive>
inline void save(Archive& ar, __int128_t const& val)
{
    save(ar, static_cast<__uint128_t>(val));
}

template <class Archive>
inline void load(Archive& ar, __int128_t& val)
{
    __uint128_t tmp;
    load(ar, tmp);
    val = static_cast<__int128_t>(tmp);
}

}  // namespace cereal

#endif
#endif
