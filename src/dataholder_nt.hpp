#ifndef VIRTUALSECUREPLATFORM_DATAHOLDER_NT_HPP
#define VIRTUALSECUREPLATFORM_DATAHOLDER_NT_HPP

#include "tfhepp_cufhe_wrapper.hpp"

#include <cassert>

#include <optional>

namespace nt {

enum class Bit : bool;

// DataHolder holds data using Task::setInput/Task::getOutput.
// FIXME: The name "DataHolder" is misleading. Maybe "IODataPointer" or
// something is more suitable, because we actually do not "hold" the data but
// "point" them. DataHolder::setBit() does not set the bit itself but set the
// pointer to a bit.
class DataHolder {
private:
    union {
        const Bit* dataBit_;
        const TLWELvl0* dataTLWELvl0_;
    };

    enum class TYPE {
        UND,
        BIT,
        TLWE_LVL0,
    } type_;

public:
    DataHolder();
    DataHolder(const Bit* const dataBit);
    DataHolder(const TLWELvl0* const dataTLWELvl0);

    Bit getBit() const;
    void setBit(const Bit* const dataBit);

    void getTLWELvl0(TLWELvl0& out) const;
    void setTLWELvl0(const TLWELvl0* const dataTLWELvl0);
};

}  // namespace nt

#endif
