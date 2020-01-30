#ifndef VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_PACKET_HPP

#include <iomanip>
#include <memory>

//
#include <tfhe++.hpp>

//
#include "serialize.hpp"

namespace detail {
uint32_t read32le(std::istream &is)
{
    uint32_t ret = 0;
    int ch = 0;
    ch = is.get();
    if (ch == EOF)
        throw std::runtime_error("Invalid input stream");
    ret |= (ch << 0);
    ch = is.get();
    if (ch == EOF)
        throw std::runtime_error("Invalid input stream");
    ret |= (ch << 8);
    ch = is.get();
    if (ch == EOF)
        throw std::runtime_error("Invalid input stream");
    ret |= (ch << 16);
    ch = is.get();
    if (ch == EOF)
        throw std::runtime_error("Invalid input stream");
    ret |= (ch << 24);

    return ret;
}

uint64_t read64le(std::istream &is)
{
    uint64_t l = read32le(is);
    uint64_t h = read32le(is);
    return (h << 32) | l;
}

void write16le(std::ostream &os, uint16_t val)
{
    char buf[2] = {static_cast<char>(val), static_cast<char>(val >> 8)};
    os.write(buf, sizeof(buf));
}

void write32le(std::ostream &os, uint32_t val)
{
    write16le(os, val & 0xffff);
    write16le(os, val >> 16);
}

void write64le(std::ostream &os, uint64_t val)
{
    write32le(os, val & 0xffffffff);
    write32le(os, val >> 32);
}
}  // namespace detail

struct KVSPReqPacket {
    std::shared_ptr<TFHEpp::GateKey> gateKey;
    std::vector<TFHEpp::TLWElvl0> rom, ram;

    static KVSPReqPacket readFrom(std::istream &is)
    {
        if (!is)
            throw std::runtime_error("Invalid input stream");

        // Check if the signature ('KVSP') is correct.
        char signature[4];
        is.read(signature, sizeof(signature));
        if (signature[0] != 'K' || signature[1] != 'V' || signature[2] != 'S' ||
            signature[3] != 'P')
            throw std::runtime_error("Invalid signature");

        // Check if the version is correct ('0').
        if (detail::read32le(is) != 0)
            throw std::runtime_error("Invalid version");

        // Get sizes of components.
        int64_t cloudKeySize = detail::read64le(is);
        int64_t romSize = detail::read64le(is);
        int64_t ramSize = detail::read64le(is);
        int64_t headerSize = is.tellg();

        // Read cloud key.
        auto gateKey = std::make_shared<TFHEpp::GateKey>();
        import_from_binary(*gateKey, is);

        if (!gateKey || is.tellg() != headerSize + cloudKeySize)
            throw std::runtime_error("Invalid cloud key");

        // Read encrypted ROM binary.
        std::vector<TFHEpp::TLWElvl0> rom;
        for (int64_t initialPos = is.tellg();
             is.tellg() < initialPos + romSize;) {
            TFHEpp::TLWElvl0 tlwe;
            import_from_binary(tlwe, is);
            rom.push_back(tlwe);
        }

        if (is.tellg() != headerSize + cloudKeySize + romSize)
            throw std::runtime_error("Invalid encrypted ROM data");

        // Read encrypted RAM binary.
        std::vector<TFHEpp::TLWElvl0> ram;
        for (int64_t initialPos = is.tellg();
             is.tellg() < initialPos + ramSize;) {
            TFHEpp::TLWElvl0 tlwe;
            import_from_binary(tlwe, is);
            ram.push_back(tlwe);
        }

        if (is.tellg() != headerSize + cloudKeySize + romSize + ramSize)
            throw std::runtime_error("Invalid encrypted RAM data");

        return KVSPReqPacket{gateKey, rom, ram};
    }
};

struct KVSPResPacket {
    std::vector<TFHEpp::TLWElvl0> flags;
    std::vector<std::vector<TFHEpp::TLWElvl0>> regs;
    std::vector<TFHEpp::TLWElvl0> ram;

    void writeTo(std::ostream &os)
    {
        if (!os)
            throw std::runtime_error("Invalid output stream");

        // Write the signature ('KVSP').
        char signature[4] = {'K', 'V', 'S', 'P'};
        os.write(signature, sizeof(signature));

        // Write the version ('0').
        uint32_t version = 0;
        detail::write32le(os, version);

        // Write the number of flags and regs.
        detail::write16le(os, flags.size());
        detail::write16le(os, regs.size());

        // Skip components' sizes; we will fill them later.
        os.seekp(24, std::ios_base::cur);
        uint64_t bodyBegin = os.tellp();

        // Write encrypted flags.
        for (auto &&sample : flags)
            dump_as_binary(sample, os);
        uint64_t flagsSize = static_cast<uint64_t>(os.tellp()) - bodyBegin;

        // Write encrypted regs.
        for (auto &&reg : regs)
            for (auto &&sample : reg)
                dump_as_binary(sample, os);
        uint64_t regsSize =
            static_cast<uint64_t>(os.tellp()) - (bodyBegin + flagsSize);

        // Write encrypted RAM.
        for (auto &&sample : ram)
            dump_as_binary(sample, os);
        uint64_t ramSize = static_cast<uint64_t>(os.tellp()) -
                           (bodyBegin + flagsSize + regsSize);
        auto bodyEnd = os.tellp();

        // Now we place the size of each component in the header.
        os.seekp(bodyBegin - 24);
        detail::write64le(os, flagsSize);
        detail::write64le(os, regsSize);
        detail::write64le(os, ramSize);
        os.seekp(bodyEnd);
    }
};

struct KVSPPlainReqPacket {
    std::vector<uint8_t> rom, ram;

    static KVSPPlainReqPacket readFrom(std::istream &is)
    {
        if (!is)
            throw std::runtime_error("Invalid input stream");

        // Check if the signature ('KVSP') is correct.
        char signature[4];
        is.read(signature, sizeof(signature));
        if (signature[0] != 'K' || signature[1] != 'V' || signature[2] != 'S' ||
            signature[3] != 'P')
            throw std::runtime_error("Invalid signature");

        // Check if the version is correct ('0').
        if (detail::read32le(is) != 0)
            throw std::runtime_error("Invalid version");

        // Get sizes of components.
        int64_t romSize = detail::read64le(is);
        int64_t ramSize = detail::read64le(is);
        int64_t headerSize = is.tellg();

        // Read ROM binary. Assume char is 8-bit wide.
        std::vector<uint8_t> rom(romSize);
        is.read(reinterpret_cast<char *>(rom.data()), romSize);

        // Read RAM binary. Assume char is 8-bit wide.
        std::vector<uint8_t> ram(ramSize);
        is.read(reinterpret_cast<char *>(ram.data()), ramSize);

        if (is.tellg() != headerSize + romSize + ramSize)
            throw std::runtime_error("Invalid encrypted RAM data");

        return KVSPPlainReqPacket{rom, ram};
    }
};

struct KVSPPlainResPacket {
    std::vector<uint8_t> flags;
    std::vector<uint16_t> regs;
    std::vector<uint8_t> ram;

    void print(std::ostream &os)
    {
        assert(flags.size() == 1);
        assert(regs.size() == 16);
        assert(ram.size() == 512);

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

#endif
