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

using GateKey = TFHEpp::GateKeywoFFT;
using GateKeyFFT = TFHEpp::GateKey;
using SecretKey = TFHEpp::SecretKey;
using CircuitKey = TFHEpp::CircuitKey<Lvl02, Lvl21>;
using KeySwitchingKey = TFHEpp::KeySwitchingKey<Lvl10>;

inline bool decryptTLWELvl0(const TLWELvl0& src, const SecretKey& sk)
{
    return TFHEpp::bootsSymDecrypt({src}, sk).at(0);
}

#ifdef IYOKAN_CUDA_ENABLED

#include <cufhe.h>
#include <cufhe_gpu.cuh>

inline TLWELvl0 cufhe2tfhepp(const cufhe::Ctxt& src)
{
    // Assume src.tlwehost has valid data
    return src.tlwehost;
}

inline void cufhe2tfheppInPlace(TLWELvl0& dst, const cufhe::Ctxt& src)
{
    // Assume src.tlwehost has valid data
    // FIXME: Optimization using in-place property?
    dst = src.tlwehost;
}

inline std::shared_ptr<cufhe::Ctxt> tfhepp2cufhe(const TLWELvl0& src)
{
    auto c = std::make_shared<cufhe::Ctxt>();
    c->tlwehost = src;
    return c;
}

inline void tfhepp2cufheInPlace(cufhe::Ctxt& dst, const TLWELvl0& src)
{
    dst.tlwehost = src;
}

inline void setCtxtZero(cufhe::Ctxt& out)
{
    TFHEpp::HomCONSTANTZERO(out.tlwehost);
}

inline void setCtxtOne(cufhe::Ctxt& out)
{
    TFHEpp::HomCONSTANTONE(out.tlwehost);
}

inline void ifftGateKey(GateKey& out, const GateKeyFFT& src)
{
    out.ksk = src.ksk;

    for (size_t p = 0; p < Lvl0::n; p++) {
        const TRGSWLvl1FFT& trgswfft = src.bkfftlvl01.at(p);
        TRGSWLvl1& trgsw = out.bklvl01.at(p);
        for (size_t q = 0; q < 2 * Lvl1::l; q++) {
            for (size_t r = 0; r < 2; r++) {
                TFHEpp::TwistFFT<Lvl1>(trgsw.at(q).at(r), trgswfft.at(q).at(r));
            }
        }
    }
}

#endif
#endif
