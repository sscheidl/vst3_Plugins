# Instruments

Plug-ins that turn MIDI into sound. They have no audio input and register in
the VST3 category `Instrument`.

| Project | Origin | Status |
| --- | --- | --- |
| [toy-piano-classic](toy-piano-classic/) | Port of `instruments/toy-piano` from Daisy-Seed | 0.3.2: builds, validated, tested and code-reviewed; 0.1.0 loads in Studio One, not yet listened to |
| [torus-classic](torus-classic/) | Kekulé: port of `instruments/physical-modeling` (Rings/Torus) from Daisy-Seed, without its reverb | 0.2.2: builds, validated and tested automatically; 0.1.0 loads and plays in Studio One; Codex released 0.2.1 for the DAW test (one P3 point open); the additions of 0.2.0 not yet tried in a DAW |
| [elements](elements/) | Mendelejew: port of the Mutable Instruments Elements DSP from pichenettes/eurorack, at a true internal 48 kHz | 0.1.4: octave controls, Damping reversed as in Kekulé, snowflake Freeze, future-function navigation; high-note investigation, no DSP changes; reviewed by Claude, Codex's check of the Damping change open |
