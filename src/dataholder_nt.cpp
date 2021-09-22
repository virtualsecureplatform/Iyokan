#include "dataholder_nt.hpp"

namespace nt {

DataHolder::DataHolder() : dataBit_(nullptr), type_(TYPE::UND)
{
}

DataHolder::DataHolder(Bit *dataBit) : dataBit_(dataBit), type_(TYPE::BIT)
{
}

Bit DataHolder::getBit() const
{
    assert(type_ == TYPE::BIT);
    return *dataBit_;
}

void DataHolder::setBit(Bit *dataBit)
{
    dataBit_ = dataBit;
    type_ = TYPE::BIT;
}

}  // namespace nt
