#ifndef VIRTUALSECUREPLATFORM_BLUEPRINT_HPP
#define VIRTUALSECUREPLATFORM_BLUEPRINT_HPP

#include "label.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace nt {

namespace blueprint {  // blueprint components
struct File {
    enum class TYPE {
        IYOKANL1_JSON,
        YOSYS_JSON,
    } type;
    std::string path, name;
};

struct BuiltinROM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, outRdataWidth;
};

struct BuiltinRAM {
    enum class TYPE {
        CMUX_MEMORY,
        MUX,
    } type;
    std::string name;
    size_t inAddrWidth, inWdataWidth, outRdataWidth;
};

struct Port {
    const char* kind;  // Store a string literal
    ConfigName cname;
};
}  // namespace blueprint

class Blueprint {
private:
    std::string sourceFile_, source_;

    std::vector<blueprint::File> files_;
    std::vector<blueprint::BuiltinROM> builtinROMs_;
    std::vector<blueprint::BuiltinRAM> builtinRAMs_;
    std::vector<std::pair<blueprint::Port, blueprint::Port>> edges_;

    std::map<std::tuple<std::string, int>, blueprint::Port> atPorts_;
    std::unordered_map<std::string, int> atPortWidths_;

private:
    std::vector<blueprint::Port> parsePortString(const std::string& src,
                                                 const char* const kind);

public:
    Blueprint(const std::string& fileName);

    bool needsCircuitKey() const;
    const std::string& sourceFile() const;
    const std::string& source() const;
    const std::vector<blueprint::File>& files() const;
    const std::vector<blueprint::BuiltinROM>& builtinROMs() const;
    const std::vector<blueprint::BuiltinRAM>& builtinRAMs() const;
    const std::vector<std::pair<blueprint::Port, blueprint::Port>>& edges()
        const;
    const std::map<std::tuple<std::string, int>, blueprint::Port>& atPorts()
        const;
    std::optional<blueprint::Port> at(const std::string& portName,
                                      int portBit = 0) const;
    const std::unordered_map<std::string, int>& atPortWidths() const;
};

}  // namespace nt

#endif
