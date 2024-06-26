#include "packet.hpp"

#include <CLI/CLI.hpp>
#include <toml.hpp>

namespace {

enum class TYPE {
    GENKEY,
    GENEVALKEY,
    ENC,
    DEC,
    PACK,
    PACKET2TOML,
    TOML2PACKET,
    CONVERT,
    CONVERT_PLAIN,
} type;

using KeyVal = std::pair<std::string, std::string>;

std::optional<std::vector<KeyVal>> parseKVOpts(const std::string& src)
{
    const std::regex ptn{"([^:]+):([^\n]+)(?:\n|$)"};

    std::vector<KeyVal> ret;
    std::smatch mt;
    auto head = src.cbegin();
    while (std::regex_search(head, src.cend(), mt, ptn)) {
        assert(mt.size() == 3);

        if (mt[0].first != head)
            return std::nullopt;

        ret.emplace_back(mt[1], mt[2]);
        head = mt.suffix().first;
    }

    if (head == src.cend())
        return ret;
    return std::nullopt;
}

std::vector<Bit> readAsBitVec(std::istream& is)
{
    std::vector<Bit> ret;
    while (is) {
        int ch = is.get();
        if (ch == EOF)
            break;
        for (int i = 0; i < 8; i++) {
            ret.push_back((ch & 1u) != 0 ? 1_b : 0_b);
            ch >>= 1;
        }
    }
    return ret;
}

std::vector<Bit> readAsBitVec(const std::string& path)
{
    std::ifstream ifs{path, std::ios::binary};
    if (!ifs)
        error::die("Can't open: ", path);
    return readAsBitVec(ifs);
}

PlainPacket makePlainPacketFromCmdOptions(
    const std::optional<std::string>& ram,
    const std::optional<std::string>& rom,
    const std::optional<std::string>& bits)
{
    PlainPacket pkt;

    if (ram) {
        auto src = parseKVOpts(ram.value());
        if (!src)
            error::die("Invalid form of option --ram");
        for (auto&& [key, path] : *src)
            pkt.ram[key] = readAsBitVec(path);
    }
    if (rom) {
        auto src = parseKVOpts(rom.value());
        if (!src)
            error::die("Invalid form of option --rom");
        for (auto&& [key, path] : *src)
            pkt.rom[key] = readAsBitVec(path);
    }
    if (bits) {
        auto src = parseKVOpts(bits.value());
        if (!src)
            error::die("Invalid form of option --bits");
        for (auto&& [key, path] : *src)
            pkt.bits[key] = readAsBitVec(path);
    }

    return pkt;
}

void printPlainPacket(std::ostream& os, const PlainPacket& pkt)
{
    toml::value root = {
        {"cycles", pkt.numCycles ? *pkt.numCycles : -1},
        {"ram", std::vector<toml::table>{}},
        {"rom", std::vector<toml::table>{}},
        {"bits", std::vector<toml::table>{}},
    };

    auto bits2bytes = [](const std::vector<Bit>& bits) -> toml::array {
        toml::array ret;
        int ibit = 0, byte = 0;
        for (; ibit < bits.size(); ibit++) {
            byte |= (bits[ibit] == 1_b ? 1 : 0) << (ibit % 8);
            if (ibit % 8 == 7) {
                ret.push_back(byte);
                byte = 0;
            }
        }
        if (ibit % 8 != 0)
            ret.push_back(byte);
        return ret;
    };

    auto printEntry =
        [&root, &bits2bytes](
            const std::unordered_map<std::string, std::vector<Bit>>&
                name2bitvec,
            const std::string& entryName) {
            auto& v = toml::find<toml::array>(root, entryName);
            for (auto&& [name, bits] : name2bitvec)
                v.push_back(toml::table{
                    {"name", name},
                    {"size", bits.size()},
                    {"bytes", bits2bytes(bits)},
                });
        };

    printEntry(pkt.ram, "ram");
    printEntry(pkt.rom, "rom");
    printEntry(pkt.bits, "bits");

    os << std::setw(80) << root;
}

void doGenKeyTFHEpp(const std::string& out)
{
    TFHEpp::SecretKey sk;
    writeToArchive(out, sk);
}

void doGenEvalKeyTFHEpp(const std::string& in, const std::string& out)
{
    auto sk = readFromArchive<TFHEpp::SecretKey>(in);
    TFHEpp::EvalKey ek;
    ek.emplaceiksk<TFHEpp::lvl10param>(sk);
    ek.emplacebk<TFHEpp::lvl01param>(sk);
    ek.emplacebk2bkfft<TFHEpp::lvl01param>();
    ek.emplacebkfft<TFHEpp::lvl02param>(sk);
    ek.emplaceprivksk4cb<TFHEpp::lvl21param>(sk);
    writeToArchive(out, ek);
}

void doEnc(const std::string& key, const std::string& in,
           const std::string& out)
{
    auto sk = readFromArchive<TFHEpp::SecretKey>(key);
    auto pkt = readFromArchive<PlainPacket>(in);
    auto encPkt = pkt.encrypt(sk);
    writeToArchive(out, encPkt);
}

void doDec(const std::string& key, const std::string& in,
           const std::string& out)
{
    auto sk = readFromArchive<TFHEpp::SecretKey>(key);
    auto encPkt = readFromArchive<TFHEPacket>(in);
    auto pkt = encPkt.decrypt(sk);
    writeToArchive(out, pkt);
}

void doPack(const std::string& out, const PlainPacket& pkt)
{
    writeToArchive(out, pkt);
}

void doPacket2Toml(const std::string& in)
{
    auto pkt = readFromArchive<PlainPacket>(in);
    printPlainPacket(std::cout, pkt);
}

void doToml2Packet(const std::string& in, const std::string& out)
{
    const auto root = toml::parse(in);
    PlainPacket pkt;
    pkt.numCycles = toml::find_or<int>(root, "cycles", -1);

    auto parseEntries =
        [&root](std::unordered_map<std::string, std::vector<Bit>>& name2bitvec,
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
                    if (byte >= 256)
                        spdlog::warn(
                            "'bytes' field expects only <256 unsinged integer, "
                            "but got '{}'. Only the lower 8bits is used.",
                            byte);
                    for (int i = 0; i < 8; i++) {
                        if (it == v.end())
                            goto end;
                        *it++ = ((byte >> i) & 1u) != 0 ? 1_b : 0_b;
                    }
                }
            end:;  // ';' is necessary since label is followed by expression.
            }
        };

    parseEntries(pkt.ram, "ram");    // [[ram]]
    parseEntries(pkt.rom, "rom");    // [[rom]]
    parseEntries(pkt.bits, "bits");  // [[bits]]

    writeToArchive(out, pkt);
}

