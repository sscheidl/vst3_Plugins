# Instruments

Plug-ins that turn MIDI into sound. They register in the VST3 category
`Instrument`; none reads audio so far (Messier has a sidechain input for its next
version).

| Project | Origin | Status |
| --- | --- | --- |
| [toy-piano-classic](toy-piano-classic/) | Port of `instruments/toy-piano` from Daisy-Seed | 0.3.2: builds, validated, tested and code-reviewed; 0.1.0 loads in Studio One, not yet listened to |
| [torus-classic](torus-classic/) | Kekulé: port of `instruments/physical-modeling` (Rings/Torus) from Daisy-Seed, without its reverb | 0.3.1: builds, validated and tested automatically; 4, 8 or 16 voices, octave keys, no Plug-in Nap; released by Codex, not tried in a DAW yet; 0.1.0 loads and plays in Studio One |
| [elements](elements/) | Mendelejew: port of the Mutable Instruments Elements DSP from pichenettes/eurorack, at a true internal 48 kHz | 0.1.4: octave controls, Damping reversed as in Kekulé, snowflake Freeze, future-function navigation; high-note investigation, no DSP changes; reviewed by Claude, Codex's check of the Damping change open |
| [messier](messier/) | Messier: port of the granular looper `instruments/granular` (Nebulae on Daisy 0.2.2) from Daisy-Seed, file source only | 0.1.10: builds, validated, tested automatically, bit-exact against the firmware core; grain level 0.90 as the original; the findings of nine Codex reviews fixed, drag and drop of WAV files, Stop and Bypass, the next check open; data path of the window with a provisional map; tried in Studio One by the user (0.1.7, 0.1.8) |
