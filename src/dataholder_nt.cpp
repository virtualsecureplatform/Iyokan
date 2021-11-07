#include "dataholder_nt.hpp"

namespace nt {

/* class DataHolder */
DataHolder::DataHolder() : dataBit_(nullptr), type_(TYPE::UND)
{
}

DataHolder::DataHolder(const Bit* const dataBit)
    : dataBit_(dataBit), type_(TYPE::BIT)
{
}

Bit DataHolder::getBit() const
{
    assert(type_ == TYPE::BIT);
    return *dataBit_;
}

void DataHolder::setBit(const Bit* const dataBit)
{
    dataBit_ = dataBit;
    type_ = TYPE::BIT;
}

}  // namespace nt
