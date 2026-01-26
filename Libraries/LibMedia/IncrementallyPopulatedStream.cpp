/*
 * Copyright (c) 2025, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/RefPtr.h>
#include <LibMedia/IncrementallyPopulatedStream.h>

namespace Media {

NonnullRefPtr<IncrementallyPopulatedStream> IncrementallyPopulatedStream::create_empty()
{
    return adopt_ref(*new IncrementallyPopulatedStream({}, false));
}

NonnullRefPtr<IncrementallyPopulatedStream> IncrementallyPopulatedStream::create_from_buffer(ByteBuffer&& buffer)
{
    return adopt_ref(*new IncrementallyPopulatedStream(move(buffer), true));
}

void IncrementallyPopulatedStream::append(ByteBuffer&& buffer)
{
    Threading::MutexLocker locker { m_mutex };
    m_buffer.append(buffer);
    m_state_changed.broadcast();
}

void IncrementallyPopulatedStream::discard_leading_data(size_t count)
{
    Threading::MutexLocker locker { m_mutex };
    if (count == 0)
        return;

    // We can only discard bytes that are currently in the buffer.
    // If the caller asks to discard more than currently buffered (relative to previous drops),
    // we clamp or error. For robustness, we clamp to current buffer size.
    size_t to_remove = min(count, m_buffer.size());
    
    // Efficiently remove from the front. ByteBuffer remove is O(N) memmove, 
    // but this is expected to be called infrequently (once per segment/chunk eviction).
    // Note: ByteBuffer::slice() creates a view, but we want to reclaim memory.
    // ByteBuffer::remove(index, count) is available in AK.
    
    // We assume AK::ByteBuffer has overwrite/resize capabilities. 
    // A simplified way is to create a new buffer if we are removing a large chunk.
    if (to_remove > 0) {
        // Move the remaining bytes to the beginning?
        // AK::ByteBuffer doesn't have a simple 'remove_prefix'.
        // Let's rely on constructing a new buffer for now to ensure memory is actually freed.
        auto remaining_size = m_buffer.size() - to_remove;
        if (remaining_size == 0) {
            m_buffer.clear();
        } else {
             // Optimize: Check if we have a way to shift in place.
             // m_buffer.bytes().slice(to_remove).copy_to(m_buffer.bytes()); -- dangerous overlap
             // Vector-like erase from front:
             auto new_buffer = ByteBuffer::create_uninitialized(remaining_size);
             if (!new_buffer.is_error()) {
                 m_buffer.bytes().slice(to_remove).copy_to(new_buffer.value());
                 m_buffer = new_buffer.release_value();
             }
        }
        m_dropped_bytes += to_remove;
    }
}

void IncrementallyPopulatedStream::close()
{
    Threading::MutexLocker locker { m_mutex };
    m_closed = true;
    m_state_changed.broadcast();
}

u64 IncrementallyPopulatedStream::size()
{
    Threading::MutexLocker locker { m_mutex };
    while (!m_closed && !m_expected_size.has_value())
        m_state_changed.wait();
    if (m_closed)
        return m_dropped_bytes + m_buffer.size();
    return m_expected_size.value();
}

u64 IncrementallyPopulatedStream::current_size()
{
    Threading::MutexLocker locker { m_mutex };
    return m_dropped_bytes + m_buffer.size();
}

void IncrementallyPopulatedStream::set_expected_size(u64 expected_size)
{
    Threading::MutexLocker locker { m_mutex };
    m_expected_size = expected_size;
    m_buffer.ensure_capacity(expected_size);
    m_state_changed.broadcast();
}

DecoderErrorOr<size_t> IncrementallyPopulatedStream::read_at(Cursor& consumer, size_t position, Bytes& bytes, AllowPositionAtEnd allow_position_at_end)
{
    Threading::MutexLocker locker { m_mutex };

    // Adjust position relative to what we physically hold
    if (position < m_dropped_bytes) {
         // Trying to read data that has been evicted
         return DecoderError::with_description(DecoderErrorCategory::IO, "Read access to evicted data"sv);
    }
    size_t relative_position = position - m_dropped_bytes;

    while (relative_position + bytes.size() > m_buffer.size() && !m_closed && !consumer.m_aborted) {
        consumer.m_blocked = true;
        m_state_changed.wait();
        consumer.m_blocked = false;
        
        // Re-check dropped bytes after wake-up
        if (position < m_dropped_bytes)
            return DecoderError::with_description(DecoderErrorCategory::IO, "Read access to evicted data"sv);
        relative_position = position - m_dropped_bytes;
    }

    if (consumer.m_aborted)
        return DecoderError::with_description(DecoderErrorCategory::Aborted, "Blocking read was aborted"sv);
    if (relative_position > m_buffer.size() || (allow_position_at_end == AllowPositionAtEnd::No && relative_position == m_buffer.size()))
        return DecoderError::with_description(DecoderErrorCategory::EndOfStream, "Blocking read reached end of stream"sv);
    
    return m_buffer.bytes().slice(relative_position).copy_trimmed_to(bytes);
}

DecoderErrorOr<void> IncrementallyPopulatedStream::Cursor::seek(size_t offset, SeekMode mode)
{
    size_t new_position = m_position;

    switch (mode) {
    case SeekMode::SetPosition:
        new_position = offset;
        break;
    case SeekMode::FromCurrentPosition:
        new_position += offset;
        break;
    case SeekMode::FromEndPosition:
        new_position = this->size() + offset;
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    Bytes empty;
    TRY(m_stream->read_at(*this, new_position, empty, AllowPositionAtEnd::Yes));

    m_position = new_position;
    return {};
}

DecoderErrorOr<size_t> IncrementallyPopulatedStream::Cursor::read_into(Bytes bytes)
{
    auto read_count = TRY(m_stream->read_at(*this, m_position, bytes, AllowPositionAtEnd::No));
    m_position += read_count;
    return read_count;
}

void IncrementallyPopulatedStream::Cursor::abort()
{
    Threading::MutexLocker locker { m_stream->m_mutex };
    m_aborted = true;
    m_stream->m_state_changed.broadcast();
}

}
