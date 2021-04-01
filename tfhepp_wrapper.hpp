#ifndef VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_WRAPPER_HPP
#define VIRTUALSECUREPLATFORM_IYOKAN_TFHEPP_WRAPPER_HPP

#include <tfhe++.hpp>

using TLWElvl0 = TFHEpp::TLWE<TFHEpp::lvl0param>;
using TLWElvl1 = TFHEpp::TLWE<TFHEpp::lvl1param>;
using TRLWElvl1 = TFHEpp::TRLWE<TFHEpp::lvl1param>;
using TRGSWFFTlvl1 = TFHEpp::TRGSWFFT<TFHEpp::lvl1param>;
using Polynomiallvl1 = TFHEpp::Polynomial<TFHEpp::lvl1param>;
using PolynomialInFDlvl1 = TFHEpp::PolynomialInFD<TFHEpp::lvl1param>;
using Keylvl1 = TFHEpp::Key<TFHEpp::lvl1param>;
using KeySwitchingKey = TFHEpp::KeySwitchingKey<TFHEpp::lvl10param>;

inline TRLWElvl1 trlweSymEncryptlvl1(const Polynomiallvl1& p,
                                     const double alpha, const Keylvl1& key)
{
    return TFHEpp::trlweSymEncrypt<TFHEpp::lvl1param>(p, alpha, key);
}

inline std::array<bool, TFHEpp::lvl1param::n> trlweSymDecryptlvl1(
    const TRLWElvl1& c, const Keylvl1& key)
{
    return TFHEpp::trlweSymDecrypt<TFHEpp::lvl1param>(c, key);
}

inline void CMUXFFTlvl1(TRLWElvl1& res, const TRGSWFFTlvl1& cs,
                        const TRLWElvl1& c1, const TRLWElvl1& c0)
{
    TFHEpp::CMUXFFT<TFHEpp::lvl1param>(res, cs, c1, c0);
}

inline void trgswfftExternalProductlvl1(TRLWElvl1& res, const TRLWElvl1& trlwe,
                                        const TRGSWFFTlvl1& trgswfft)
{
    TFHEpp::trgswfftExternalProduct<TFHEpp::lvl1param>(res, trlwe, trgswfft);
}

inline void SampleExtractIndexlvl1(TLWElvl1& tlwe, const TRLWElvl1& trlwe,
                                   const int index)
{
    TFHEpp::SampleExtractIndex<TFHEpp::lvl1param>(tlwe, trlwe, index);
}

inline void IdentityKeySwitchlvl10(TLWElvl0& res, const TLWElvl1& tlwe,
                                   const KeySwitchingKey& ksk)
{
    TFHEpp::IdentityKeySwitch<TFHEpp::lvl10param>(res, tlwe, ksk);
}

inline void GateBootstrappingTLWE2TRLWEFFTlvl01(TRLWElvl1& acc,
                                                const TLWElvl0& tlwe,
                                                const TFHEpp::GateKey& gk)
{
    TFHEpp::GateBootstrappingTLWE2TRLWEFFT<TFHEpp::lvl01param>(acc, tlwe,
                                                               gk.bkfftlvl01);
}

inline void TwistFFTlvl1(Polynomiallvl1& res, const PolynomialInFDlvl1& a)
{
    TFHEpp::TwistFFT<TFHEpp::lvl1param>(res, a);
}

#endif
