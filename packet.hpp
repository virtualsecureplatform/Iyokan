#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP

#include <fstream>
#include <iomanip>
#include <memory>
#include <regex>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>

#include <tfhe++.hpp>

#include "error.hpp"
#include "utility.hpp"

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

template <class Archive>
void serialize(Archive& ar, CircuitKey& src)
{
    ar(src.privksk, src.bkfftlvl02);
}
}  // namespace TFHEpp

inline std::vector<TFHEpp::TLWElvl0> encrypt(const TFHEpp::SecretKey& key,
                                             const std::vector<uint8_t>& src)
{
    std::vector<TFHEpp::TLWElvl0> ret;
    for (uint8_t v : src) {
        for (uint32_t i = 0; i < 8; i++) {
            uint8_t b = (v >> i) & 1u;
            ret.push_back(TFHEpp::bootsSymEncrypt(std::vector{b}, key).at(0));
        }
    }
    return ret;
}

inline std::vector<TFHEpp::TRLWElvl1> encryptROM(
    const TFHEpp::SecretKey& key, const std::vector<uint8_t>& src)
{
    const TFHEpp::lweParams& params = key.params;
    std::vector<TFHEpp::TRLWElvl1> ret;

    for (size_t i = 0; i < src.size() / (params.N / 8); i++) {
        TFHEpp::Polynomiallvl1 pmu;
        for (size_t j = 0; j < params.N; j++) {
            size_t offset = i * params.N + j;
            size_t byteOffset = offset / 8, bitOffset = offset % 8;
            uint8_t val = byteOffset < src.size()
                              ? (src[byteOffset] >> bitOffset) & 1u
                              : 0;
            pmu[j] = val ? params.μ : -params.μ;
        }
        ret.push_back(
            TFHEpp::trlweSymEncryptlvl1(pmu, params.αbk, key.key.lvl1));
    }

    return ret;
}

inline std::vector<TFHEpp::TRLWElvl1> encryptRAM(
    const TFHEpp::SecretKey& key, const std::vector<uint8_t>& src)
{
    const TFHEpp::lweParams& params = key.params;
    std::vector<TFHEpp::TRLWElvl1> ret;

    for (size_t i = 0; i < src.size(); i++) {
        for (size_t bit = 0; bit < 8; bit++) {
            TFHEpp::Polynomiallvl1 pmu = {};
            uint8_t val = (src[i] >> bit) & 1u;
            pmu[0] = val ? params.μ : -params.μ;
            ret.push_back(
                TFHEpp::trlweSymEncryptlvl1(pmu, params.α, key.key.lvl1));
        }
    }

    return ret;
}

inline std::vector<uint8_t> decrypt(const TFHEpp::SecretKey& key,
                                    const std::vector<TFHEpp::TLWElvl0>& src)
{
    std::vector<uint8_t> ret;
    for (auto it = src.begin(); it != src.end();) {
        uint8_t byte = 0;
        for (uint32_t i = 0; i < 8; i++, ++it) {
            assert(it != src.end());
            uint8_t val = TFHEpp::bootsSymDecrypt(std::vector{*it}, key).at(0);
            byte |= (val & 1u) << i;
        }
        ret.push_back(byte);
    }
    return ret;
}

inline std::vector<uint8_t> decryptRAM(
    const TFHEpp::SecretKey& key, const std::vector<TFHEpp::TRLWElvl1>& src)
{
    std::vector<uint8_t> ret;
    for (auto it = src.begin(); it != src.end();) {
        uint8_t byte = 0;
        for (uint32_t i = 0; i < 8; i++, ++it) {
            assert(it != src.end());
            uint8_t val = TFHEpp::trlweSymDecryptlvl1(*it, key.key.lvl1).at(0);
            byte |= (val & 1u) << i;
        }
        ret.push_back(byte);
    }
    return ret;
}

struct TFHEPacket;

