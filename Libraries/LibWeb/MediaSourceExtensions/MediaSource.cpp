/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/MediaSourcePrototype.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/MediaSource.h>
#include <LibWeb/MimeSniff/MimeType.h>
#include <LibWeb/HTML/HTMLMediaElement.h>
#include <LibCore/EventLoop.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(MediaSource);

WebIDL::ExceptionOr<GC::Ref<MediaSource>> MediaSource::construct_impl(JS::Realm& realm)
{
    return realm.create<MediaSource>(realm);
}

MediaSource::MediaSource(JS::Realm& realm)
    : DOM::EventTarget(realm)
{
}

MediaSource::~MediaSource() = default;

void MediaSource::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(MediaSource);
    Base::initialize(realm);

    m_source_buffers = realm.create<SourceBufferList>(realm);
    m_active_source_buffers = realm.create<SourceBufferList>(realm);
}

void MediaSource::set_onsourceopen(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::sourceopen, value);
}

GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceopen()
{
    return event_handler_attribute(EventNames::sourceopen);
}

void MediaSource::set_onsourceended(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::sourceended, value);
}

GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceended()
{
    return event_handler_attribute(EventNames::sourceended);
}

void MediaSource::set_onsourceclose(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::sourceclose, value);
}

GC::Ptr<WebIDL::CallbackType> MediaSource::onsourceclose()
{
    return event_handler_attribute(EventNames::sourceclose);
}

void MediaSource::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_source_buffers);
    visitor.visit(m_active_source_buffers);
}

void MediaSource::set_ready_state(Bindings::ReadyState ready_state)
{
    if (m_ready_state == ready_state)
        return;
    m_ready_state = ready_state;

    if (m_ready_state == Bindings::ReadyState::Open) {
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), EventNames::sourceopen));
        });
    } else if (m_ready_state == Bindings::ReadyState::Ended) {
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), EventNames::sourceended));
        });
    } else if (m_ready_state == Bindings::ReadyState::Closed) {
        queue_a_media_element_task([this] {
            dispatch_event(DOM::Event::create(realm(), EventNames::sourceclose));
        });
    }
}

void MediaSource::set_duration(double duration)
{
    if (m_duration == duration)
        return;
    m_duration = duration;
    // FIXME: Notify media element about duration change.
}

WebIDL::ExceptionOr<GC::Ref<SourceBuffer>> MediaSource::add_source_buffer(String const& type)
{
    auto& realm = this->realm();

    // 4. Create a new SourceBuffer object and add it to the sourceBuffers attribute.
    auto stream = Media::IncrementallyPopulatedStream::create_empty();
    auto source_buffer = realm.create<SourceBuffer>(realm, stream, this, type);
    m_source_buffers->add(source_buffer);

    // 5. Add it to the activeSourceBuffers attribute.
    m_active_source_buffers->add(source_buffer);

    // Queue a task to fire an event named addsourcebuffer at the SourceBufferList in sourceBuffers.
    queue_a_media_element_task([this] {
        m_source_buffers->dispatch_event(DOM::Event::create(this->realm(), EventNames::addsourcebuffer));
        m_active_source_buffers->dispatch_event(DOM::Event::create(this->realm(), EventNames::addsourcebuffer));
    });

    // 6. Return the new SourceBuffer object.
    return source_buffer;
}

WebIDL::ExceptionOr<void> MediaSource::remove_source_buffer(SourceBuffer& source_buffer)
{
    // 1. If sourceBuffer is not in sourceBuffers, then return.
    // 2. If the updating attribute of sourceBuffer is true, then abort the stream append loop of sourceBuffer.
    // 3. ... (simplified)
    m_source_buffers->remove(source_buffer);
    m_active_source_buffers->remove(source_buffer);

    // Queue a task to fire an event named removesourcebuffer at the SourceBufferList in sourceBuffers.
    queue_a_media_element_task([this] {
        m_source_buffers->dispatch_event(DOM::Event::create(this->realm(), EventNames::removesourcebuffer));
        m_active_source_buffers->dispatch_event(DOM::Event::create(this->realm(), EventNames::removesourcebuffer));
    });

    return {};
}

void MediaSource::end_of_stream(Optional<Bindings::EndOfStreamError> error)
{
    if (m_ready_state != Bindings::ReadyState::Open)
        return;
    set_ready_state(Bindings::ReadyState::Ended);
    if (error.has_value()) {
        // FIXME: Handle error
    }
}

void MediaSource::queue_a_media_element_task(Function<void()> task)
{
    // FIXME: This should use the media element event task source.
    Core::deferred_invoke(move(task));
}

// https://w3c.github.io/media-source/#dom-mediasource-istypesupported
bool MediaSource::is_type_supported(JS::VM&, String const& type)
{
    // ... (keep existing implementation)
    // 1. If type is an empty string, then return false.
    if (type.is_empty())
        return false;

    // 2. If type does not contain a valid MIME type string, then return false.
    auto mime_type = MimeSniff::MimeType::parse(type);
    if (!mime_type.has_value())
        return false;

    // FIXME: 3. If type contains a media type or media subtype that the MediaSource does not support, then
    //    return false.
    if (mime_type->type() != "audio" && mime_type->type() != "video")
        return false;

    // FIXME: 4. If type contains a codec that the MediaSource does not support, then return false.

    // FIXME: 5. If the MediaSource does not support the specified combination of media type, media
    //    subtype, and codecs then return false.

    // 6. Return true.
    return true;
}

}
