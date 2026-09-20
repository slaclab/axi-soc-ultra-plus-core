/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for
 * <rogue/interfaces/memory/TransactionLock.h>. Shadows the real rogue
 * header, which is not installed on a plain development host.
 *
 * PyRFdc::doTransaction takes one of these as a scope guard and never calls a
 * method on it, so the shim is an empty RAII token. The real class holds the
 * transaction and locks its mutex for the scope; the harness drives one
 * transaction at a time from one thread, so there is no contention for a
 * shim lock to arbitrate. Making that a no-op is therefore a statement about
 * the harness, not about the production code: any concurrency defect in
 * PyRFdc.cpp is outside what this harness can observe.
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

#ifndef __ROGUE_INTERFACES_MEMORY_TRANSACTION_LOCK_H__
#define __ROGUE_INTERFACES_MEMORY_TRANSACTION_LOCK_H__

#include <memory>

namespace rogue {
namespace interfaces {
namespace memory {

class TransactionLock {
  public:
    TransactionLock() {}
    ~TransactionLock() {}

    void lock() {}
    void unlock() {}
};

typedef std::shared_ptr<rogue::interfaces::memory::TransactionLock> TransactionLockPtr;

}  // namespace memory
}  // namespace interfaces
}  // namespace rogue

#endif  /* __ROGUE_INTERFACES_MEMORY_TRANSACTION_LOCK_H__ */
