#include "iyokan_nt.hpp"

#include <picojson.h>

namespace nt {

/* class YosysJSONReader */

namespace {

class YosysJSONReader {
private:
    enum class PORT {
        IN,
        OUT,
    };

    struct Port {
        PORT type;
        int id, bit;

        Port(PORT type, int id, int bit) : type(type), id(id), bit(bit)
        {
        }
    };

    enum class CELL {
        NOT,
        AND,
        ANDNOT,
        NAND,
        OR,
        XOR,
        XNOR,
        NOR,
        ORNOT,
        DFFP,
        // SDFFPP0,
        // SDFFPP1,
        MUX,
    };

    struct Cell {
        CELL type;
        int id, bit0, bit1, bit2;

        Cell(CELL type, int id, int bit0)
            : type(type), id(id), bit0(bit0), bit1(-1), bit2(-1)
        {
        }
        Cell(CELL type, int id, int bit0, int bit1)
            : type(type), id(id), bit0(bit0), bit1(bit1), bit2(-1)
        {
        }
        Cell(CELL type, int id, int bit0, int bit1, int bit2)
            : type(type), id(id), bit0(bit0), bit1(bit1), bit2(bit2)
        {
        }
    };

private:
    static int getConnBit(const picojson::object& conn, const std::string& key)
    {
        using namespace picojson;
        const auto& bits = conn.at(key).get<array>();
        if (bits.size() != 1)
            ERR_DIE("Invalid JSON: wrong conn size: expected 1, got "
                    << bits.size());
        if (!bits.at(0).is<double>())
            ERR_DIE(
                "Connection of cells to a constant driver is not implemented.");
        return bits.at(0).get<double>();
    }

public:
    template <class NetworkBuilder>
    static void read(const std::string& nodeName, std::istream& is,
                     NetworkBuilder& builder)
    {
        // Convert Yosys JSON to gates. Thanks to:
        // https://github.com/virtualsecureplatform/Iyokan-L1/blob/ef7c9a993ddbfd54ef58e66b116b681e59d90a3c/Converter/YosysConverter.cs
        using namespace picojson;

        value v;
        const std::string err = parse(v, is);
        if (!err.empty())
            ERR_DIE("Invalid JSON of network: " << err);

        object& root = v.get<object>();
        object& modules = root.at("modules").get<object>();
        if (modules.size() != 1)
            ERR_DIE(".modules should be an object of size 1");
        object& modul = modules.begin()->second.get<object>();
        object& ports = modul.at("ports").get<object>();
        object& cells = modul.at("cells").get<object>();

        std::unordered_map<int, int> bit2id;

        // Create INPUT/OUTPUT and extract port connection info
        std::vector<Port> portvec;
        for (auto&& [key, valAny] : ports) {
            object& val = valAny.template get<object>();
            std::string& direction = val["direction"].get<std::string>();
            array& bits = val["bits"].get<array>();

            if (key == "clock")
                continue;
            if (key == "reset" && bits.size() == 0)
                continue;
            if (direction != "input" && direction != "output")
                ERR_DIE("Invalid direction token: " << direction);

            const bool isDirInput = direction == "input";
            const std::string& portName = key;
            for (size_t i = 0; i < bits.size(); i++) {
                const int portBit = i;

                if (bits.at(i).is<std::string>()) {
                    // Yosys document
                    // (https://yosyshq.net/yosys/cmd_write_json.html) says:
                    //
                    //     Signal bits that are connected to a constant driver
                    //     are denoted as string "0" or "1" instead of a number.
                    //
                    // We handle this case here.

                    if (isDirInput)
                        ERR_DIE(
                            "Invalid bits: INPUT that is connected to a "
                            "constant driver is not implemented");

                    std::string cnstStr = bits.at(i).get<std::string>();
                    bool cnst = cnstStr == "1";
                    if (!cnst && cnstStr != "0")
                        LOG_S(WARNING)
                            << "Constant bit of '{}' is regarded as '0'."
                            << cnstStr;

                    int id1 = builder.OUTPUT(nodeName, portName, portBit),
                        id0 = cnst ? builder.CONSTONE() : builder.CONSTZERO();
                    builder.connect(id0, id1);
                }
                else {
                    const int bit = bits.at(i).get<double>();

                    int id = isDirInput
                                 ? builder.INPUT(nodeName, portName, portBit)
                                 : builder.OUTPUT(nodeName, portName, portBit);
                    portvec.emplace_back(isDirInput ? PORT::IN : PORT::OUT, id,
                                         bit);
                    if (isDirInput)
                        bit2id.emplace(bit, id);
                }
            }
        }

        // Create gates and extract gate connection info
        const std::unordered_map<std::string, CELL> mapCell = {
            {"$_NOT_", CELL::NOT},
            {"$_AND_", CELL::AND},
            {"$_ANDNOT_", CELL::ANDNOT},
            {"$_NAND_", CELL::NAND},
            {"$_OR_", CELL::OR},
            {"$_XOR_", CELL::XOR},
            {"$_XNOR_", CELL::XNOR},
            {"$_NOR_", CELL::NOR},
            {"$_ORNOT_", CELL::ORNOT},
            {"$_DFF_P_", CELL::DFFP},
            //{"$_SDFF_PP0_", CELL::SDFFPP0},
            //{"$_SDFF_PP1_", CELL::SDFFPP1},
            {"$_MUX_", CELL::MUX},
        };
        std::vector<Cell> cellvec;
        for (auto&& [_key, valAny] : cells) {
            object& val = valAny.template get<object>();
            const std::string& type = val.at("type").get<std::string>();
            object& conn = val.at("connections").get<object>();
            auto get = [&](const char* key) -> int {
                return getConnBit(conn, key);
            };

            int bit = -1, id = -1;
            switch (mapCell.at(type)) {
            case CELL::AND:
                id = builder.AND();
                cellvec.emplace_back(CELL::AND, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::NAND:
                id = builder.NAND();
                cellvec.emplace_back(CELL::NAND, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::XOR:
                id = builder.XOR();
                cellvec.emplace_back(CELL::XOR, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::XNOR:
                id = builder.XNOR();
                cellvec.emplace_back(CELL::XNOR, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::NOR:
                id = builder.NOR();
                cellvec.emplace_back(CELL::NOR, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::ANDNOT:
                id = builder.ANDNOT();
                cellvec.emplace_back(CELL::ANDNOT, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::OR:
                id = builder.OR();
                cellvec.emplace_back(CELL::OR, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::ORNOT:
                id = builder.ORNOT();
                cellvec.emplace_back(CELL::ORNOT, id, get("A"), get("B"));
                bit = get("Y");
                break;
            case CELL::DFFP:
                id = builder.DFF();
                cellvec.emplace_back(CELL::DFFP, id, get("D"));
                bit = get("Q");
                break;
                /*
            case CELL::SDFFPP0:
                id = builder.SDFF(Bit(false));
                cellvec.emplace_back(CELL::DFFP, id, get("D"));
                bit = get("Q");
                break;
            case CELL::SDFFPP1:
                id = builder.SDFF(Bit(true));
                cellvec.emplace_back(CELL::DFFP, id, get("D"));
                bit = get("Q");
                break;
                */
            case CELL::NOT:
                id = builder.NOT();
                cellvec.emplace_back(CELL::NOT, id, get("A"));
                bit = get("Y");
                break;
            case CELL::MUX:
                id = builder.MUX();
                cellvec.emplace_back(CELL::MUX, id, get("A"), get("B"),
                                     get("S"));
                bit = get("Y");
                break;
            }
            bit2id.emplace(bit, id);
        }

        for (auto&& port : portvec) {
            if (port.type == PORT::IN)
                // Actually nothing to do!
                continue;
            builder.connect(bit2id.at(port.bit), port.id);
        }

        for (auto&& cell : cellvec) {
            switch (cell.type) {
            case CELL::AND:
            case CELL::NAND:
            case CELL::XOR:
            case CELL::XNOR:
            case CELL::NOR:
            case CELL::ANDNOT:
            case CELL::OR:
            case CELL::ORNOT:
                builder.connect(bit2id.at(cell.bit0), cell.id);
                builder.connect(bit2id.at(cell.bit1), cell.id);
                break;
            case CELL::DFFP:
            // case CELL::SDFFPP0:
            // case CELL::SDFFPP1:
            case CELL::NOT:
                builder.connect(bit2id.at(cell.bit0), cell.id);
                break;
            case CELL::MUX:
                builder.connect(bit2id.at(cell.bit0), cell.id);
                builder.connect(bit2id.at(cell.bit1), cell.id);
                builder.connect(bit2id.at(cell.bit2), cell.id);
                break;
            }
        }
    }
};

}  // namespace

void readYosysJSONNetwork(const std::string& nodeName, std::istream& is,
                          NetworkBuilder& nb)
{
    YosysJSONReader::read(nodeName, is, nb);
}

/* class IyokanL1JSONReader */

namespace {

class IyokanL1JSONReader {
public:
    template <class NetworkBuilder>
    static void read(const std::string& nodeName, std::istream& is,
                     NetworkBuilder& builder)
    {
        std::unordered_map<int, int> id2taskId;
        auto addId = [&](int id, int taskId) { id2taskId.emplace(id, taskId); };
        auto findTaskId = [&](int id) {
            auto it = id2taskId.find(id);
            if (it == id2taskId.end())
                ERR_DIE("Invalid JSON");
            return it->second;
        };
        auto connectIds = [&](int from, int to) {
            builder.connect(findTaskId(from), findTaskId(to));
        };

        picojson::value v;
        const std::string err = picojson::parse(v, is);
        if (!err.empty())
            ERR_DIE("Invalid JSON of network: " << err);

        picojson::object& obj = v.get<picojson::object>();
        picojson::array& cells = obj["cells"].get<picojson::array>();
        picojson::array& ports = obj["ports"].get<picojson::array>();
        for (const auto& e : ports) {
            picojson::object port = e.get<picojson::object>();
            std::string type = port.at("type").get<std::string>();
            int id = static_cast<int>(port.at("id").get<double>());
            std::string portName = port.at("portName").get<std::string>();
            int portBit = static_cast<int>(port.at("portBit").get<double>());
            if (type == "input")
                addId(id, builder.INPUT(nodeName, portName, portBit));
            else if (type == "output")
                addId(id, builder.OUTPUT(nodeName, portName, portBit));
        }
        for (const auto& e : cells) {
            picojson::object cell = e.get<picojson::object>();
            std::string type = cell.at("type").get<std::string>();
            int id = static_cast<int>(cell.at("id").get<double>());
            if (type == "AND")
                addId(id, builder.AND());
            else if (type == "NAND")
                addId(id, builder.NAND());
            else if (type == "ANDNOT")
                addId(id, builder.ANDNOT());
            else if (type == "XOR")
                addId(id, builder.XOR());
            else if (type == "XNOR")
                addId(id, builder.XNOR());
            else if (type == "DFFP")
                addId(id, builder.DFF());
            else if (type == "NOT")
                addId(id, builder.NOT());
            else if (type == "NOR")
                addId(id, builder.NOR());
            else if (type == "OR")
                addId(id, builder.OR());
            else if (type == "ORNOT")
                addId(id, builder.ORNOT());
            else if (type == "MUX")
                addId(id, builder.MUX());
            else {
                bool valid = false;
                /* FIXME
                // If builder.RAM() exists
                if constexpr (detail::hasMethodFuncRAM<NetworkBuilder>) {
                    if (type == "RAM") {
                        int addr = cell.at("ramAddress").get<double>(),
                            bit = cell.at("ramBit").get<double>();
                        addId(id, builder.RAM(addr, bit));
                        valid = true;
                    }
                }
                */

                if (!valid)
                    ERR_DIE("Invalid JSON of network. Invalid type: " << type);
            }
        }
        for (const auto& e : ports) {
            picojson::object port = e.get<picojson::object>();
            std::string type = port.at("type").get<std::string>();
            int id = static_cast<int>(port.at("id").get<double>());
            picojson::array& bits = port.at("bits").get<picojson::array>();
            if (type == "input") {
                // nothing to do!
            }
            else if (type == "output") {
                for (const auto& b : bits) {
                    int logic = static_cast<int>(b.get<double>());
                    connectIds(logic, id);
                }
            }
        }
        for (const auto& e : cells) {
            picojson::object cell = e.get<picojson::object>();
            std::string type = cell.at("type").get<std::string>();
            int id = static_cast<int>(cell.at("id").get<double>());
            picojson::object input = cell.at("input").get<picojson::object>();
            if (type == "AND" || type == "NAND" || type == "XOR" ||
                type == "XNOR" || type == "NOR" || type == "ANDNOT" ||
                type == "OR" || type == "ORNOT") {
                int A = static_cast<int>(input.at("A").get<double>());
                int B = static_cast<int>(input.at("B").get<double>());
                connectIds(A, id);
                connectIds(B, id);
            }
            else if (type == "DFFP" || type == "RAM") {
                int D = static_cast<int>(input.at("D").get<double>());
                connectIds(D, id);
            }
            else if (type == "NOT") {
                int A = static_cast<int>(input.at("A").get<double>());
                connectIds(A, id);
            }
            else if (type == "MUX") {
                int A = static_cast<int>(input.at("A").get<double>());
                int B = static_cast<int>(input.at("B").get<double>());
                int S = static_cast<int>(input.at("S").get<double>());
                connectIds(A, id);
                connectIds(B, id);
                connectIds(S, id);
            }
            else {
                ERR_DIE("Invalid JSON of network. Invalid type: " << type);
            }
        }
    }
};

}  // namespace

void readIyokanL1JSONNetwork(const std::string& nodeName, std::istream& is,
                             NetworkBuilder& nb)
{
    IyokanL1JSONReader::read(nodeName, is, nb);
}

}  // namespace nt
