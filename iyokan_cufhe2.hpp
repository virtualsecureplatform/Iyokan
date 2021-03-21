#ifndef VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP2
#define VIRTUALSECUREPLATFORM_IYOKAN_CUFHE_HPP2

#include <cufhe.h>
#include <cufhe_gpu.cuh>

#include "iyokan.hpp"
#include "iyokan_tfhepp.hpp"

class CUFHE2Stream {
private:
    std::unique_ptr<cufhe::Stream> st_;

public:
    CUFHE2Stream() : st_(std::make_unique<cufhe::Stream>())
    {
        st_->Create();
    }

    ~CUFHE2Stream()
    {
        st_->Destroy();
    }

    operator cufhe::Stream() const
    {
        return *st_;
    }
};

void doCUFHE2(const Options& opt);

#endif
