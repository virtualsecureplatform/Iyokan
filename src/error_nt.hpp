#ifndef VIRTUALSECUREPLATFORM_ERROR_NT_HPP
#define VIRTUALSECUREPLATFORM_ERROR_NT_HPP

#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>

namespace nt::error {
void initialize();
[[noreturn]] void abortWithBacktrace();
}  // namespace nt::error

#define DBG 1  // verbosity debug for loguru

#define ERROR_DIE(...)                     \
    {                                      \
        LOG_F(ERROR, __VA_ARGS__);         \
        ::nt::error::abortWithBacktrace(); \
    }

#define ERRDIE(cont)                     \
    do {                                 \
        LOG_S(ERROR) << cont;            \
        nt::error::abortWithBacktrace(); \
    } while (false);

#endif
