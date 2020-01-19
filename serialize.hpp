#ifndef VIRTUALSECUREPLATFORM_IYOKAN_SERIALIZE_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_SERIALIZE_HPP

#include <fstream>

//
#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
#include <tfhe++.hpp>

namespace TFHEpp {
template <class Archive>
void serialize(Archive& ar, lweParams& src)
{
    ar(src.n, src.α, src.Nbit, src.N, src.l, src.Bgbit, src.Bg, src.αbk, src.t,
       src.basebit, src.αks, src.μ, src.nbarbit, src.nbar, src.lbar,
       src.Bgbitbar, src.Bgbar, src.αbklvl02, src.tbar, src.basebitlvl21,
       src.αprivks, src.μbar);
}

template <class Archive>
void serialize(Archive& ar, lweKey& src)
{
    ar(src.lvl0, src.lvl1, src.lvl2);
}

template <class Archive>
void serialize(Archive& ar, SecretKey& src)
{
    ar(src.key, src.params);
}

template <class Archive>
void serialize(Archive& ar, GateKey& src)
{
    ar(src.ksk, src.bkfftlvl01);
}
}  // namespace TFHEpp

template <class T>
void dump_as_binary(const T& obj, std::ostream& os)
{
    cereal::PortableBinaryOutputArchive ar{os};
    ar(obj);
}

template <class T>
void import_from_binary(T& obj, std::istream& is)
{
    cereal::PortableBinaryInputArchive ar{is};
    ar(obj);
}

template <class T>
void dump_as_binary(const T& obj, const std::string& filepath)
{
    std::ofstream ofs{filepath, std::ios_base::binary};
    assert(ofs && "Invalid filepath, maybe you don't have right permission?");
    dump_as_binary(obj, ofs);
}

template <class T>
void import_from_binary(T& obj, const std::string& filepath)
{
    std::ifstream ifs{filepath, std::ios_base::binary};
    assert(ifs && "Invalid filepath, maybe not exists?");
    import_from_binary(obj, ifs);
}

std::shared_ptr<TFHEpp::SecretKey> import_secret_key(
    const std::string& filepath)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    assert(sk);
    import_from_binary(*sk, filepath);
    return sk;
}

#endif
