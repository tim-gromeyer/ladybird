/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Atomic.h>
#include <AK/AtomicRefCounted.h>
#include <AK/ByteBuffer.h>
#include <AK/Forward.h>
#include <AK/Vector.h>
#include <LibMedia/DecoderError.h>
#include <LibMedia/Export.h>
#include <LibThreading/ConditionVariable.h>
#include <LibThreading/Mutex.h>

#include <LibMedia/ByteStream.h>

namespace Media {

class MEDIA_API IncrementallyPopulatedStream final : public ByteStream {
public:
    static NonnullRefPtr<IncrementallyPopulatedStream> create_empty();
    static NonnullRefPtr<IncrementallyPopulatedStream> create_from_buffer(ByteBuffer&&);

    virtual ~IncrementallyPopulatedStream() override = default;

    void append(ByteBuffer&&);
    void close();
    void discard_leading_data(size_t);

    virtual u64 size() override;
    u64 current_size();
    void set_expected_size(u64);

    virtual NonnullRefPtr<ByteStreamCursor> create_cursor() override;

    class Cursor final : public ByteStreamCursor {
    public:
        Cursor(NonnullRefPtr<IncrementallyPopulatedStream> stream)
            : m_stream(move(stream))
        {
        }

        virtual ~Cursor() override = default;

        virtual DecoderErrorOr<void> seek(size_t position, SeekMode mode) override;
        virtual DecoderErrorOr<size_t> read_into(Bytes bytes) override;
        virtual DecoderErrorOr<size_t> read_some(Bytes bytes) override;

        virtual size_t position() const override { return m_position; }
        virtual u64 size() const override { return m_stream->size(); }

        virtual void abort() override;
        virtual void reset_abort() override { m_aborted = false; }

        virtual bool is_blocked() const override { return m_blocked; }

    private:
        friend class IncrementallyPopulatedStream;

        NonnullRefPtr<IncrementallyPopulatedStream> m_stream;
        size_t m_position { 0 };
        bool m_aborted { false };
        Atomic<bool> m_blocked { false };
    };

    /*
     * Note: implemented in cpp file 
     */ 
     // auto create_cursor() -> implemented via virtual create_cursor() implemented in cpp
     // We need to keep the public method compatible or just let the virtual one handle it.
     // The virtual one returns NonnullRefPtr<ByteStreamCursor>.
     // We should probably allow creating the specific cursor too if needed, but for now virtual override is enough.

private:
    IncrementallyPopulatedStream(ByteBuffer buffer, bool is_complete);

    friend class Cursor;

    enum class AllowPositionAtEnd {
        Yes,
        No,
    };
    enum class PartialRead {
        Yes,
        No,
    };
    DecoderErrorOr<size_t> read_at(Cursor&, size_t position, Bytes&, AllowPositionAtEnd, PartialRead = PartialRead::No);

    Threading::Mutex m_mutex;
    Threading::ConditionVariable m_state_changed { m_mutex };
    Vector<ByteBuffer> m_chunks;
    size_t m_dropped_bytes { 0 };
    size_t m_buffered_size { 0 };
    Optional<u64> m_expected_size;
    Atomic<bool> m_closed { false };
};

}
