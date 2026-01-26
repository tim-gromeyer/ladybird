/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibMedia/IncrementallyPopulatedStream.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-sourcebuffer
class SourceBuffer : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBuffer, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBuffer);

public:
    void set_onupdatestart(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdatestart();

    void set_onupdate(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdate();

    void set_onupdateend(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onupdateend();

    void set_onerror(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onerror();

    void set_onabort(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onabort();

    WebIDL::ExceptionOr<void> append_buffer(GC::Root<WebIDL::BufferSource> const& data);
    WebIDL::ExceptionOr<void> abort();
    WebIDL::ExceptionOr<void> change_type(String const& type);
    WebIDL::ExceptionOr<void> remove(double start, double end);

    double append_window_start() const { return m_append_window_start; }
    void set_append_window_start(double value) { m_append_window_start = value; }

    double append_window_end() const { return m_append_window_end; }
    void set_append_window_end(double value) { m_append_window_end = value; }

    bool updating() const { return m_updating; }
    GC::Ref<HTML::TimeRanges> buffered() const { return *m_buffered; }

    RefPtr<Media::IncrementallyPopulatedStream> stream() const { return m_stream; }

    void queue_a_media_element_task(Function<void()>);

protected:
    SourceBuffer(JS::Realm&, RefPtr<Media::IncrementallyPopulatedStream>, GC::Ptr<MediaSource>, String mime_type);

    virtual ~SourceBuffer() override;

    virtual void initialize(JS::Realm&) override;

    virtual void visit_edges(Cell::Visitor&) override;
    
    struct ParsedTiming {
        double start { NAN };
        double duration { NAN };
    };

    struct ChunkInfo {
        double start_time;
        size_t byte_offset;
        size_t size;
    };

    // WebM Helpers
    ParsedTiming parse_webm_timestamps(ReadonlyBytes);
    // MP4 Helpers
    ParsedTiming parse_mp4_timestamps(ReadonlyBytes);

private:
    RefPtr<Media::IncrementallyPopulatedStream> m_stream;
    Vector<ChunkInfo> m_appended_chunks;
    bool m_updating { false };
    GC::Ptr<HTML::TimeRanges> m_buffered;
    GC::Ptr<MediaSource> m_media_source;

    double m_append_window_start { 0 };
    double m_append_window_end { INFINITY };

    String m_mime_type;
    u64 m_webm_timecode_scale { 1000000 };
    u64 m_mp4_timescale { 0 };
    double m_last_parsed_timestamp { NAN };
};

}
