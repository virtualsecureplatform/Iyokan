#include "packet_nt.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <toml.hpp>

#include <fstream>

namespace {

template <class T>
void readFromArchive(T& res, std::istream& is)
{
    cereal::PortableBinaryInputArchive ar{is};
    ar(res);
}

template <class T>
void readFromArchive(T& res, const std::string& path)
{
    try {
        std::ifstream ifs{path, std::ios::binary};
        if (!ifs)
            ERR_DIE(
                "Can't open the file to read from; Maybe not found?: " << path);
        readFromArchive<T>(res, ifs);
    }
    catch (std::exception& ex) {
        ERR_DIE("Invalid archive: " << path);
    }
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
    try {
        std::ofstream ofs{path, std::ios::binary};
        if (!ofs)
            ERR_DIE("Can't open the file to write in; maybe not allowed?: "
                    << path);
        return writeToArchive(ofs, src);
    }
    catch (std::exception& ex) {
        ERR_DIE("Unable to write into archive: " << path << ": " << ex.what());
    }
}

template <class T>
bool isCorrectArchive(const std::string& path)
{
    try {
        std::ifstream ifs{path, std::ios::binary};
        if (!ifs)
            return false;
        T cont;
        readFromArchive<T>(cont, ifs);
        return true;
    }
    catch (std::exception& ex) {
        return false;
    }
}

}  // namespace

