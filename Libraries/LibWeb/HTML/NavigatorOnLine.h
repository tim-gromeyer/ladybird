/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Debug.h>

namespace Web::HTML {

class NavigatorOnLineMixin {
public:
    // https://html.spec.whatwg.org/multipage/system-state.html#dom-navigator-online
    // FIXME: Reflect actual connectivity status.
    bool on_line() const
    {
        dbgln("NavigatorOnLineMixin::on_line returns true");
        return true;
    }
};

}
