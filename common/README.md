# Common

Code used by more than one plug-in: CMake helpers, DSP utilities, shared GUI
parts. Code moves here when a second plug-in needs it, not before.

Empty so far. Torus Classic and Mendelejew each carry their own copy of the
streaming adapter, the parameter hand-over between the threads, the strict
text input and the windowing; those are the candidates. They were not moved
here with Mendelejew because that would have changed Torus Classic while its
review is still open (see `docs/PLANUNG.md`).
