#include <chrono>

#include <CLI/CLI.hpp>

#include "iyokan_plain.hpp"
#include "iyokan_tfhepp.hpp"

// Thanks to: https://faithandbrave.hateblo.jp/entry/2014/05/01/171631
std::vector<std::string> split(const std::string &input, char delimiter)
{
    std::istringstream stream(input);

    std::string field;
    std::vector<std::string> result;
    while (std::getline(stream, field, delimiter))
        result.push_back(field);
    return result;
}

int main(int argc, char **argv)
{
    CLI::App app{"Prallel FHE circuit evaluation engine."};

    Options opt;
    std::string enableROM;

    app.require_subcommand();
    enum class TYPE { PLAIN, TFHE } type;
    app.add_option("-l", opt.logicFile, "")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("-t", opt.numWorkers, "")->check(CLI::PositiveNumber);
    app.add_option("--enable-rom", enableROM, "");
    app.add_option("-i", opt.inputFile, "")
        ->required()
        ->check(CLI::ExistingFile);

    {
        CLI::App *plain = app.add_subcommand("plain", "");
        plain->fallthrough();
        plain->parse_complete_callback([&] { type = TYPE::PLAIN; });
        plain->add_option("-c", opt.numCycles, "");
    }

    {
        CLI::App *tfhe = app.add_subcommand("tfhe", "");
        tfhe->fallthrough();
        tfhe->add_option("-c", opt.numCycles, "")->required();
        tfhe->add_option("-o", opt.outputFile, "")->required();
        tfhe->parse_complete_callback([&] { type = TYPE::TFHE; });
    }

    CLI11_PARSE(app, argc, argv);

    if (!enableROM.empty())
        opt.romPorts = split(enableROM, ':');
    assert(opt.romPorts.empty() || opt.romPorts.size() == 4);

    AsyncThread::setNumThreads(opt.numWorkers);

    switch (type) {
    case TYPE::PLAIN:
        doPlain(opt);
        break;
    case TYPE::TFHE:
        doTFHE(opt);
        break;
    }
}
