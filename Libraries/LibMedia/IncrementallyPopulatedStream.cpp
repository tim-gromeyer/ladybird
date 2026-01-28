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

NonnullRefPtr<ByteStreamCursor> IncrementallyPopulatedStream::create_cursor()
{
    return adopt_ref(*new Cursor(NonnullRefPtr { *this }));
}

IncrementallyPopulatedStream::IncrementallyPopulatedStream(ByteBuffer buffer, bool is_complete)
    : m_closed(is_complete)
{
    if (!buffer.is_empty()) {
        m_buffered_size = buffer.size();
        m_chunks.append(move(buffer));
    }
}

void IncrementallyPopulatedStream::append(ByteBuffer&& buffer)
{
    Threading::MutexLocker locker { m_mutex };
    if (buffer.is_empty())
        return;

    m_buffered_size += buffer.size();
    m_chunks.append(move(buffer));
    m_state_changed.broadcast();
}

void IncrementallyPopulatedStream::discard_leading_data(size_t count)
{
    Threading::MutexLocker locker { m_mutex };
    if (count == 0)
        return;

    size_t to_remove = min(count, m_buffered_size);
    if (to_remove == 0)
        return;

    size_t remaining_to_remove = to_remove;

    // Remove full chunks from the front
    while (!m_chunks.is_empty()) {
        auto& first_chunk = m_chunks.first();
        if (remaining_to_remove >= first_chunk.size()) {
            remaining_to_remove -= first_chunk.size();
            m_chunks.remove(0);
        } else {
            // Partial removal from the first chunk
            // This copies the remaining part, but it happens only once at the boundary.
            auto new_buffer_result = ByteBuffer::create_uninitialized(first_chunk.size() - remaining_to_remove);
            if (!new_buffer_result.is_error()) {
                auto new_buffer = new_buffer_result.release_value();
                first_chunk.bytes().slice(remaining_to_remove).copy_to(new_buffer);
                m_chunks[0] = move(new_buffer);
            }
            remaining_to_remove = 0;
            break;
        }
    }

    m_dropped_bytes += to_remove;
    m_buffered_size -= to_remove;
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
        return m_dropped_bytes + m_buffered_size;
    return m_expected_size.value();
}

u64 IncrementallyPopulatedStream::current_size()
{
    Threading::MutexLocker locker { m_mutex };
    return m_dropped_bytes + m_buffered_size;
}

void IncrementallyPopulatedStream::set_expected_size(u64 expected_size)
{
    Threading::MutexLocker locker { m_mutex };
    m_expected_size = expected_size;
    // We don't pre-allocate chunks as we don't know their individual sizes.
    m_state_changed.broadcast();
}

DecoderErrorOr<size_t> IncrementallyPopulatedStream::read_at(Cursor& consumer, size_t position, Bytes& bytes, AllowPositionAtEnd allow_position_at_end, PartialRead partial_read)
{
    Threading::MutexLocker locker { m_mutex };

    // 1. Check eviction
    if (position < m_dropped_bytes) {
        return DecoderError::with_description(DecoderErrorCategory::IO, "Read access to evicted data"sv);
    }
    size_t relative_position = position - m_dropped_bytes;

    // 2. Wait for data
    // If partial_read is Yes, we only wait until we have AT LEAST 1 byte (or 0 if size=0).
    size_t required_bytes = (partial_read == PartialRead::Yes) ? min(bytes.size(), 1ul) : bytes.size();

    while (relative_position + required_bytes > m_buffered_size && !m_closed && !consumer.m_aborted) {
        consumer.m_blocked = true;
        m_state_changed.wait();
        consumer.m_blocked = false;

        // Re-check eviction after wait
        if (position < m_dropped_bytes)
            return DecoderError::with_description(DecoderErrorCategory::IO, "Read access to evicted data"sv);
        relative_position = position - m_dropped_bytes;
    }

    if (consumer.m_aborted)
        return DecoderError::with_description(DecoderErrorCategory::Aborted, "Blocking read was aborted"sv);

    if (relative_position > m_buffered_size || (allow_position_at_end == AllowPositionAtEnd::No && relative_position == m_buffered_size))
        return DecoderError::with_description(DecoderErrorCategory::EndOfStream, "Blocking read reached end of stream"sv);

    // 3. Read from chunks
    size_t bytes_to_read = min(bytes.size(), m_buffered_size - relative_position);

    size_t bytes_read = 0;
    size_t chunk_offset = 0;

    // Find starting chunk
    for (auto& chunk : m_chunks) {
        if (relative_position < chunk_offset + chunk.size()) {
            // Found start chunk
            size_t offset_in_chunk = relative_position - chunk_offset;
            size_t available_in_chunk = chunk.size() - offset_in_chunk;
            size_t to_copy = min(bytes_to_read - bytes_read, available_in_chunk);

            chunk.bytes().slice(offset_in_chunk, to_copy).copy_to(bytes.slice(bytes_read, to_copy));
            bytes_read += to_copy;
            relative_position += to_copy;

            if (bytes_read == bytes_to_read)
                break;
        }
        chunk_offset += chunk.size();
    }

    return bytes_read;
}

DecoderErrorOr<void> IncrementallyPopulatedStream::Cursor::seek(size_t offset, ByteStreamCursor::SeekMode mode)
{
    size_t new_position = m_position;

    switch (mode) {
    case ByteStreamCursor::SeekMode::SetPosition:
        new_position = offset;
        break;
    case ByteStreamCursor::SeekMode::FromCurrentPosition:
        new_position += offset;
        break;
    case ByteStreamCursor::SeekMode::FromEndPosition:
        new_position = this->size() + offset;
        break;
    default:
        VERIFY_NOT_REACHED();
    }

    // Optimization: Don't read just to seek. Just verify validity?
    // Original implementation did a read_at to verify. Let's keep strict validation for now.
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

DecoderErrorOr<size_t> IncrementallyPopulatedStream::Cursor::read_some(Bytes bytes)
{
    auto read_count = TRY(m_stream->read_at(*this, m_position, bytes, AllowPositionAtEnd::No, PartialRead::Yes));
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
