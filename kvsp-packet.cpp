#include <cassert>
#include <string>

#include <picojson.h>

#include "kvsp-packet.hpp"

void print(const PlainPacket& packet, std::ostream& os)
{
    // Save io state.
    std::ios fmt{nullptr};
    fmt.copyfmt(os);

    // Print # of cycles
    os << "#cycle\t";
    if (packet.numCycles)
        os << *packet.numCycles;
    else
        os << "unknown";
    os << std::endl << std::endl;

    // Print flags
    os << "f0\t" << utility::u8vec2i(packet.bits.at("finflag")) << std::endl;

    os << std::endl;

    // Print regs
    for (int i = 0; i < 16; i++)
        os << "x" << i << "\t"
           << utility::u8vec2i(packet.bits.at(utility::fok("reg_x", i)))
           << std::endl;

    os << std::endl;

    // Print RAM
    auto &ramA = packet.ram.at("ramA"), &ramB = packet.ram.at("ramB");
    const int WIDTH = 16;
    assert(ramA.size() == ramB.size());

    os << "      ";
    for (int i = 0; i < WIDTH; i++)
        os << std::setw(3) << i;
    os << std::endl;
    for (size_t addr = 0; addr < ramA.size() * 2; addr++) {
        if (addr % WIDTH == 0)
            os << std::setw(6) << std::setfill('0') << std::hex << addr << " ";
        os << std::setw(2) << std::setfill('0') << std::hex
           << static_cast<int>((addr % 2 == 1 ? ramA : ramB).at(addr / 2))
           << " ";
        if (addr % WIDTH == WIDTH - 1)
            os << std::endl;
    }

    // FIXME: Print ROM

    os.copyfmt(fmt);
}

void printAsJSON(const PlainPacket& packet, std::ostream& os)
{
    std::vector<std::tuple<std::string, double, std::string>> srcTable = {
        {"flag", 0, "finflag"}, {"reg", 0, "reg_x0"},   {"reg", 1, "reg_x1"},
        {"reg", 2, "reg_x2"},   {"reg", 3, "reg_x3"},   {"reg", 4, "reg_x4"},
        {"reg", 5, "reg_x5"},   {"reg", 6, "reg_x6"},   {"reg", 7, "reg_x7"},
        {"reg", 8, "reg_x8"},   {"reg", 9, "reg_x9"},   {"reg", 10, "reg_x10"},
        {"reg", 11, "reg_x11"}, {"reg", 12, "reg_x12"}, {"reg", 13, "reg_x13"},
        {"reg", 14, "reg_x14"}, {"reg", 15, "reg_x15"}};

    picojson::array root;

    // Print bits.
    for (auto&& [type, addr, byteSrc] : srcTable) {
        picojson::object item;
        item["type"] = picojson::value(type);
        item["addr"] = picojson::value(addr);
        item["byte"] = picojson::value(
            static_cast<double>(utility::u8vec2i(packet.bits.at(byteSrc))));
        root.emplace_back(item);
    }

    // Print ram.
    auto &ramA = packet.ram.at("ramA"), &ramB = packet.ram.at("ramB");
    assert(ramA.size() == ramB.size());
    for (int i = 0; i < ramA.size() * 2; i++) {
        picojson::object item;
        item["type"] = picojson::value("ram");
        item["addr"] = picojson::value(static_cast<double>(i));
        item["byte"] = picojson::value(
            static_cast<double>((i % 2 == 1 ? ramA : ramB).at(i / 2)));
        root.emplace_back(item);
    }

    os << picojson::value(root) << std::endl;
}

std::shared_ptr<TFHEpp::SecretKey> import_secret_key(
    const std::string& filepath)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    assert(sk);
    readFromArchive(*sk, filepath);
    return sk;
}

void doGenkey(const std::string& output_filepath)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    assert(sk);
    writeToArchive(output_filepath, *sk);
}

void doPlainPack(std::string inputFileName, std::string outputFileName)
{
    auto reqPacket = parseELF(inputFileName);
    writeToArchive(outputFileName, reqPacket);
}

void doPlainUnpack(std::string inputFileName)
{
    auto packet = readFromArchive<PlainPacket>(inputFileName);
    print(packet, std::cout);
}

void doEnc(std::string keyFileName, std::string inputFileName,
           std::string outputFileName)
{
    std::shared_ptr<TFHEpp::SecretKey> sk = import_secret_key(keyFileName);
    auto plain = parseELF(inputFileName);
    auto reqPacket = parseELF(inputFileName).encrypt(*sk);
    writeToArchive(outputFileName, reqPacket);
}

void doDec(std::string keyFileName, std::string inputFileName)
{
    std::shared_ptr<TFHEpp::SecretKey> sk = import_secret_key(keyFileName);
    auto resPacket = readFromArchive<TFHEPacket>(inputFileName).decrypt(*sk);
    print(resPacket, std::cout);
}

void doDecJSON(std::string keyFileName, std::string inputFileName)
{
    std::shared_ptr<TFHEpp::SecretKey> sk = import_secret_key(keyFileName);
    auto resPacket = readFromArchive<TFHEPacket>(inputFileName).decrypt(*sk);
    printAsJSON(resPacket, std::cout);
}

int main(int argc, char** argv)
{
    /*
       kvsp-packet genkey KEY-FILE
       kvsp-packet enc KEY-FILE INPUT-FILE OUTPUT-FILE
       kvsp-packet dec KEY-FILE INPUT-FILE
       kvsp-packet dec-json KEY-FILE INPUT-FILE
       kvsp-packet plain-pack INPUT-FILE OUTPUT-FILE
       kvsp-packet plain-unpack INPUT-FILE
    */

    assert(argc >= 3 && "Invalid command-line arguments");

    std::string subcommand = argv[1];
    if (subcommand == "genkey") {
        assert(argc == 3 && "Invalid command-line arguments");
        doGenkey(argv[2]);
    }
    else if (subcommand == "enc") {
        assert(argc == 5 && "Invalid command-line arguments");
        doEnc(argv[2], argv[3], argv[4]);
    }
    else if (subcommand == "dec") {
        assert(argc == 4 && "Invalid command-line arguments");
        doDec(argv[2], argv[3]);
    }
    else if (subcommand == "dec-json") {
        assert(argc == 4 && "Invalid command-line arguments");
        doDecJSON(argv[2], argv[3]);
    }
    else if (subcommand == "plain-pack") {
        assert(argc == 4 && "Invalid command-line arguments");
        doPlainPack(argv[2], argv[3]);
    }
    else if (subcommand == "plain-unpack") {
        assert(argc == 3 && "Invalid command-line arguments");
        doPlainUnpack(argv[2]);
    }
    else {
        assert("Invalid command-line arguments");
    }

    return 0;
}
