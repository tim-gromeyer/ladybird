/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/MediaSourceExtensions/SourceBuffer.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#managedsourcebuffer-interface
class ManagedSourceBuffer : public SourceBuffer {
    WEB_PLATFORM_OBJECT(ManagedSourceBuffer, SourceBuffer);
    GC_DECLARE_ALLOCATOR(ManagedSourceBuffer);

public:
    virtual ~ManagedSourceBuffer() override;

    void set_onbufferedchange(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onbufferedchange();

    void set_onstartstreaming(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onstartstreaming();

    void set_onendstreaming(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onendstreaming();

private:
    ManagedSourceBuffer(JS::Realm&, RefPtr<Media::IncrementallyPopulatedStream>, GC::Ptr<MediaSource>, String mime_type);

    virtual void initialize(JS::Realm&) override;
};

}
