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
        assert(0 <= addr && addr < rom.size() ||
               0x10000 <= addr && addr < 0x10000 + ram.size());

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

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(gateKey, rom, ram);
    }

    KVSPReqPacket()
    {
    }

    KVSPReqPacket(std::shared_ptr<TFHEpp::GateKey> gateKey,
                  std::vector<TFHEpp::TLWElvl0> rom,
                  std::vector<TFHEpp::TLWElvl0> ram)
        : gateKey(std::move(gateKey)), rom(std::move(rom)), ram(std::move(ram))
    {
    }

    KVSPReqPacket(const TFHEpp::SecretKey& key, const KVSPPlainReqPacket& plain)
        : gateKey(std::make_shared<TFHEpp::GateKey>(key)),
          rom(encrypt(key, plain.rom)),
          ram(encrypt(key, plain.ram))
    {
    }
};

struct KVSPResPacket {
    std::vector<TFHEpp::TLWElvl0> flags;
    std::vector<std::vector<TFHEpp::TLWElvl0>> regs;
    std::vector<TFHEpp::TLWElvl0> ram;
    int numCycles = -1;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(flags, regs, ram, numCycles);
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

inline KVSPPlainResPacket decrypt(const TFHEpp::SecretKey& key,
                                  const KVSPResPacket& src)
{
    std::vector<uint8_t> flags = decrypt(key, src.flags),
                         ram = decrypt(key, src.ram);

    std::vector<uint16_t> regs;
    for (auto&& encReg : src.regs) {
        std::vector<uint8_t> bytes = decrypt(key, encReg);
        assert(bytes.size() == 2);
        regs.push_back(bytes[0] | (bytes[1] << 8u));
    }

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