void doConvert(
    const std::vector<std::pair<std::string, std::string>>& inNameFilepath,
    const std::string& outFilepath, const std::vector<std::string>& rules)
{
    std::unordered_map<std::string, TFHEPacket> name2pkt;
    for (auto&& [name, path] : inNameFilepath) {
        name2pkt.emplace(name, readFromArchive<TFHEPacket>(path));
    }

    TFHEPacket out;
    std::regex re{
        R"((ram|rom|bits)\.([a-zA-Z0-9]+)\s*=\s*([a-zA-Z0-9]+)\.([a-zA-Z0-9]+))"};
    std::smatch m;
    for (const std::string& rule : rules) {
        if (!std::regex_match(rule, m, re))
            error::die("Invalid assignment: ", rule);
        if (m[1] == "ram") {
            out.ram.emplace(m[2], name2pkt.at(m[3]).ram.at(m[4]));
            out.ramInTLWE.emplace(m[2], name2pkt.at(m[3]).ramInTLWE.at(m[4]));
        }
        else if (m[1] == "rom") {
            out.rom.emplace(m[2], name2pkt.at(m[3]).rom.at(m[4]));
            out.romInTLWE.emplace(m[2], name2pkt.at(m[3]).romInTLWE.at(m[4]));
        }
        else {
            assert(m[1] == "bits");
            out.bits.emplace(m[2], name2pkt.at(m[3]).bits.at(m[4]));
        }
    }

    writeToArchive(outFilepath, out);
}

