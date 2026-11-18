#pragma once

namespace messier {

// The exception boundary of the window (0.1.10, MS-R37). Every call from the host,
// VSTGUI or the system into the window's own code goes through one of these: drawing,
// mouse and wheel, drag and drop, the timer, the file dialog's answer, sizes and the
// editor's own callbacks. An exception (in practice std::bad_alloc for a text or a
// path) ends that call: the window keeps what it shows and catches up on a later call.
// Nothing leaves into VSTGUI or the host.
template <typename Body>
void Guarded(Body&& body) noexcept {
    try {
        body();
    } catch(...) {
    }
}

template <typename Result, typename Body>
Result Guarded(Result fallback, Body&& body) noexcept {
    try {
        return body();
    } catch(...) {
        return fallback;
    }
}

} // namespace messier
