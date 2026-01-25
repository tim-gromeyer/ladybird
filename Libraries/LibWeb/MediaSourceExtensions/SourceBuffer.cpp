/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/SourceBufferPrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/HTML/TimeRanges.h>
#include <LibCore/EventLoop.h>
#include <AK/Endian.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::MediaSourceExtensions {

using namespace Web::WebIDL;

GC_DEFINE_ALLOCATOR(SourceBuffer);

SourceBuffer::SourceBuffer(JS::Realm& realm, RefPtr<Media::IncrementallyPopulatedStream> stream, GC::Ptr<MediaSource> media_source, String mime_type)
    : DOM::EventTarget(realm)
    , m_stream(move(stream))
    , m_media_source(media_source)
    , m_mime_type(move(mime_type))
{
    m_buffered = realm.create<HTML::TimeRanges>(realm);
}

SourceBuffer::~SourceBuffer() = default;

void SourceBuffer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(SourceBuffer);
    Base::initialize(realm);
}

void SourceBuffer::set_onupdatestart(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::updatestart, value);
}

GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdatestart()
{
    return event_handler_attribute(EventNames::updatestart);
}

void SourceBuffer::set_onupdate(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::update, value);
}

GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdate()
{
    return event_handler_attribute(EventNames::update);
}

void SourceBuffer::set_onupdateend(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::updateend, value);
}

GC::Ptr<WebIDL::CallbackType> SourceBuffer::onupdateend()
{
    return event_handler_attribute(EventNames::updateend);
}

void SourceBuffer::set_onerror(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::error, value);
}

GC::Ptr<WebIDL::CallbackType> SourceBuffer::onerror()
{
    return event_handler_attribute(EventNames::error);
}

void SourceBuffer::set_onabort(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::abort, value);
}

GC::Ptr<WebIDL::CallbackType> SourceBuffer::onabort()
{
    return event_handler_attribute(EventNames::abort);
}

Web::WebIDL::ExceptionOr<void> SourceBuffer::append_buffer(GC::Root<Web::WebIDL::BufferSource> const& data)
{
    // FIXME: 1. If this object has been removed from the sourceBuffers attribute of a MediaSource object, then throw an InvalidStateError.
    // 2. If the updating attribute is true, then throw an InvalidStateError.
    if (m_updating)
        return Web::WebIDL::InvalidStateError::create(realm(), "SourceBuffer is currently updating"_utf16);

    // 3. If the readyState attribute of the parent media source is "closed", then throw an InvalidStateError.
    // 4. If the buffer full flag is true, then throw a QuotaExceededError.

    // 5. ... (snip) ...

    auto buffer_or_error = Web::WebIDL::get_buffer_source_copy(data->raw_object());
    if (buffer_or_error.is_error())
        return {};
    auto buffer = buffer_or_error.release_value();

    ParsedTiming timing;
    if (m_mime_type.contains("webm"sv)) {
        timing = parse_webm_timestamps(buffer);
    } else if (m_mime_type.contains("mp4"sv)) {
        timing = parse_mp4_timestamps(buffer);
    }

    m_updating = true;
    m_stream->append(move(buffer));

    queue_a_media_element_task([this, timing] {
        m_updating = false;
        double duration = m_media_source->duration();
        if (isnan(duration) || duration <= 0)
            duration = 1000000.0;
        
        double start_time = timing.start;
        if (isnan(start_time)) {
             // If we didn't find a new Timecode/Cluster in this chunk,
             // try to use the last known timestamp to keep extending the buffer.
             // This is a heuristic for chunked appends.
             start_time = m_last_parsed_timestamp;
        }

        if (!isnan(start_time)) {
             m_last_parsed_timestamp = start_time;
             // Use a generous window (e.g. 5s) to ensure we cover the whole cluster duration
             // even if we only parsed the start. Overlapping ranges will be merged.
             double end = start_time + 5.0; 
             m_buffered->add_range(start_time, end);
        }

        dispatch_event(DOM::Event::create(realm(), EventNames::updatestart));
        dispatch_event(DOM::Event::create(realm(), EventNames::update));
        dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
    });

    return {};
}

// Reader for EBML Variable Size Integer (VINT)
struct VintResult {
    u64 value;
    size_t length;
};