void doConvertPlain(
    const std::vector<std::pair<std::string, std::string>>& inNameFilepath,
    const std::string& outFilepath, const std::vector<std::string>& rules)
{
    std::unordered_map<std::string, PlainPacket> name2pkt;
    for (auto&& [name, path] : inNameFilepath) {
        name2pkt.emplace(name, readFromArchive<PlainPacket>(path));
    }

    PlainPacket out;
    std::regex re{
        R"((ram|rom|bits)\.([a-zA-Z0-9]+)\s*=\s*([a-zA-Z0-9]+)\.([a-zA-Z0-9]+))"};
    std::smatch m;
    for (const std::string& rule : rules) {
        if (!std::regex_match(rule, m, re))
            error::die("Invalid assignment: ", rule);
        if (m[1] == "ram") {
            out.ram.emplace(m[2], name2pkt.at(m[3]).ram.at(m[4]));
        }
        else if (m[1] == "rom") {
            out.rom.emplace(m[2], name2pkt.at(m[3]).rom.at(m[4]));
        }
        else {
            assert(m[1] == "bits");
            out.bits.emplace(m[2], name2pkt.at(m[3]).bits.at(m[4]));
        }
    }

    writeToArchive(outFilepath, out);
}

std::string type2str(TYPE t)
{
    switch (t) {
    case TYPE::GENKEY:
        return "genkey";
    case TYPE::GENEVALKEY:
        return "genevalkey";
    case TYPE::ENC:
        return "enc";
    case TYPE::DEC:
        return "dec";
    case TYPE::PACK:
        return "pack";
    case TYPE::PACKET2TOML:
        return "packet2toml";
    case TYPE::TOML2PACKET:
        return "toml2packet";
    case TYPE::CONVERT:
        return "convert";
    case TYPE::CONVERT_PLAIN:
        return "convert-plain";
    }

    assert(0);
}

}  // namespace

