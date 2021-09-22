#ifndef VIRTUALSECUREPLATFORM_DATAHOLDER_NT_HPP
#define VIRTUALSECUREPLATFORM_DATAHOLDER_NT_HPP

#include <cassert>

#include <optional>

namespace nt {

enum class Bit : bool;

class DataHolder {
private:
    union {
        Bit *dataBit_;
    };

    enum class TYPE {
        UND,
        BIT,
    } type_;

public:
    DataHolder();
    DataHolder(Bit *dataBit);

    Bit getBit() const;
    void setBit(Bit *dataBit);
};

}  // namespace nt

#endif
