# Instruments

Plug-ins that turn MIDI into sound. They have no audio input and register in
the VST3 category `Instrument`.

| Project | Origin | Status |
| --- | --- | --- |
| [toy-piano-classic](toy-piano-classic/) | Port of `instruments/toy-piano` from Daisy-Seed | 0.3.2: builds, validated, tested and code-reviewed; 0.1.0 loads in Studio One, not yet listened to |
| [torus-classic](torus-classic/) | Port of `instruments/physical-modeling` (Rings/Torus) from Daisy-Seed, without its reverb | 0.2.0: builds, validated and tested automatically; 0.1.0 loads and plays in Studio One; Codex's four findings fixed in 0.1.1, not yet re-reviewed; the additions of 0.2.0 not yet tried in a DAW |
| [elements](elements/) | Mendelejew: port of the Mutable Instruments Elements DSP from pichenettes/eurorack, at a true internal 48 kHz | 0.1.0: builds, validated and tested automatically against the original code; not yet loaded in a DAW, not reviewed |
