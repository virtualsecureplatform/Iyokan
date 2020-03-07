#ifndef VIRTUALSECUREPLATFORM_KVSP_PACKET_CUFHE_HPP
#define VIRTUALSECUREPLATFORM_KVSP_PACKET_CUFHE_HPP

#include <elfio/elfio.hpp>

#include "packet.hpp"

inline PlainPacket parseELF(std::istream& is)
{
    ELFIO::elfio reader;
    reader.load(is);
    assert(reader.segments.size() != 0);

    std::vector<uint8_t> rom(512, 0), ram(512, 0);
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

    std::vector<Bit> ramAbits, ramBbits;
    for (int i = 0; i < ram.size(); i++) {
        for (int j = 0; j < 8; j++) {
            if (i % 2 == 1)
                ramAbits.push_back(Bit((ram[i] >> j) & 1u));
            else
                ramBbits.push_back(Bit((ram[i] >> j) & 1u));
        }
    }

    std::vector<Bit> rombits;
    for (auto&& byte : rom) {
        for (int i = 0; i < 8; i++)
            rombits.push_back(Bit((byte >> i) & 1u));
    }

    return PlainPacket{{{"ramA", ramAbits}, {"ramB", ramBbits}},
                       {{"rom", rombits}},
                       {},
                       std::nullopt};
}

inline PlainPacket parseELF(const std::string& path)
{
    std::ifstream ifs{path, std::ios::binary};
    assert(ifs);
    return parseELF(ifs);
}

#endif
