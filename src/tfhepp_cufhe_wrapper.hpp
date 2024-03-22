#ifndef VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_CUFHE_WRAPPER_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_CUFHE_WRAPPER_HPP

#include <tfhe++.hpp>

using Lvl0 = TFHEpp::lvl0param;
using Lvl1 = TFHEpp::lvl1param;
using Lvl01 = TFHEpp::lvl01param;
using Lvl02 = TFHEpp::lvl02param;
using Lvl10 = TFHEpp::lvl10param;
using Lvl21 = TFHEpp::lvl21param;

using PolyLvl1 = TFHEpp::Polynomial<Lvl1>;
using TLWELvl0 = TFHEpp::TLWE<Lvl0>;
using TLWELvl1 = TFHEpp::TLWE<Lvl1>;
using TRGSWLvl1 = TFHEpp::TRGSW<Lvl1>;
using TRGSWLvl1FFT = TFHEpp::TRGSWFFT<Lvl1>;
using TRLWELvl1 = TFHEpp::TRLWE<Lvl1>;

using EvalKey = TFHEpp::EvalKey;
using SecretKey = TFHEpp::SecretKey;
using KeySwitchingKey = TFHEpp::KeySwitchingKey<Lvl10>;

inline bool decryptTLWELvl0(const TLWELvl0& src, const SecretKey& sk)
{
    return TFHEpp::bootsSymDecrypt<Lvl0>({src}, sk).at(0);
}

inline void setTLWELvl0Trivial0(TLWELvl0& dst)
{
    TFHEpp::HomCONSTANTZERO<Lvl0>(dst);
}

inline void setTLWELvl0Trivial1(TLWELvl0& dst)
{
    TFHEpp::HomCONSTANTONE<Lvl0>(dst);
}

#ifdef IYOKAN_CUDA_ENABLED

#include <cufhe_gpu.cuh>

inline TLWELvl0 cufhe2tfhepp(const cufhe::Ctxt<TFHEpp::lvl0param>& src)
{
    // Assume src.tlwehost has valid data
    return src.tlwehost;
}

inline void cufhe2tfheppInPlace(TLWELvl0& dst, const cufhe::Ctxt<TFHEpp::lvl0param>& src)
{
    // Assume src.tlwehost has valid data
    // FIXME: Optimization using in-place property?
    dst = src.tlwehost;
}

inline std::shared_ptr<cufhe::Ctxt<TFHEpp::lvl0param>> tfhepp2cufhe(const TLWELvl0& src)
{
    auto c = std::make_shared<cufhe::Ctxt<TFHEpp::lvl0param>>();
    c->tlwehost = src;
    return c;
}

inline void tfhepp2cufheInPlace(cufhe::Ctxt<TFHEpp::lvl0param>& dst, const TLWELvl0& src)
{
    dst.tlwehost = src;
}

inline void setCtxtZero(cufhe::Ctxt<TFHEpp::lvl0param>& out)
{
    TFHEpp::HomCONSTANTZERO<Lvl0>(out.tlwehost);
}

inline void setCtxtOne(cufhe::Ctxt<TFHEpp::lvl0param>& out)
{
    TFHEpp::HomCONSTANTONE<Lvl0>(out.tlwehost);
}

#endif
#endif
