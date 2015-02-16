/* Copyright (c) 2015 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RAMCLOUD_TRANSACTION_H
#define RAMCLOUD_TRANSACTION_H

#include <list>
#include <map>
#include <memory>

#include "Common.h"

namespace RAMCloud {

class Buffer;
class ClientTransactionTask;
class RamCloud;

class Transaction {
  PUBLIC:
    explicit Transaction(RamCloud* ramcloud);

    bool commit();

    void read(uint64_t tableId, const void* key, uint16_t keyLength,
            Buffer* value);

    void remove(uint64_t tableId, const void* key, uint16_t keyLength);

    void write(uint64_t tableId, const void* key, uint16_t keyLength,
            const void* buf, uint32_t length);

  PRIVATE:
    /// Overall client state information.
    RamCloud* ramcloud;

    /// Pointer to the dynamically allocated transaction task that represents
    /// that transaction.
    std::shared_ptr<ClientTransactionTask> taskPtr;

    /// Keeps track of whether commit has already been called to preclude
    /// subsequent read, remove, write, and commit calls.
    bool commitStarted;

    DISALLOW_COPY_AND_ASSIGN(Transaction);
};

} // end RAMCloud

#endif  /* RAMCLOUD_TRANSACTION_H */

