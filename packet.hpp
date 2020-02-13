#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP

#include <fstream>
#include <iomanip>
#include <memory>

#include <elfio/elfio.hpp>

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>

#include <tfhe++.hpp>

#include <picojson.h>

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

struct KVSPPlainReqPacket {
    std::vector<uint8_t> rom, ram;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(rom, ram);
    }
};

struct KVSPPlainResPacket {
    std::vector<uint8_t> flags;
    std::vector<uint16_t> regs;
    std::vector<uint8_t> ram;
    int numCycles = -1;

    void printAsJSON(std::ostream& os)
    {
        picojson::array root;

        // Print flags.
        for (int i = 0; i < flags.size(); i++) {
            picojson::object item;
            item["type"] = picojson::value("flag");
            item["addr"] = picojson::value(static_cast<double>(i));
            item["byte"] = picojson::value(static_cast<bool>(flags.at(i)));
            root.emplace_back(item);
        }

        // Print regs.
        for (int i = 0; i < regs.size(); i++) {
            picojson::object item;
            item["type"] = picojson::value("reg");
            item["addr"] = picojson::value(static_cast<double>(i));
            item["byte"] = picojson::value(static_cast<double>(regs.at(i)));
            root.emplace_back(item);
        }

        // Print ram.
        for (int i = 0; i < ram.size(); i++) {
            picojson::object item;
            item["type"] = picojson::value("ram");
            item["addr"] = picojson::value(static_cast<double>(i));
            item["byte"] = picojson::value(static_cast<double>(ram.at(i)));
            root.emplace_back(item);
        }

        os << picojson::value(root);
    }

    void print(std::ostream& os)
    {
        assert(flags.size() == 1);
        assert(regs.size() == 16);
        assert(ram.size() == 512);

        // Print the number of cycles
        os << "#cycle\t";
        if (numCycles < 0)
            os << "unknown";
        else
            os << numCycles;
        os << std::endl << std::endl;

        // Print values of flags
        for (size_t i = 0; i < flags.size(); i++)
            os << "f" << i << "\t" << static_cast<int>(flags[i]) << std::endl;
        os << std::endl;

        // Print values of registers
        for (size_t reg = 0; reg < regs.size(); reg++)
            os << "x" << reg << "\t" << regs[reg] << std::endl;
        os << std::endl;

        // Print values of RAM
        const int WIDTH = 16;
        os << "      ";
        for (int i = 0; i < WIDTH; i++)
            os << std::setw(3) << i;
        os << std::endl;
        for (size_t addr = 0; addr < ram.size(); addr++) {
            if (addr % WIDTH == 0)
                os << std::setw(6) << std::setfill('0') << std::hex << addr
                   << " ";
            os << std::setw(2) << std::setfill('0') << std::hex
               << static_cast<int>(ram[addr]) << " ";
            if (addr % WIDTH == WIDTH - 1)
                os << std::endl;
        }
    }
};

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

inline std::vector<TFHEpp::TRLWElvl1> encryptTRLWElvl1(
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

inline KVSPPlainReqPacket parseELF(std::istream& is)
{
    ELFIO::elfio reader;
    reader.load(is);
    assert(reader.segments.size() != 0);

    std::vector<uint8_t> rom(512), ram(512);
    for (ELFIO::segment* pseg : reader.segments) {
        ELFIO::segment& seg = *pseg;
        auto size = seg.get_file_size();
        if (size == 0)
            continue;

        auto addr = seg.get_virtual_address();
        assert(0 <= addr && addr < rom.size() - size ||
               0x10000 <= addr && addr < 0x10000 + ram.size() - size);

        const char* src = seg.get_data();
        std::copy(src, src + size,
                  &(addr < 0x10000 ? rom.at(addr) : ram.at(addr - 0x10000)));
    }

    return KVSPPlainReqPacket{rom, ram};
}

inline KVSPPlainReqPacket parseELF(const std::string& path)
{
    std::ifstream ifs{path, std::ios::binary};
    assert(ifs);
    return parseELF(ifs);
}

struct KVSPReqPacket {
    std::shared_ptr<TFHEpp::GateKey> gateKey;
    std::vector<TFHEpp::TLWElvl0> rom, ram;

    std::shared_ptr<TFHEpp::CircuitKey> circuitKey;
    std::vector<TFHEpp::TRLWElvl1> romCk, ramCk;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(gateKey, rom, ram, circuitKey, romCk, ramCk);
    }

    KVSPReqPacket()
    {
    }

    KVSPReqPacket(std::shared_ptr<TFHEpp::GateKey> gateKey,
                  std::vector<TFHEpp::TLWElvl0> rom,
                  std::vector<TFHEpp::TLWElvl0> ram,
                  std::shared_ptr<TFHEpp::CircuitKey> ck,
                  std::vector<TFHEpp::TRLWElvl1> romCk)
        : gateKey(std::move(gateKey)),
          rom(std::move(rom)),
          ram(std::move(ram)),
          circuitKey(std::move(ck)),
          romCk(std::move(romCk))
    {
    }

    static KVSPReqPacket make(const TFHEpp::SecretKey& key,
                              const KVSPPlainReqPacket& plain)
    {
        auto gk = std::make_shared<TFHEpp::GateKey>(key);
        auto rom = encrypt(key, plain.rom);
        auto ram = encrypt(key, plain.ram);
        return KVSPReqPacket{gk, rom, ram, nullptr,
                             std::vector<TFHEpp::TRLWElvl1>{}};
    }

    static KVSPReqPacket makeWithCk(const TFHEpp::SecretKey& key,
                                    const KVSPPlainReqPacket& plain)
    {
        auto packet = make(key, plain);
        packet.circuitKey = std::make_shared<TFHEpp::CircuitKey>(key);
        packet.romCk = encryptTRLWElvl1(key, plain.rom);
        packet.ramCk = encryptRAMCk(key, plain.ram);
        return packet;
    }

private:
    static std::vector<TFHEpp::TRLWElvl1> encryptRAMCk(
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
};

struct KVSPResPacket {
    std::vector<TFHEpp::TLWElvl0> flags;
    std::vector<std::vector<TFHEpp::TLWElvl0>> regs;
    std::vector<TFHEpp::TLWElvl0> ram;
    std::vector<TFHEpp::TRLWElvl1> ramCk;
    int numCycles = -1;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(flags, regs, ram, numCycles, ramCk);
    }
};

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

inline std::vector<uint8_t> decrypt(const TFHEpp::SecretKey& key,
                                    const std::vector<TFHEpp::TRLWElvl1>& src)
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

inline KVSPPlainResPacket decrypt(const TFHEpp::SecretKey& key,
                                  const KVSPResPacket& src)
{
    std::vector<uint8_t> flags;
    for (auto&& encFlag : src.flags)
        flags.push_back(
            TFHEpp::bootsSymDecrypt(std::vector{encFlag}, key).at(0));

    std::vector<uint16_t> regs;
    for (auto&& encReg : src.regs) {
        std::vector<uint8_t> bytes = decrypt(key, encReg);
        assert(bytes.size() == 2);
        regs.push_back(bytes[0] | (bytes[1] << 8u));
    }

    std::vector<uint8_t> ram =
        src.ramCk.empty() ? decrypt(key, src.ram) : decrypt(key, src.ramCk);

    return KVSPPlainResPacket{flags, regs, ram, src.numCycles};
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
