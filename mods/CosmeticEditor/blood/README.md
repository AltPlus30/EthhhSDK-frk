# Monochrome blood bases

These five supplied TGAs are **edited monochrome replacements**, despite retaining
the source filenames containing `original`. They are not the extracted originals.
Source: `D:\AI\opencode\temp\ininja-blood-monochrome`.

All decode to grayscale RGB; decoded top-left alpha matches the independently
validated originals in `build/blood-textures` byte for byte (32-bit original BGRA
palettes). Runtime configuration checks dimensions, grayscale and the original
alpha SHA-256 before accepting a file. RGB is baked into private SDK-managed
textures, preserving each alpha byte.

Order: splatter 0, 1, 2, 3, then pool. See `provenance.json` and
`docs/cosmetic-blood-runtime.md`. No game archive or extracted original was edited.
