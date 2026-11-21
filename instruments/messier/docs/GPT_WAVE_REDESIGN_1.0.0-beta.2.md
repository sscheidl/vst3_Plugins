# WAVE view redesign: GPT's feasibility brief (pasted by the user, 2026-11-21)

Reference images: `gpt-orbital-scope-entwurf.webp` (GPT's concept: "Orbital Scope"),
`wave-1317-ausschnitt.png` (current WAV view, build 13:17).

Verbatim as the user pasted it:

MESSIER – WAVE View Visual Redesign Feasibility Study
Please evaluate a visual redesign of the existing circular WAVE view.
Our preferred direction is Orbital Scope (concept 3), enhanced with selected elements of Spectral Cloud (concept 1).
The goal is to make the waveform feel more atmospheric, spatial and organically integrated into the glass dome, without sacrificing precision.
Requirements:

* Preserve all existing waveform data, time mapping, stereo information, loop selection, playhead behavior and interaction logic.
* Replace the visually homogeneous, opaque waveform fill with a more nuanced layered representation.
* Keep a precise waveform layer with clearly readable sample peaks and transients.
* Investigate a secondary, softly blurred, low-opacity waveform layer to create depth.
* Optionally introduce subtle fine-grained internal texture derived from the waveform amplitude.
* Preserve the spherical coordinate system and its existing functional markers.
* Use restrained colour accents consistent with the selected nebula and HUE.
* Keep the existing dome and metal ring assets.
* Avoid unnecessary GPU dependencies, expensive per-frame blur operations, or DSP changes.

Please investigate:

1. Which renderer currently draws the waveform and how it handles caching.
2. Whether the existing waveform geometry can be reused for multiple visual passes.
3. Whether the diffuse layer can be cached independently of the playhead.
4. What modifications would be required for the Spectral Cloud texture.
5. The expected impact on frame rate and GUI scaling.
6. Which design effects can be implemented without significant architectural changes.

Please recommend an incremental implementation strategy.
Do not modify the code yet. First provide a feasibility assessment, risks and a proposed rendering architecture.
