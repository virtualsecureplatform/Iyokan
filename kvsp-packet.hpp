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

    std::vector<uint8_t> ramA, ramB;
    for (int i = 0; i < ram.size(); i++) {
        if (i % 2 == 1)
            ramA.push_back(ram[i]);
        else
            ramB.push_back(ram[i]);
    }

    return PlainPacket{{{"ramA", ramA}, {"ramB", ramB}}, {{"rom", rom}}};
}

inline PlainPacket parseELF(const std::string& path)
{
    std::ifstream ifs{path, std::ios::binary};
    assert(ifs);
    return parseELF(ifs);
}

#endif
