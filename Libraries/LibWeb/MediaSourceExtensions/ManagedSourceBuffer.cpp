/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/ManagedSourceBufferPrototype.h>
#include <LibWeb/MediaSourceExtensions/EventNames.h>
#include <LibWeb/MediaSourceExtensions/ManagedSourceBuffer.h>
#include <LibMedia/IncrementallyPopulatedStream.h>

namespace Web::MediaSourceExtensions {

GC_DEFINE_ALLOCATOR(ManagedSourceBuffer);

ManagedSourceBuffer::ManagedSourceBuffer(JS::Realm& realm, RefPtr<Media::IncrementallyPopulatedStream> stream, GC::Ptr<MediaSource> media_source, String mime_type)
    : SourceBuffer(realm, move(stream), media_source, move(mime_type))
{
}

ManagedSourceBuffer::~ManagedSourceBuffer() = default;

void ManagedSourceBuffer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ManagedSourceBuffer);
    Base::initialize(realm);
}

void ManagedSourceBuffer::set_onstartstreaming(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::startstreaming, value);
}

GC::Ptr<WebIDL::CallbackType> ManagedSourceBuffer::onstartstreaming()
{
    return event_handler_attribute(EventNames::startstreaming);
}

void ManagedSourceBuffer::set_onendstreaming(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::endstreaming, value);
}

GC::Ptr<WebIDL::CallbackType> ManagedSourceBuffer::onendstreaming()
{
    return event_handler_attribute(EventNames::endstreaming);
}

void ManagedSourceBuffer::set_onbufferedchange(GC::Ptr<WebIDL::CallbackType> value)
{
    set_event_handler_attribute(EventNames::bufferedchange, value);
}

GC::Ptr<WebIDL::CallbackType> ManagedSourceBuffer::onbufferedchange()
{
    return event_handler_attribute(EventNames::bufferedchange);
}

}
