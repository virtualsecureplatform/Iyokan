#ifndef VIRTUALSECUREPLATFORM_PACKET_NT_HPP
#define VIRTUALSECUREPLATFORM_PACKET_NT_HPP

#include "error_nt.hpp"
#include "tfhepp_cufhe_wrapper.hpp"

#include <memory>

namespace nt {

enum class Bit : bool {};
inline constexpr Bit operator!(Bit l) noexcept
{
    return Bit(!static_cast<int>(l));
}
inline constexpr Bit operator|(Bit l, Bit r) noexcept
{
    return Bit(static_cast<int>(l) | static_cast<int>(r));
}
inline constexpr Bit operator&(Bit l, Bit r) noexcept
{
    return Bit(static_cast<int>(l) & static_cast<int>(r));
}
inline constexpr Bit operator^(Bit l, Bit r) noexcept
{
    return Bit(static_cast<int>(l) ^ static_cast<int>(r));
}
inline constexpr Bit operator|=(Bit& l, Bit r) noexcept
{
    return l = l | r;
}
inline constexpr Bit operator&=(Bit& l, Bit r) noexcept
{
    return l = l & r;
}
inline constexpr Bit operator^=(Bit& l, Bit r) noexcept
{
    return l = l ^ r;
}
inline Bit operator"" _b(unsigned long long x)
{
    return Bit(x != 0);
}
inline std::ostream& operator<<(std::ostream& os, const Bit& bit)
{
    os << (bit == 1_b ? 1 : 0);
    return os;
}

struct TFHEppBKey {
    std::shared_ptr<GateKeyFFT> gk;
    std::shared_ptr<CircuitKey> ck;

    TFHEppBKey()
    {
    }

    TFHEppBKey(const TFHEpp::SecretKey& sk)
        : gk(std::make_shared<GateKeyFFT>(sk)),
          ck(std::make_shared<CircuitKey>(sk))
    {
    }

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(gk, ck);
    }
};

struct TFHEPacket;

struct PlainPacket {
    std::unordered_map<std::string, std::vector<Bit>> ram;
    std::unordered_map<std::string, std::vector<Bit>> rom;
    std::unordered_map<std::string, std::vector<Bit>> bits;
    std::optional<int> numCycles;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(ram, rom, bits, numCycles);
    }

    bool operator==(const PlainPacket& rhs) const;
    TFHEPacket encrypt(const TFHEpp::SecretKey& key) const;

    static PlainPacket fromTOML(const std::string& filepath);
};

struct TFHEPacket {
    std::unordered_map<std::string, std::vector<TRLWELvl1>> ram;
    std::unordered_map<std::string, std::vector<TLWELvl0>> ramInTLWE;
    std::unordered_map<std::string, std::vector<TRLWELvl1>> rom;
    std::unordered_map<std::string, std::vector<TLWELvl0>> romInTLWE;
    std::unordered_map<std::string, std::vector<TLWELvl0>> bits;
    std::optional<int> numCycles;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(ram, ramInTLWE, rom, romInTLWE, bits, numCycles);
    }

    PlainPacket decrypt(const TFHEpp::SecretKey& key) const;
};

uint64_t bitvec2i(const std::vector<Bit>& src, int start = 0, int end = -1);
std::vector<TLWELvl0> encryptBits(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src);
std::vector<TRLWELvl1> encryptROM(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src);
std::vector<TLWELvl0> encryptROMInTLWE(const TFHEpp::SecretKey& key,
                                       const std::vector<Bit>& src);
std::vector<TRLWELvl1> encryptRAM(const TFHEpp::SecretKey& key,
                                  const std::vector<Bit>& src);
std::vector<TLWELvl0> encryptRAMInTLWE(const TFHEpp::SecretKey& key,
                                       const std::vector<Bit>& src);
std::vector<uint8_t> decrypt(const TFHEpp::SecretKey& key,
                             const std::vector<TLWELvl0>& src);
std::vector<Bit> decryptBits(const TFHEpp::SecretKey& key,
                             const std::vector<TLWELvl0>& src);
std::vector<Bit> decryptRAM(const TFHEpp::SecretKey& key,
                            const std::vector<TRLWELvl1>& src);
std::vector<Bit> decryptRAMInTLWE(const TFHEpp::SecretKey& key,
                                  const std::vector<TLWELvl0>& src);
std::vector<Bit> decryptROM(const TFHEpp::SecretKey& key,
                            const std::vector<TRLWELvl1>& src);
std::vector<Bit> decryptROMInTLWE(const TFHEpp::SecretKey& key,
                                  const std::vector<TLWELvl0>& src);
PlainPacket readPlainPacket(std::istream& is);
PlainPacket readPlainPacket(const std::string& path);
TFHEPacket readTFHEPacket(std::istream& is);
TFHEPacket readTFHEPacket(const std::string& path);
void readTFHEppBKey(TFHEppBKey& out, const std::string& path);
void writePlainPacket(std::ostream& os, const PlainPacket& pkt);
void writePlainPacket(const std::string& path, const PlainPacket& pkt);
void writeTFHEPacket(std::ostream& os, const TFHEPacket& pkt);
void writeTFHEPacket(const std::string& path, const TFHEPacket& pkt);
void writeTFHEppBKey(const std::string& path, const TFHEppBKey& bkey);

}  // namespace nt

#endif
