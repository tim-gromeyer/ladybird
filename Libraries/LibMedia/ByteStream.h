/*
 * Copyright (c) 2025, generic_user <generic@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/AtomicRefCounted.h>
#include <AK/NonnullRefPtr.h>
#include <LibMedia/DecoderError.h>
#include <LibMedia/Forward.h>

namespace Media {

class ByteStreamCursor;

class ByteStream : public AtomicRefCounted<ByteStream> {
public:
    virtual ~ByteStream() = default;
    virtual NonnullRefPtr<ByteStreamCursor> create_cursor() = 0;
    virtual u64 size() = 0;
};

class ByteStreamCursor : public AtomicRefCounted<ByteStreamCursor> {
public:
    virtual ~ByteStreamCursor() = default;
    
    enum class SeekMode : u8 {
        SetPosition,
        FromCurrentPosition,
        FromEndPosition,
    };

    virtual DecoderErrorOr<void> seek(size_t position, SeekMode mode) = 0;
    virtual DecoderErrorOr<size_t> read_into(Bytes bytes) = 0;
    virtual DecoderErrorOr<size_t> read_some(Bytes bytes) { return read_into(bytes); }
    virtual size_t position() const = 0;
    virtual u64 size() const = 0;
    virtual void abort() = 0;
    virtual void reset_abort() = 0;
    virtual bool is_blocked() const = 0;
};

}
