/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for
 * <rogue/interfaces/memory/Transaction.h>. Shadows the real rogue header,
 * which is not installed on a plain development host.
 *
 * This shim adds observable behavior. The pair that the whole error-reporting
 * question turns on is which of done() and errorStr() PyRFdc::doTransaction
 * calls, and what string it passes, so both are recorded on the transaction:
 * doneCalled, errorStrCalled and errorStrValue. A harness that only watched
 * the console line could not tell a transaction that failed from one that
 * logged and then reported success to its caller.
 *
 * The payload is a harness-owned byte buffer sized by the caller, so a write
 * followed by a read of the same offset is a real round trip through
 * PyRFdc::doTransaction rather than a reimplementation of its dispatch.
 *
 * Only the members PyRFdc.cpp names are provided: size(), address(),
 * begin(), type(), lock(), done() and errorStr(). The real class also
 * carries timeouts, sub-transactions, identifiers and Python accessors.
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

#ifndef __ROGUE_INTERFACES_MEMORY_TRANSACTION_H__
#define __ROGUE_INTERFACES_MEMORY_TRANSACTION_H__

#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "rogue/interfaces/memory/TransactionLock.h"

namespace rogue {
namespace interfaces {
namespace memory {

class Transaction {
  public:
    typedef uint8_t *iterator;

    //! Harness factory. Address and type are fixed for the life of the
    //! transaction, as they are on the real class.
    static std::shared_ptr<rogue::interfaces::memory::Transaction> create(uint64_t address,
                                                                          uint32_t size,
                                                                          uint32_t type) {
        return std::make_shared<rogue::interfaces::memory::Transaction>(address, size, type);
    }

    Transaction(uint64_t address, uint32_t size, uint32_t type)
        : address_(address), type_(type), data_(size, 0) {}

    ~Transaction() {}

    std::shared_ptr<rogue::interfaces::memory::TransactionLock> lock() {
        return std::make_shared<rogue::interfaces::memory::TransactionLock>();
    }

    uint64_t address() { return address_; }
    uint32_t size() { return static_cast<uint32_t>(data_.size()); }
    uint32_t type() { return type_; }

    uint8_t *begin() { return data_.empty() ? nullptr : &data_[0]; }
    uint8_t *end() { return begin() + data_.size(); }

    //! Recorded, not merely accepted: the count distinguishes a single
    //! completion from a body that completed the same transaction twice.
    void done() { doneCalls_++; }

    //! Recorded verbatim, including any trailing newline, because the exact
    //! text is what the caller sees as the exception message.
    void errorStr(std::string error) {
        errorStrCalls_++;
        errorStrValue_ = error;
    }

    //! Harness accessors. Not present on the real class.
    uint32_t doneCalls() const { return doneCalls_; }
    uint32_t errorStrCalls() const { return errorStrCalls_; }
    bool doneCalled() const { return doneCalls_ > 0; }
    bool errorStrCalled() const { return errorStrCalls_ > 0; }
    const std::string &errorStrValue() const { return errorStrValue_; }

    //! Payload helpers, so a check can seed a write and read a readback
    //! without reaching into the buffer by hand.
    void setWord(uint32_t index, uint32_t value) {
        memcpy(&data_[index * sizeof(uint32_t)], &value, sizeof(uint32_t));
    }

    uint32_t getWord(uint32_t index) const {
        uint32_t value = 0;
        memcpy(&value, &data_[index * sizeof(uint32_t)], sizeof(uint32_t));
        return value;
    }

  private:
    uint64_t address_;
    uint32_t type_;
    std::vector<uint8_t> data_;
    uint32_t doneCalls_ = 0;
    uint32_t errorStrCalls_ = 0;
    std::string errorStrValue_;
};

typedef std::shared_ptr<rogue::interfaces::memory::Transaction> TransactionPtr;

}  // namespace memory
}  // namespace interfaces
}  // namespace rogue

#endif  /* __ROGUE_INTERFACES_MEMORY_TRANSACTION_H__ */
