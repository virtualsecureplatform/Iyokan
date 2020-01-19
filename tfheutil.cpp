#include "serialize.hpp"
//
#include <cassert>
#include <fstream>
#include <memory>
#include <random>

void doGenkey(const std::string& output_filepath)
{
    auto sk = std::make_shared<TFHEpp::SecretKey>();
    assert(sk);
    dump_as_binary(*sk, output_filepath);
}

void doCloudkey(const std::string& input_filepath,
                const std::string& output_filepath)
{
    // Load secret key
    auto sk = import_secret_key(input_filepath);

    // Get cloud key
    auto ck = std::make_shared<TFHEpp::GateKey>(*sk);
    assert(ck);

    // Export cloud key
    dump_as_binary(*ck, output_filepath);
}

void doEnc(const std::string& key_filepath, const std::string& input_filepath,
           const std::string& output_filepath, const std::string& nbits_str)
{
    // nbits may be negative, which means 'no limit about the number of bits'
    auto nbits = std::stoll(nbits_str);

    auto key = import_secret_key(key_filepath);

    std::ifstream ifs{input_filepath, std::ios_base::binary};
    assert(ifs && "Invalid filepath, maybe not exists?");
    std::ofstream ofs{output_filepath, std::ios_base::binary};
    assert(ofs && "Invalid filepath, maybe you don't have right permission?");

    while (nbits != 0) {
        int byte = ifs.get();
        if (byte == EOF)
            break;
        for (int i = 0; i < 8; i++, byte >>= 1) {
            uint8_t b = byte & 1;
            auto tlwe = TFHEpp::bootsSymEncrypt(std::vector{b}, *key).at(0);
            dump_as_binary(tlwe, ofs);
            if (--nbits == 0)
                break;
        }
    }

    assert(nbits <= 0 && "Too small input file");
}

void doDec(const std::string& key_filepath, const std::string& input_filepath,
           const std::string& output_filepath, const std::string& nbits_str)
{
    // nbits may be negative, which means 'no limit about the number of bits'
    auto nbits = std::stoll(nbits_str);

    auto key = import_secret_key(key_filepath);

    std::ifstream ifs{input_filepath, std::ios_base::binary};
    assert(ifs && "Invalid filepath, maybe not exists?");
    std::ofstream ofs{output_filepath, std::ios_base::binary};
    assert(ofs && "Invalid filepath, maybe you don't have right permission?");

    while (nbits != 0) {
        if (ifs.peek() == EOF)
            break;

        unsigned int byte = 0;
        for (int i = 0; i < 8; i++) {
            auto tlwe = std::make_shared<TFHEpp::TLWElvl0>();
            import_from_binary(*tlwe, ifs);

            uint8_t val =
                TFHEpp::bootsSymDecrypt(std::vector{*tlwe}, *key).at(0);
            byte |= (val & 1) << i;

            if (--nbits == 0)
                break;
        }
        ofs.put(byte);
    }

    assert(nbits <= 0 && "Too small input file");
}

int main(int argc, char** argv)
{
    /*
       tfheutil genkey KEY-FILE
       tfheutil cloudkey INPUT-KEY-FILE OUTPUT-FILE
       tfheutil enc KEY-FILE INPUT-FILE OUTPUT-FILE NUM-OF-BITS
       tfheutil dec KEY-FILE INPUT-FILE OUTPUT-FILE NUM-OF-BITS
    */

    assert(argc >= 3 && "Invalid command-line arguments");

    std::string subcommand = argv[1];
    if (subcommand == "genkey") {
        assert(argc == 3 && "Invalid command-line arguments");
        doGenkey(argv[2]);
    }
    else if (subcommand == "cloudkey") {
        assert(argc == 4 && "Invalid command-line arguments");
        doCloudkey(argv[2], argv[3]);
    }
    else if (subcommand == "enc") {
        assert(argc == 6 && "Invalid command-line arguments");
        doEnc(argv[2], argv[3], argv[4], argv[5]);
    }
    else if (subcommand == "dec") {
        assert(argc == 6 && "Invalid command-line arguments");
        doDec(argv[2], argv[3], argv[4], argv[5]);
    }
    else {
        assert("Invalid command-line arguments");
    }

    return 0;
}
