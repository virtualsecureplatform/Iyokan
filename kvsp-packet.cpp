#include <cassert>
#include <string>

#include "packet.hpp"

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

void doPlain(std::string inputFileName, std::string outputFileName)
{
    auto reqPacket = parseELF(inputFileName);
    writeToArchive(outputFileName, reqPacket);
}

void doEnc(std::string keyFileName, std::string inputFileName,
           std::string outputFileName)
{
    std::shared_ptr<TFHEpp::SecretKey> sk = import_secret_key(keyFileName);
    auto plain = parseELF(inputFileName);
    KVSPReqPacket reqPacket{*sk, plain};
    writeToArchive(outputFileName, reqPacket);
}

void doDec(std::string keyFileName, std::string inputFileName)
{
    std::shared_ptr<TFHEpp::SecretKey> sk = import_secret_key(keyFileName);

    auto resPacket = readFromArchive<KVSPResPacket>(inputFileName);
    decrypt(*sk, resPacket).print(std::cout);
}

int main(int argc, char** argv)
{
    /*
       kvsp-packet genkey KEY-FILE
       kvsp-packet enc KEY-FILE INPUT-FILE OUTPUT-FILE
       kvsp-packet dec KEY-FILE INPUT-FILE
       kvsp-packet plain INPUT-FILE OUTPUT-FILE
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
    else if (subcommand == "plain") {
        assert(argc == 4 && "Invalid command-line arguments");
        doPlain(argv[2], argv[3]);
    }
    else {
        assert("Invalid command-line arguments");
    }

    return 0;
}
