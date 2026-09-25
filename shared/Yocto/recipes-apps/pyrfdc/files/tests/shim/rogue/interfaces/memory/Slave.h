/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <rogue/interfaces/memory/Slave.h>.
 * Shadows the real rogue header, which is not installed on a plain
 * development host.
 *
 * Supplies the base class PyRFdc derives from, with the two-argument
 * constructor the production code calls as rim::Slave(4, 0x1000) and the
 * virtual doTransaction the production code overrides. The minimum and
 * maximum access sizes are stored and exposed, so a check can confirm the
 * class was constructed with the access bounds the real transport enforces
 * rather than with whatever the shim felt like.
 *
 * Also pulls in the Logging shim, because PyRFdc.h names
 * std::shared_ptr<rogue::Logging> without including that header directly and
 * relies on the real Slave.h chain to provide it.
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

#ifndef __ROGUE_INTERFACES_MEMORY_SLAVE_H__
#define __ROGUE_INTERFACES_MEMORY_SLAVE_H__

#include <stdint.h>

#include <memory>
#include <mutex>
#include <string>

#include "rogue/Logging.h"
#include "rogue/interfaces/memory/Transaction.h"

namespace rogue {
namespace interfaces {
namespace memory {

class Slave {
  public:
    Slave(uint32_t min, uint32_t max) : min_(min), max_(max) {}

    virtual ~Slave() {}

    virtual uint32_t doMinAccess() { return min_; }
    virtual uint32_t doMaxAccess() { return max_; }

    virtual void doTransaction(std::shared_ptr<rogue::interfaces::memory::Transaction> transaction) {
        (void)transaction;
    }

  private:
    uint32_t min_;
    uint32_t max_;
};

typedef std::shared_ptr<rogue::interfaces::memory::Slave> SlavePtr;

}  // namespace memory
}  // namespace interfaces
}  // namespace rogue

#endif  /* __ROGUE_INTERFACES_MEMORY_SLAVE_H__ */
