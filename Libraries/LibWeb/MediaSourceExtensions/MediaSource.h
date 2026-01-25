/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>
#include <LibWeb/MediaSourceExtensions/SourceBufferList.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-mediasource
class MediaSource : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaSource, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaSource);

public:
    [[nodiscard]] static WebIDL::ExceptionOr<GC::Ref<MediaSource>> construct_impl(JS::Realm&);

    // https://w3c.github.io/media-source/#dom-mediasource-canconstructindedicatedworker
    static bool can_construct_in_dedicated_worker(JS::VM&) { return true; }

    void set_onsourceopen(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceopen();

    void set_onsourceended(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceended();

    void set_onsourceclose(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onsourceclose();

    static bool is_type_supported(JS::VM&, String const&);

    WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> add_source_buffer(String const& type);
    WebIDL::ExceptionOr<void> remove_source_buffer(SourceBuffer& source_buffer);
    void end_of_stream(Optional<Bindings::EndOfStreamError> error);

    GC::Ref<SourceBufferList> source_buffers() const { return *m_source_buffers; }
    GC::Ref<SourceBufferList> active_source_buffers() const { return *m_active_source_buffers; }

    double duration() const { return m_duration; }
    void set_duration(double duration);

    Bindings::ReadyState ready_state() const { return m_ready_state; }
    void set_ready_state(Bindings::ReadyState ready_state);

    void queue_a_media_element_task(Function<void()>);

protected:
    MediaSource(JS::Realm&);

    virtual ~MediaSource() override;

    virtual void initialize(JS::Realm&) override;

    virtual void visit_edges(Cell::Visitor&) override;

private:
    GC::Ptr<SourceBufferList> m_source_buffers;
    GC::Ptr<SourceBufferList> m_active_source_buffers;
    Bindings::ReadyState m_ready_state { Bindings::ReadyState::Closed };
    double m_duration { NAN };
};

}
