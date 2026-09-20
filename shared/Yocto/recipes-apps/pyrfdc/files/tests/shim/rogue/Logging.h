/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <rogue/Logging.h>. Shadows the real rogue
 * header, which is not installed on a plain development host.
 *
 * This shim adds observable behavior rather than stubbing to nothing: every
 * string passed to error() is formatted and appended to the recorded error
 * list on the single harness fixture gScript. The console line PyRFdc.cpp
 * emits at the end of a failing transaction, and the string it hands back to
 * the caller, are two separate observations of the same failure, and a check
 * that only saw the second could not tell an operator-visible message from a
 * silent one.
 *
 * Only the members the production sources name are provided: create(), and
 * the error(), warning() and debug() format methods. The real class also
 * carries log levels, filters and Python forwarding, none of which
 * PyRFdc.cpp or PyRFdc.h reaches.
 *
 * Test-build only. See shim/rogue/Directives.h for why files/tests/ cannot
 * reach the Yocto image build.
 * ----------------------------------------------------------------------------
 * This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the 'axi-soc-ultra-plus-core', including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
 **/

#ifndef __ROGUE_LOGGING_H__
#define __ROGUE_LOGGING_H__

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

#include "xrfdcScript.h"

namespace rogue {

class Logging {
  public:
    explicit Logging(const std::string &name) : name_(name) {}

    static std::shared_ptr<rogue::Logging> create(const std::string &name) {
        return std::make_shared<rogue::Logging>(name);
    }

    //! Record the formatted string, so a check can assert the console line.
    void error(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        gScript.logErrors.push_back(format(fmt, args));
        va_end(args);
    }

    //! Recorded separately from error(): a warning is not a failure report.
    void warning(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        gScript.logWarnings.push_back(format(fmt, args));
        va_end(args);
    }

    //! Recorded so a check can assert progress through the constructor.
    void debug(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        gScript.logDebugs.push_back(format(fmt, args));
        va_end(args);
    }

  private:
    //! Format into a bounded buffer. Truncation is reported in the recorded
    //! string rather than silently dropped, so a check cannot pass against a
    //! message the harness itself cut short.
    static std::string format(const char *fmt, va_list args) {
        char buf[1024];
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        if (n < 0) {
            return std::string("<shim Logging: vsnprintf failed>");
        }
        std::string out(buf);
        if (static_cast<size_t>(n) >= sizeof(buf)) {
            out += "<shim Logging: truncated>";
        }
        return out;
    }

    std::string name_;
};

}  // namespace rogue

#endif  /* __ROGUE_LOGGING_H__ */
