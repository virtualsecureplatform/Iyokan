#include "dataholder_nt.hpp"
#include "iyokan_nt.hpp"
#include "packet_nt.hpp"

#include <cereal/archives/portable_binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/variant.hpp>

#include <functional>
#include <typeindex>
#include <variant>

namespace {

using namespace nt;

template <class Archive>
void save(Archive& ar, const Bit& b, const std::uint32_t version)
{
    assert(version == 1);

    ar(static_cast<bool>(b));
}

template <class Archive>
void load(Archive& ar, Bit& b, const std::uint32_t version)
{
    assert(version == 1);

    bool bl;
    ar(bl);
    b = static_cast<Bit>(b);
}

struct Serializable {
    std::variant<Bit> data;

    template <class Archive>
    void serialize(Archive& ar, const std::uint32_t version)
    {
        assert(version == 1);

        ar(data);
    }
};

}  // namespace

CEREAL_CLASS_VERSION(Serializable, 1);

namespace nt {

/* class Allocator */

Allocator::Allocator()
    : hasLoadedFromIStream_(false), indexToBeMade_(0), data_()
{
}

Allocator::Allocator(std::istream& is)
    : hasLoadedFromIStream_(true), indexToBeMade_(0), data_()
{
    // Read and de-serialize data from the snapshot file
    cereal::PortableBinaryInputArchive ar{is};
    size_t size;
    Serializable buf;

    ar(size);
    for (size_t i = 0; i < size; i++) {
        ar(buf);
        switch (buf.data.index()) {
        case 0: {  // Bit
            Bit b = std::get<0>(buf.data);
            data_.emplace_back(b);
            break;
        }

        default:
            ERR_UNREACHABLE;
        }
    }
}

void Allocator::dumpAllocatedData(std::ostream& os) const
{
    // FIXME: WE KNOW the code in this function is TREMENDOUSLY UGLY (and
    // inefficient). We need to find some more sophisticated ways to do this.

    cereal::PortableBinaryOutputArchive ar{os};
    Serializable buf;

    // Serialization process for each type
    std::unordered_map<std::type_index, std::function<void(const std::any&)>>
        tyHandlers;
    tyHandlers[typeid(Bit)] = [&](const std::any& any) {
        const Bit* src = std::any_cast<Bit>(&any);
        buf.data = *src;
        ar(buf);
    };

    // First serialize the size of the entries
    ar(static_cast<size_t>(data_.size()));

    // Dispatch
    for (size_t i = 0; i < data_.size(); i++) {
        const std::any& src = data_.at(i);
        tyHandlers.at(src.type())(src);
    }
}

}  // namespace nt
