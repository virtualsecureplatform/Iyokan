#include "iyokan_nt.hpp"

#include <fmt/format.h>
#include <toml.hpp>

#include <regex>

namespace {
std::vector<std::string> regexMatch(const std::string& text,
                                    const std::regex& re)
{
    std::vector<std::string> ret;
    std::smatch m;
    if (!std::regex_match(text, m, re))
        return ret;
    for (auto&& elm : m)
        ret.push_back(elm.str());
    return ret;
}

}  // namespace

namespace nt {

/* class Blueprint */

Blueprint::Blueprint(const std::string& fileName)
{
    namespace fs = std::filesystem;

    // Read the file
    std::stringstream inputStream;
    {
        std::ifstream ifs{fileName};
        if (!ifs)
            ERR_DIE("File not found: " << fileName);
        inputStream << ifs.rdbuf();
        source_ = inputStream.str();
        inputStream.seekg(std::ios::beg);
    }

    // Parse config file
    const auto src = toml::parse(inputStream, fileName);

    // Find working directory of config
    fs::path wd = fs::absolute(fileName);
    wd.remove_filename();

    // [[file]]
    {
        const auto srcFiles =
            toml::find_or<std::vector<toml::value>>(src, "file", {});
        for (const auto& srcFile : srcFiles) {
            std::string typeStr = toml::find<std::string>(srcFile, "type");
            fs::path path = toml::find<std::string>(srcFile, "path");
            std::string name = toml::find<std::string>(srcFile, "name");

            blueprint::File::TYPE type;
            if (typeStr == "iyokanl1-json")
                type = blueprint::File::TYPE::IYOKANL1_JSON;
            else if (typeStr == "yosys-json")
                type = blueprint::File::TYPE::YOSYS_JSON;
            else
                ERR_DIE("Invalid file type: " << typeStr);

            if (path.is_relative())
                path = wd / path;  // Make path absolute

            files_.push_back(blueprint::File{type, path.string(), name});
        }
    }

    // [[builtin]]
    {
        const auto srcBuiltins =
            toml::find_or<std::vector<toml::value>>(src, "builtin", {});
        for (const auto& srcBuiltin : srcBuiltins) {
            const auto type = toml::find<std::string>(srcBuiltin, "type");
            const auto name = toml::find<std::string>(srcBuiltin, "name");

            if (type == "rom" || type == "mux-rom") {
                auto romType = type == "rom"
                                   ? blueprint::BuiltinROM::TYPE::CMUX_MEMORY
                                   : blueprint::BuiltinROM::TYPE::MUX;
                const auto inAddrWidth =
                    toml::find<size_t>(srcBuiltin, "in_addr_width");
                const auto outRdataWidth =
                    toml::find<size_t>(srcBuiltin, "out_rdata_width");

                builtinROMs_.push_back(blueprint::BuiltinROM{
                    romType, name, inAddrWidth, outRdataWidth});
            }
            else if (type == "ram" || type == "mux-ram") {
                auto ramType = type == "ram"
                                   ? blueprint::BuiltinRAM::TYPE::CMUX_MEMORY
                                   : blueprint::BuiltinRAM::TYPE::MUX;
                const auto inAddrWidth =
                    toml::find<size_t>(srcBuiltin, "in_addr_width");
                const auto inWdataWidth =
                    toml::find<size_t>(srcBuiltin, "in_wdata_width");
                const auto outRdataWidth =
                    toml::find<size_t>(srcBuiltin, "out_rdata_width");

                builtinRAMs_.push_back(blueprint::BuiltinRAM{
                    ramType, name, inAddrWidth, inWdataWidth, outRdataWidth});
            }
        }
    }

    // [connect]
    {
        const auto srcConnect = toml::find_or<toml::table>(src, "connect", {});
        for (const auto& [srcKey, srcValue] : srcConnect) {
            if (srcKey == "TOGND") {  // TOGND = [@...[n:m], @...[n:m], ...]
                auto ary = toml::get<std::vector<std::string>>(srcValue);
                for (const auto& portStr : ary) {  // @...[n:m]
                    if (portStr.empty() || portStr.at(0) != '@')
                        ERR_DIE("Invalid port name for TOGND: " << portStr);
                    auto ports = parsePortString(portStr, "output");
                    for (auto&& port : ports) {  // @...[n]
                        const std::string& name = port.portName;
                        int bit = port.portBit;
                        auto [it, inserted] = atPortWidths_.emplace(name, 0);
                        it->second = std::max(it->second, bit + 1);
                    }
                }
                continue;
            }

            std::string srcTo = srcKey,
                        srcFrom = toml::get<std::string>(srcValue),
                        errMsg = fmt::format("Invalid connect: {} = {}", srcTo,
                                             srcFrom);

            // Check if input is correct.
            if (srcTo.empty() || srcFrom.empty() ||
                (srcTo[0] == '@' && srcFrom[0] == '@'))
                ERR_DIE(errMsg);

            // Others.
            std::vector<blueprint::Port> portsTo =
                                             parsePortString(srcTo, "input"),
                                         portsFrom =
                                             parsePortString(srcFrom, "output");
            if (portsTo.size() != portsFrom.size())
                ERR_DIE(errMsg);

            for (size_t i = 0; i < portsTo.size(); i++) {
                const blueprint::Port& to = portsTo[i];
                const blueprint::Port& from = portsFrom[i];

                if (srcTo[0] == '@') {  // @... = ...
                    if (!to.nodeName.empty() || from.nodeName.empty())
                        ERR_DIE(errMsg);

                    const std::string& name = to.portName;
                    int bit = to.portBit;

                    {
                        auto [it, inserted] =
                            atPorts_.emplace(std::make_tuple(name, bit), from);
                        if (!inserted)
                            LOG_S(WARNING)
                                << srcTo
                                << " is used multiple times. Only the first "
                                   "one is effective.";
                    }

                    auto [it, inserted] = atPortWidths_.emplace(name, 0);
                    it->second = std::max(it->second, bit + 1);
                }
                else if (srcFrom[0] == '@') {  // ... = @...
                    if (!from.nodeName.empty() || to.nodeName.empty())
                        ERR_DIE(errMsg);

                    const std::string& name = from.portName;
                    int bit = from.portBit;

                    {
                        auto [it, inserted] =
                            atPorts_.emplace(std::make_tuple(name, bit), to);
                        if (!inserted)
                            LOG_S(WARNING)
                                << srcFrom
                                << " is used multiple times. Only the first "
                                   "one is effective. (FIXME)";
                    }

                    auto [it, inserted] = atPortWidths_.emplace(name, 0);
                    it->second = std::max(it->second, bit + 1);
                }
                else {  // ... = ...
                    edges_.emplace_back(from, to);
                }
            }
        }
    }
}

std::vector<blueprint::Port> Blueprint::parsePortString(const std::string& src,
                                                        const std::string& kind)
{
    std::string nodeName, portName;
    int portBitFrom, portBitTo;

    auto match = regexMatch(
        src,
        std::regex(R"(^@?(?:([^/]+)/)?([^[]+)(?:\[([0-9]+):([0-9]+)\])?$)"));
    if (match.empty())
        ERR_DIE("Invalid port string: " << src);

    assert(match.size() == 1 + 4);

    nodeName = match[1];
    portName = match[2];

    if (match[3].empty()) {  // hoge/piyo
        assert(match[4].empty());
        portBitFrom = 0;
        portBitTo = 0;
    }
    else {  // hoge/piyo[foo:bar]
        assert(!match[4].empty());
        portBitFrom = std::stoi(match[3]);
        portBitTo = std::stoi(match[4]);
    }

    std::vector<blueprint::Port> ret;
    for (int i = portBitFrom; i < portBitTo + 1; i++)
        ret.push_back(blueprint::Port{nodeName, kind, portName, i});
    return ret;
}

bool Blueprint::needsCircuitKey() const
{
    for (const auto& bprom : builtinROMs_)
        if (bprom.type == blueprint::BuiltinROM::TYPE::CMUX_MEMORY)
            return true;
    for (const auto& bpram : builtinRAMs_)
        if (bpram.type == blueprint::BuiltinRAM::TYPE::CMUX_MEMORY)
            return true;
    return false;
}

const std::string& Blueprint::sourceFile() const
{
    return sourceFile_;
}

const std::string& Blueprint::source() const
{
    return source_;
}

const std::vector<blueprint::File>& Blueprint::files() const
{
    return files_;
}

const std::vector<blueprint::BuiltinROM>& Blueprint::builtinROMs() const
{
    return builtinROMs_;
}

const std::vector<blueprint::BuiltinRAM>& Blueprint::builtinRAMs() const
{
    return builtinRAMs_;
}

const std::vector<std::pair<blueprint::Port, blueprint::Port>>&
Blueprint::edges() const
{
    return edges_;
}

const std::map<std::tuple<std::string, int>, blueprint::Port>&
Blueprint::atPorts() const
{
    return atPorts_;
}

std::optional<blueprint::Port> Blueprint::at(const std::string& portName,
                                             int portBit) const
{
    auto it = atPorts_.find(std::make_tuple(portName, portBit));
    if (it == atPorts_.end())
        return std::nullopt;
    return it->second;
}

const std::unordered_map<std::string, int>& Blueprint::atPortWidths() const
{
    return atPortWidths_;
}

/* makeMUXROM */

namespace {
void make1bitROMWithMUX(const std::string& nodeName,
                        const std::vector<UID>& addrInputs,
                        size_t outRdataWidth, size_t indexOutRdata,
                        NetworkBuilder& nb)
{
    /*
       INPUT
        addr[1] ------------------------------+
       INPUT                                  |
        addr[0] --+-----------------+         |
                  |                 |         |
                  | ROM             |         |
                  |  romdata[0] -- |\         |
                  | ROM            | | --+    |
                  |  romdata[1] -- |/    +-- |\           OUTPUT
                  |                          | | -- ... -- rdata[indexOutRdata]
                  +-----------------+    +-- |/
                                    |    |
                    ROM             |    |
                     romdata[2] -- +\    |
                    ROM            | | --+
                     romdata[3] -- |/

        ...

                    ROM
                     addr[2^inAddrWidth-1] -- ...
    */

    const int inAddrWidth = addrInputs.size();

    // Create ROMs
    std::vector<UID> workingIds;
    for (size_t i = 0; i < (1 << inAddrWidth); i++) {
        UID id = nb.ROM(nodeName, "romdata", indexOutRdata + i * outRdataWidth);
        workingIds.push_back(id);
    }

    // Create MUXs
    for (size_t i = 0; i < inAddrWidth; i++) {
        assert(workingIds.size() > 0 && workingIds.size() % 2 == 0);
        std::vector<UID> newWorkingIds;
        for (int j = 0; j < workingIds.size(); j += 2) {
            int id = nb.MUX();
            nb.connect(workingIds.at(j), id);
            nb.connect(workingIds.at(j + 1), id);
            nb.connect(addrInputs.at(i), id);
            newWorkingIds.push_back(id);
        }
        workingIds.swap(newWorkingIds);
    }
    assert(workingIds.size() == 1);

    // Create output
    int id = nb.OUTPUT(nodeName, "rdata", indexOutRdata);
    nb.connect(workingIds.at(0), id);
}
}  // namespace

void makeMUXROM(const blueprint::BuiltinROM& rom, NetworkBuilder& nb)
{
    // Create inputs
    std::vector<UID> addrInputs;
    for (size_t i = 0; i < rom.inAddrWidth; i++) {
        UID id = nb.INPUT(rom.name, "addr", i);
        addrInputs.push_back(id);
    }

    // Create 1bit ROMs
    for (size_t i = 0; i < rom.outRdataWidth; i++) {
        make1bitROMWithMUX(rom.name, addrInputs, rom.outRdataWidth, i, nb);
    }
}

}  // namespace nt
