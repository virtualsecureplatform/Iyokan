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

DataHolder::DataHolder(const TLWELvl0* const dataTLWELvl0)
    : dataTLWELvl0_(dataTLWELvl0), type_(TYPE::TLWE_LVL0)
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

void DataHolder::getTLWELvl0(TLWELvl0& out) const
{
    assert(type_ == TYPE::TLWE_LVL0);
    out = *dataTLWELvl0_;
}

void DataHolder::setTLWELvl0(const TLWELvl0* const dataTLWELvl0)
{
    dataTLWELvl0_ = dataTLWELvl0;
    type_ = TYPE::TLWE_LVL0;
}

}  // namespace nt