static Optional<VintResult> read_ebml_vint(ReadonlyBytes data, size_t offset)
{
    if (offset >= data.size()) return {};
    u8 first_byte = data[offset];
    u8 length = 0;
    u64 value_mask = 0;

    if (first_byte & 0x80) { length = 1; value_mask = 0x7F; }
    else if (first_byte & 0x40) { length = 2; value_mask = 0x3F; }
    else if (first_byte & 0x20) { length = 3; value_mask = 0x1F; }
    else if (first_byte & 0x10) { length = 4; value_mask = 0x0F; }
    else if (first_byte & 0x08) { length = 5; value_mask = 0x07; }
    else if (first_byte & 0x04) { length = 6; value_mask = 0x03; }
    else if (first_byte & 0x02) { length = 7; value_mask = 0x01; }
    else if (first_byte & 0x01) { length = 8; value_mask = 0x00; }
    else return {}; // Invalid VINT

    if (offset + length > data.size()) return {};

    u64 value = first_byte & value_mask;
    for (size_t i = 1; i < length; ++i) {
        value = (value << 8) | data[offset + i];
    }

    return VintResult { value, length };
}

static Optional<u64> read_ebml_uint(ReadonlyBytes data, size_t offset, size_t size)
{
    if (offset + size > data.size()) return {};
    u64 value = 0;
    for (size_t i = 0; i < size; ++i) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

SourceBuffer::ParsedTiming SourceBuffer::parse_webm_timestamps(ReadonlyBytes data)
{
    ParsedTiming timing;
    
    // Scan for TimecodeScale if unknown
    // TimecodeScale ID: 2A D7 B1 (3 bytes)
    // We scan 3-byte window
    if (m_webm_timecode_scale == 1000000) { // Default
        for (size_t i = 0; i + 7 <= data.size(); ++i) { // +7 to read value safely
             if (data[i] == 0x2A && data[i+1] == 0xD7 && data[i+2] == 0xB1) {
                 size_t offset = i + 3;
                 auto size_res = read_ebml_vint(data, offset);
                 if (size_res.has_value()) {
                     offset += size_res->length;
                     auto val = read_ebml_uint(data, offset, size_res->value);
                     if (val.has_value()) {
                         m_webm_timecode_scale = *val;
                         break; // Found it
                     }
                 }
             }
        }
    }

    // Scan for Cluster(s)
    // Cluster ID: 1F 43 B6 75 (4 bytes)
    for (size_t i = 0; i + 12 <= data.size(); ++i) {
        if (data[i] == 0x1F && data[i+1] == 0x43 && data[i+2] == 0xB6 && data[i+3] == 0x75) {
             // Found Cluster
             size_t offset = i + 4;
             auto cluster_size_res = read_ebml_vint(data, offset);
             if (!cluster_size_res.has_value()) continue;
             
             // Content starts at
             size_t content_offset = offset + cluster_size_res->length;
             size_t content_end = content_offset + cluster_size_res->value;
             
             // Timecode ID: E7 (1 byte)
             // Should be the first element, so scan a bit of the content
             size_t scan_limit = min(content_end, content_offset + 64);
             
             for (size_t j = content_offset; j + 4 < scan_limit && j < data.size(); ++j) {
                  if (data[j] == 0xE7) {
                      // Found Timecode
                      size_t tc_offset = j + 1;
                      auto tc_size_res = read_ebml_vint(data, tc_offset);
                      if (tc_size_res.has_value()) {
                           tc_offset += tc_size_res->length;
                           auto tc_val = read_ebml_uint(data, tc_offset, tc_size_res->value);
                           if (tc_val.has_value()) {
                               u64 timecode = *tc_val;
                               double timestamp = (double)timecode * (double)m_webm_timecode_scale / 1'000'000'000.0;
                               if (isnan(timing.start) || timestamp < timing.start) {
                                   timing.start = timestamp;
                               }
                               // Continue searching for more Clusters in this chunk?
                               // Usually unnecessary for typical chunk sizes, but let's just use the first valid one found for now.
                               // Ideally we'd map all of them, but SourceBuffer append structure assumes one timing update.
                               goto found_timing;
                           }
                      }
                  }
             }
        }
    }

found_timing:
    return timing;
}

SourceBuffer::ParsedTiming SourceBuffer::parse_mp4_timestamps(ReadonlyBytes data)
{
    ParsedTiming timing;
    size_t offset = 0;

    // Simple Box scanner
    while (offset + 8 <= data.size()) {
        u32 box_size = AK::convert_between_host_and_big_endian(
            *reinterpret_cast<u32 const*>(data.offset_pointer(offset)));
        
        u32 box_type = AK::convert_between_host_and_big_endian(
            *reinterpret_cast<u32 const*>(data.offset_pointer(offset + 4)));

        if (box_size == 0) break; // extending to end of file
        if (box_size == 1) {
            // Large size (64-bit), read next 8 bytes
            if (offset + 16 > data.size()) break;
            // skip for now, we only need basic boxes
            offset += 16; 
            continue;
        }

        // mdhd (Media Header) - contains timescale
        if (box_type == 0x6D646864) { // 'mdhd'
             size_t local_offset = offset + 8;
             if (local_offset + 4 <= data.size()) {
                 u8 version = data[local_offset];
                 local_offset += 4; // version + flags
                 
                 // creation/mod times
                 if (version == 1) local_offset += 16;
                 else local_offset += 8;
                 
                 if (local_offset + 4 <= data.size()) {
                      m_mp4_timescale = AK::convert_between_host_and_big_endian(
                          *reinterpret_cast<u32 const*>(data.offset_pointer(local_offset)));
                 }
             }
        }
        
        // tfdt (Track Fragment Decode Time) - contains base media decode time
        if (box_type == 0x74666474) { // 'tfdt'
            size_t local_offset = offset + 8;
            if (local_offset + 4 <= data.size()) {
                u8 version = data[local_offset];
                local_offset += 4; // version + flags
                
                u64 base_time = 0;
                if (version == 1) {
                    if (local_offset + 8 <= data.size())
                         base_time = AK::convert_between_host_and_big_endian(
                             *reinterpret_cast<u64 const*>(data.offset_pointer(local_offset)));
                } else {
                    if (local_offset + 4 <= data.size())
                         base_time = AK::convert_between_host_and_big_endian(
                             *reinterpret_cast<u32 const*>(data.offset_pointer(local_offset)));
                }
                
                if (m_mp4_timescale > 0) {
                    timing.start = (double)base_time / (double)m_mp4_timescale;
                }
            }
        }

        // Scan inside containers: moov, trahk, mdia, minf, stbl? 
        // Or moof, traf?
        // Basic linear scan will skip contents if we jump by box_size.
        // We need to recurse or check known containers.
        bool is_container = (box_type == 0x6D6F6F76 || // moov
                             box_type == 0x7472616B || // trak
                             box_type == 0x6D646961 || // mdia
                             box_type == 0x6D6F6F66 || // moof
                             box_type == 0x74726166);  // traf
        
        if (is_container) {
            offset += 8; // Enter container
        } else {
            offset += box_size; // Skip box
        }
    }
    return timing; 
}

Web::WebIDL::ExceptionOr<void> SourceBuffer::abort()
{
    if (m_updating) {
        m_updating = false;
        // FIXME: Abort the stream append loop.
        dispatch_event(DOM::Event::create(realm(), EventNames::abort));
        dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
    }
    return {};
}

Web::WebIDL::ExceptionOr<void> SourceBuffer::change_type(String const& type)
{
    if (m_updating)
        return Web::WebIDL::InvalidStateError::create(realm(), "SourceBuffer is currently updating"_utf16);

    if (type.is_empty())
        return Web::WebIDL::SimpleException { Web::WebIDL::SimpleExceptionType::TypeError, "Type must not be empty"sv };

    // FIXME: 4. If type contains a MIME type that is not supported or has codecs that are not supported, then throw a NotSupportedError.

    return {};
}

Web::WebIDL::ExceptionOr<void> SourceBuffer::remove([[maybe_unused]] double start, [[maybe_unused]] double end)
{
    if (m_updating)
        return Web::WebIDL::InvalidStateError::create(realm(), "SourceBuffer is currently updating"_utf16);

    // FIXME: 3. If duration is NaN, then throw a TypeError.
    // FIXME: 4. If start is negative or greater than duration, then throw a TypeError.
    // FIXME: 5. If end is less than or equal to start or end is NaN, then throw a TypeError.

    m_updating = true;
    queue_a_media_element_task([this] {
        dispatch_event(DOM::Event::create(realm(), EventNames::updatestart));
    });

    // FIXME: 6. Run the range removal algorithm.

    queue_a_media_element_task([this] {
        m_updating = false;
        dispatch_event(DOM::Event::create(realm(), EventNames::update));
        dispatch_event(DOM::Event::create(realm(), EventNames::updateend));
    });

    return {};
}

void SourceBuffer::queue_a_media_element_task(Function<void()> task)
{
    // FIXME: This should use the media element event task source.
    Core::deferred_invoke(move(task));
}

void SourceBuffer::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_buffered);
    visitor.visit(m_media_source);
}

}
