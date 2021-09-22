#ifndef VIRTUALSECUREPLATFORM_ERROR_NT_HPP
#define VIRTUALSECUREPLATFORM_ERROR_NT_HPP

#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>

namespace nt::error {
void initialize();
[[noreturn]] void abortWithBacktrace();
}  // namespace nt::error

#define DBG 1  // verbosity debug for loguru

#define ERR_DIE(cont)                    \
    do {                                 \
        LOG_S(ERROR) << cont;            \
        nt::error::abortWithBacktrace(); \
    } while (false);

#define ERR_UNREACHABLE ERR_DIE("Internal error: unreachable here")

#endif
