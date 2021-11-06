#include "label.hpp"

#include <ostream>

/* struct ConfigName */
std::ostream& operator<<(std::ostream& os, const ConfigName& c)
{
    os << c.nodeName << "/" << c.portName << "[" << c.portBit << "]";
    return os;
}

bool operator<(const ConfigName& lhs, const ConfigName& rhs)
{
    if (int res = lhs.nodeName.compare(rhs.nodeName); res != 0)
        return res < 0;
    if (int res = lhs.portName.compare(rhs.portName); res != 0)
        return res < 0;
    return lhs.portBit < rhs.portBit;
}

/* struct Label */
// Initialization of static variables.
const char* const Label::INPUT = "Input";
const char* const Label::OUTPUT = "Output";
