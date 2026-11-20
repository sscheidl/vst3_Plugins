#pragma once

#include <memory>

namespace messier {

// Starting the LOAD... file dialog (0.1.11, MS-R39). `open` is set while the dialog is
// open and blocks a second one. `run` starts the dialog and returns what the platform
// says; the dialog's answer sets `*answered` (VSTGUI calls it on cancel too, and then
// still returns false on Windows). If the dialog did not start and no answer came, or
// `run` threw before an answer, no answer will come: `open` is reset so that LOAD...
// works again, and `give_back` returns the reference VSTGUI's run() took for the
// answer. True if the dialog started or has answered.
//
// `open` belongs to the editor, which the host may destroy while the dialog's loop runs;
// `*alive` is false then, and `open` is no longer touched (GA.2 of the GPT review,
// 0.2.10). `give_back` must not need the editor.
template <typename Run, typename GiveBack>
bool StartDialog(bool& open, const std::shared_ptr<bool>& alive, const std::shared_ptr<bool>& answered, Run&& run,
                 GiveBack&& give_back) {
    open = true;
    bool started = false;
    try {
        started = run();
    } catch(...) {
        if(!*answered) {
            if(*alive)
                open = false;
            give_back();
        }
        throw;
    }
    if(started || *answered)
        return true;
    if(*alive)
        open = false;
    give_back();
    return false;
}

} // namespace messier