namespace nt {
uint64_t bitvec2i(const std::vector<Bit>& src, int start, int end)
{
    if (end == -1)
        end = src.size();
    assert(end - start < 64);
    uint64_t ret = 0;
    for (size_t i = start; i < end; i++)
        ret |= (static_cast<size_t>(src.at(i)) << (i - start));
    return ret;
}

std::vector<TLWELvl0> encryptBits(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src)
{
    std::vector<uint8_t> in;
    in.reserve(src.size());
    for (auto&& bit : src)
        in.push_back(bit == 1_b ? 1 : 0);
    return TFHEpp::bootsSymEncrypt(in, key);
}

std::vector<TRLWELvl1> encryptROM(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src)
{
    using P = TFHEpp::lvl1param;
    std::vector<TRLWELvl1> ret;

    PolyLvl1 pmu = {};
    for (size_t i = 0; i < src.size(); i++) {
        pmu[i % P::n] = src[i] == 1_b ? P::μ : -P::μ;
        if (i % P::n == P::n - 1)
            ret.push_back(
                TFHEpp::trlweSymEncrypt<Lvl1>(pmu, P::α, key.key.lvl1));
    }
    if (src.size() % P::n != 0)
        ret.push_back(TFHEpp::trlweSymEncrypt<Lvl1>(pmu, P::α, key.key.lvl1));

    return ret;
}

std::vector<TLWELvl0> encryptROMInTLWE(const TFHEpp::SecretKey& key,
                                       const std::vector<Bit>& src)
{
    return encryptBits(key, src);
}

std::vector<TRLWELvl1> encryptRAM(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src)
{
    using P = TFHEpp::lvl1param;
    std::vector<TRLWELvl1> ret;

    for (auto&& bit : src) {
        PolyLvl1 pmu = {};
        pmu[0] = bit == 1_b ? P::μ : -P::μ;
        ret.push_back(TFHEpp::trlweSymEncrypt<Lvl1>(pmu, P::α, key.key.lvl1));
    }

    return ret;
}

std::vector<TLWELvl0> encryptRAMInTLWE(const TFHEpp::SecretKey& key,
                                       const std::vector<Bit>& src)
{
    return encryptBits(key, src);
}

std::vector<uint8_t> decrypt(const TFHEpp::SecretKey& key,
                             const std::vector<TLWELvl0>& src)
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

std::vector<Bit> decryptBits(const TFHEpp::SecretKey& key,
                             const std::vector<TLWELvl0>& src)
{
    auto bitvals = TFHEpp::bootsSymDecrypt(src, key);
    std::vector<Bit> bits;
    for (auto&& bitval : bitvals)
        bits.push_back(bitval != 0 ? 1_b : 0_b);
    return bits;
}

std::vector<Bit> decryptRAM(const TFHEpp::SecretKey& key,
                            const std::vector<TRLWELvl1>& src)
{
    std::vector<Bit> ret;
    for (auto&& encbit : src) {
        uint8_t bitval =
            TFHEpp::trlweSymDecrypt<Lvl1>(encbit, key.key.lvl1).at(0);
        ret.push_back(bitval != 0 ? 1_b : 0_b);
    }

    return ret;
}

std::vector<Bit> decryptRAMInTLWE(const TFHEpp::SecretKey& key,
                                  const std::vector<TLWELvl0>& src)
{
    return decryptBits(key, src);
}

std::vector<Bit> decryptROM(const TFHEpp::SecretKey& key,
                            const std::vector<TRLWELvl1>& src)
{
    std::vector<Bit> ret;
    for (auto&& encblk : src) {
        auto blk = TFHEpp::trlweSymDecrypt<Lvl1>(encblk, key.key.lvl1);
        for (uint8_t bitval : blk)
            ret.push_back(bitval != 0 ? 1_b : 0_b);
    }

    return ret;
}

std::vector<Bit> decryptROMInTLWE(const TFHEpp::SecretKey& key,
                                  const std::vector<TLWELvl0>& src)
{
    return decryptBits(key, src);
}

bool PlainPacket::operator==(const PlainPacket& rhs) const
{
    // Check if member variables of *this and rhs are equal.
    // If we used C++20, we could make C++ compilers derive this code by using
    // '= default'!
    return ram == rhs.ram && rom == rhs.rom && bits == rhs.bits &&
           numCycles == rhs.numCycles;
}

TFHEPacket PlainPacket::encrypt(const TFHEpp::SecretKey& key) const
{
    TFHEPacket tfhe{{}, {}, {}, {}, {}, numCycles};

    // Encrypt RAM
    for (auto&& [name, src] : ram) {
        if (auto [it, inserted] = tfhe.ram.emplace(name, encryptRAM(key, src));
            !inserted)
            ERR_DIE("Invalid PlainPacket. Duplicate ram's key: " << name);
        if (auto [it, inserted] =
                tfhe.ramInTLWE.emplace(name, encryptRAMInTLWE(key, src));
            !inserted)
            ERR_DIE("Invalid PlainPacket. Duplicate ram's key: " << name);
    }

    // Encrypt ROM
    for (auto&& [name, src] : rom) {
        if (auto [it, inserted] = tfhe.rom.emplace(name, encryptROM(key, src));
            !inserted)
            ERR_DIE("Invalid PlainPacket. Duplicate rom's key: " << name);
        if (auto [it, inserted] =
                tfhe.romInTLWE.emplace(name, encryptROMInTLWE(key, src));
            !inserted)
            ERR_DIE("Invalid PlainPacket. Duplicate rom's key: " << name);
    }

    // Encrypt bits
    for (auto&& [name, src] : bits) {
        auto [it, inserted] = tfhe.bits.emplace(name, encryptBits(key, src));
        if (!inserted)
            ERR_DIE("Invalid PlainPacket. Duplicate bits's key: " << name);
    }

    return tfhe;
}

PlainPacket PlainPacket::fromTOML(const std::string& filepath)
{
    // FIXME: iyokan-packet should use this function
    const auto root = toml::parse(filepath);
    int numCycles = toml::find_or<int>(root, "cycles", -1);
    std::unordered_map<std::string, std::vector<Bit>> ram, rom, bits;

    auto parseEntries = [&root](
                            std::unordered_map<std::string, std::vector<Bit>>&
                                name2bitvec,
                            const std::string& entryName) {
        if (!root.contains(entryName))
            return;
        const auto tables =
            toml::find<std::vector<toml::value>>(root, entryName);
        for (const auto& table : tables) {
            const auto name = toml::find<std::string>(table, "name");
            const auto size = toml::find<size_t>(table, "size");
            const auto bytes =
                toml::find<std::vector<uint64_t>>(table, "bytes");

            std::vector<Bit>& v = name2bitvec[name];
            v.resize(size, 0_b);
            auto it = v.begin();
            for (uint64_t byte : bytes) {
                if (byte >= 0xffu)
                    LOG_S(WARNING)
                        << "'bytes' field expects only <256 unsinged integer, "
                           "but got '"
                        << byte << "'. Only the lower 8bits is used.";
                for (int i = 0; i < 8; i++) {
                    if (it == v.end())
                        goto end;
                    *it++ = ((byte >> i) & 1u) != 0 ? 1_b : 0_b;
                }
            }
        end:;  // ';' is necessary since label is followed by expression.
        }
    };

    parseEntries(ram, "ram");    // [[ram]]
    parseEntries(rom, "rom");    // [[rom]]
    parseEntries(bits, "bits");  // [[bits]]

    return PlainPacket{ram, rom, bits, numCycles};
}

PlainPacket TFHEPacket::decrypt(const TFHEpp::SecretKey& key) const
{
    PlainPacket plain{{}, {}, {}, numCycles};

    // Decrypt RAM
    for (auto&& [name, trlwes] : ram)
        plain.ram.emplace(name, decryptRAM(key, trlwes));
    for (auto&& [name, tlwes] : ramInTLWE)
        plain.ram.emplace(name, decryptRAMInTLWE(key, tlwes));

    // Decrypt ROM
    for (auto&& [name, trlwes] : rom)
        plain.rom.emplace(name, decryptROM(key, trlwes));
    for (auto&& [name, tlwes] : romInTLWE)
        plain.rom.emplace(name, decryptROMInTLWE(key, tlwes));

    // Decrypt bits
    for (auto&& [name, tlwes] : bits) {
        auto [it, inserted] = plain.bits.emplace(name, decryptBits(key, tlwes));
        if (!inserted)
            ERR_DIE("Invalid TFHEPacket. Duplicate bits's key: " << name);
    }

    return plain;
}

PlainPacket readPlainPacket(std::istream& is)
{
    return readFromArchive<PlainPacket>(is);
}

PlainPacket readPlainPacket(const std::string& path)
{
    return readFromArchive<PlainPacket>(path);
}

TFHEPacket readTFHEPacket(std::istream& is)
{
    return readFromArchive<TFHEPacket>(is);
}

TFHEPacket readTFHEPacket(const std::string& path)
{
    return readFromArchive<TFHEPacket>(path);
}

void writePlainPacket(std::ostream& os, const PlainPacket& pkt)
{
    writeToArchive(os, pkt);
}

void writePlainPacket(const std::string& path, const PlainPacket& pkt)
{
    writeToArchive(path, pkt);
}

void writeTFHEPacket(std::ostream& os, const TFHEPacket& pkt)
{
    writeToArchive(os, pkt);
}

}  // namespace nt
