Issue #900: manual visual evidence

Feature: 2e337ac41947ae49276af616b1084a60cf74937b
Base feature commit: d6c634dc74aa28c85f1847a583499d770f521ad6
Environment: NixOS VM, native DRM session with VirtIO/VirGL.
Screenshots supplied by the author on 2026-11-26, copied unchanged.

windows-disabled.png / windows-enabled.png:
Four translucent Foot windows and their server-side titlebars.

popup-disabled.png / popup-enabled.png:
A translucent XDG popup samples a checkerboard drawn by its opaque parent.
Only the popup and translucent titlebar show the custom distortion and tint;
the parent's checkerboard remains sharp.

The demonstration uses a custom animated blue/violet shader. Native window
and popup blur are disabled; effects.in_capture is enabled. The shader and
visual demo are not bundled presets or additions to the feature PR.
These images show appearance, not performance measurements.

Local verification: 166 Meson tests; 67 targeted compositor checks;
11 relevant AddressSanitizer checks; final expanded backdrop/shutdown checks
in debug and ASan; focused clang-tidy; repository formatting and diff check.
Release package and full NixOS VM built successfully. The author manually
tested windows, SSD titlebars, popup inheritance, and the global off/on switch.

Known boundaries: layer-shell popups do not inherit window effects; opaque
client pixels cover the shader; custom backdrops are omitted from closing
snapshots; coordinates are local to each surface. HDR and physical multi-output
manual testing were not performed.