int main(int argc, char** argv)
{
    /*
       genkey     --type tfhepp --out secret.key
       genevalkey --in secret.key --out bootstrapping.key
       enc        --key secret.key --bkey bootstrapping.key \
                  --in packet.plain --out packet.enc
       dec        --key secret.key --in packet.enc --out packet.plain
       pack       --out packet.plain            \
                  --rom A:a.bin --rom C:c.bin \
                  --ram D:d.bin --ram E:e.bin \
                  --bits F:f.bin
       packet2toml --in packet.plain
       toml2packet --in packet.toml --out packet.plain
    */

    using namespace utility;
    using namespace std::chrono;

    error::initialize("iyokan-packet");

    CLI::App app{"A simple toolset for Iyokan's packet"};
    app.require_subcommand();

    std::string in = "", out = "", key = "", bkey = "";
    std::optional<std::string> rom, ram, bits;
    std::vector<std::pair<std::string, std::string>> ins;
    std::vector<std::string> rules;

    enum class KEY_TYPE {
        TFHEPP,
    } keyType;
    CLI::CheckedTransformer transKeyType{std::map<std::string, KEY_TYPE>{
        {"tfhepp", KEY_TYPE::TFHEPP},
    }};

    {
        CLI::App* sub = app.add_subcommand("genkey", "");
        sub->parse_complete_callback([&] { type = TYPE::GENKEY; });
        sub->add_option("-o,--out", out)->required();
        sub->add_option("--type", keyType)->required()->transform(transKeyType);
    }

    {
        CLI::App* sub = app.add_subcommand("genevalkey", "");
        sub->parse_complete_callback([&] { type = TYPE::GENEVALKEY; });
        sub->add_option("-i,--in", in)->required();
        sub->add_option("-o,--out", out)->required();
    }

    {
        CLI::App* sub = app.add_subcommand("enc", "");
        sub->parse_complete_callback([&] { type = TYPE::ENC; });
        sub->add_option("--key", key)->required();
        sub->add_option("-i,--in", in)->required();
        sub->add_option("-o,--out", out)->required();
    }

    {
        CLI::App* sub = app.add_subcommand("dec", "");
        sub->parse_complete_callback([&] { type = TYPE::DEC; });
        sub->add_option("--key", key)->required();
        sub->add_option("-i,--in", in)->required();
        sub->add_option("-o,--out", out)->required();
    }

    {
        CLI::App* sub = app.add_subcommand("pack", "");
        sub->parse_complete_callback([&] { type = TYPE::PACK; });
        sub->add_option("-o,--out", out)->required();
        sub->add_option("--rom", rom)->join();
        sub->add_option("--ram", ram)->join();
        sub->add_option("--bits", bits)->join();
    }

    {
        CLI::App* sub = app.add_subcommand("packet2toml", "");
        sub->parse_complete_callback([&] { type = TYPE::PACKET2TOML; });
        sub->add_option("-i,--in", in)->required()->check(CLI::ExistingFile);
    }

    {
        CLI::App* sub = app.add_subcommand("toml2packet", "");
        sub->parse_complete_callback([&] { type = TYPE::TOML2PACKET; });
        sub->add_option("-i,--in", in)->required()->check(CLI::ExistingFile);
        sub->add_option("-o,--out", out)->required();
    }

    {
        CLI::App* cnv = app.add_subcommand("convert", "");
        cnv->parse_complete_callback([&] { type = TYPE::CONVERT; });
        cnv->add_option("-i,--in", ins)
            ->required()
            ->check(CLI::Validator(CLI::ExistingFile).application_index(1));
        cnv->add_option("-o,--out", out)->required();
        cnv->add_option("RULES", rules);
    }

    {
        CLI::App* cnv = app.add_subcommand("convert-plain", "");
        cnv->parse_complete_callback([&] { type = TYPE::CONVERT_PLAIN; });
        cnv->add_option("-i,--in", ins)
            ->required()
            ->check(CLI::Validator(CLI::ExistingFile).application_index(1));
        cnv->add_option("-o,--out", out)->required();
        cnv->add_option("RULES", rules);
    }

    CLI11_PARSE(app, argc, argv);

    spdlog::info("Starting {}...", type2str(type));

    auto start = high_resolution_clock::now();
    switch (type) {
    case TYPE::GENKEY:
        assert(keyType == KEY_TYPE::TFHEPP);
        doGenKeyTFHEpp(out);
        break;

    case TYPE::GENEVALKEY:
        doGenEvalKeyTFHEpp(in, out);
        break;

    case TYPE::ENC:
        // FIXME: Assume the key type is TFHEpp
        doEnc(key, in, out);
        break;

    case TYPE::DEC:
        // FIXME: Assume the key type is TFHEpp
        doDec(key, in, out);
        break;

    case TYPE::PACK:
        doPack(out, makePlainPacketFromCmdOptions(ram, rom, bits));
        break;

    case TYPE::PACKET2TOML:
        doPacket2Toml(in);
        break;

    case TYPE::TOML2PACKET:
        doToml2Packet(in, out);
        break;

    case TYPE::CONVERT:
        doConvert(ins, out, rules);
        break;

    case TYPE::CONVERT_PLAIN:
        doConvertPlain(ins, out, rules);
        break;
    }
    auto end = std::chrono::high_resolution_clock::now();

    spdlog::info("{} done. ({} seconds)", type2str(type),
                 duration_cast<seconds>(end - start).count());
}
