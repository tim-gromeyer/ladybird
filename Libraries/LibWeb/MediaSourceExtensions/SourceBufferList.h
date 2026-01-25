/*
 * Copyright (c) 2024, Jelle Raaijmakers <jelle@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/DOM/EventTarget.h>

namespace Web::MediaSourceExtensions {

// https://w3c.github.io/media-source/#dom-sourcebufferlist
class SourceBufferList : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(SourceBufferList, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(SourceBufferList);

public:
    void set_onaddsourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onaddsourcebuffer();

    void set_onremovesourcebuffer(GC::Ptr<WebIDL::CallbackType>);
    GC::Ptr<WebIDL::CallbackType> onremovesourcebuffer();

    size_t length() const { return m_source_buffers.size(); }
    GC::Ptr<SourceBuffer> item(size_t index) const { return index < m_source_buffers.size() ? m_source_buffers[index] : nullptr; }

    void add(GC::Ref<SourceBuffer> source_buffer) { m_source_buffers.append(source_buffer); }
    void remove(SourceBuffer& source_buffer) { m_source_buffers.remove_first_matching([&](auto& entry) { return entry.ptr() == &source_buffer; }); }

protected:
    virtual void visit_edges(Cell::Visitor&) override;

private:
    SourceBufferList(JS::Realm&);

    virtual ~SourceBufferList() override;

    virtual void initialize(JS::Realm&) override;

    Vector<GC::Ref<SourceBuffer>> m_source_buffers;
};

}