struct PlainPacket {
    std::unordered_map<std::string, std::vector<uint8_t>> ram;  // byte by byte
    std::unordered_map<std::string, std::vector<uint8_t>> rom;  // byte by byte
    std::unordered_map<std::string, std::vector<uint8_t>> bits;
    std::optional<int> numCycles;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(ram, rom, bits, numCycles);
    }

    inline TFHEPacket encrypt(const TFHEpp::SecretKey& key) const;
};

struct TFHEPacket {
    std::shared_ptr<TFHEpp::GateKey> gk;
    std::shared_ptr<TFHEpp::CircuitKey> ck;
    std::unordered_map<std::string, std::vector<TFHEpp::TRLWElvl1>> ram;
    std::unordered_map<std::string, std::vector<TFHEpp::TRLWElvl1>> rom;
    std::unordered_map<std::string, std::vector<TFHEpp::TLWElvl0>> bits;
    std::optional<int> numCycles;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(gk, ck, ram, rom, bits, numCycles);
    }

    inline PlainPacket decrypt(const TFHEpp::SecretKey& key) const;
};

TFHEPacket PlainPacket::encrypt(const TFHEpp::SecretKey& key) const
{
    TFHEPacket tfhe{std::make_shared<TFHEpp::GateKey>(key),
                    std::make_shared<TFHEpp::CircuitKey>(key),
                    {},
                    {},
                    {}};
    // Encrypt RAM
    for (auto&& [name, src] : ram) {
        auto [it, inserted] = tfhe.ram.emplace(name, encryptRAM(key, src));
        if (!inserted)
            error::die("Invalid PlainPacket. Duplicate ram's key: ", name);
    }

    // Encrypt ROM
    for (auto&& [name, src] : rom) {
        auto [it, inserted] = tfhe.rom.emplace(name, encryptROM(key, src));
        if (!inserted)
            error::die("Invalid PlainPacket. Duplicate rom's key: ", name);
    }

    // FIXME: Encrypt bits

    return tfhe;
}

PlainPacket TFHEPacket::decrypt(const TFHEpp::SecretKey& key) const
{
    PlainPacket plain{{}, {}, {}, numCycles};

    // Decrypt RAM
    for (auto&& [name, trlwes] : ram) {
        auto [it, inserted] = plain.ram.emplace(name, decryptRAM(key, trlwes));
        if (!inserted)
            error::die("Invalid TFHEPacket. Duplicate trlweData's key: ", name);
    }

    // FIXME: Decrypt ROM

    // Decrypt bits
    for (auto&& [name, tlwes] : bits) {
        auto pbits = TFHEpp::bootsSymDecrypt(tlwes, key);
        auto [it, inserted] = plain.bits.emplace(name, pbits);
        if (!inserted)
            error::die("Invalid TFHEPacket. Duplicate tlweData's key: ", name);
    }

    return plain;
}

template <class T>
void readFromArchive(T& res, std::istream& is)
{
    cereal::PortableBinaryInputArchive ar{is};
    ar(res);
}

template <class T>
void readFromArchive(T& res, const std::string& path)
{
    std::ifstream ifs{path, std::ios::binary};
    assert(ifs && "Can't open the file to read from; maybe not found?");
    readFromArchive<T>(res, ifs);
}

template <class T>
T readFromArchive(std::istream& is)
{
    T ret;
    readFromArchive(ret, is);
    return ret;
}

template <class T>
T readFromArchive(const std::string& path)
{
    T ret;
    readFromArchive(ret, path);
    return ret;
}

template <class T>
void writeToArchive(std::ostream& os, const T& src)
{
    cereal::PortableBinaryOutputArchive ar{os};
    ar(src);
}

template <class T>
void writeToArchive(const std::string& path, const T& src)
{
    std::ofstream ofs{path, std::ios::binary};
    assert(ofs && "Can't open the file to write in; maybe not allowed?");
    return writeToArchive(ofs, src);
}

#endif
