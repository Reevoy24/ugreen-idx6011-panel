# Local patches for the vendored `lvgl` submodule

`lvgl` is pinned to an upstream commit, so these fixes live here as patch files
instead of being committed into the submodule (which would need a fork). Apply
them to the submodule before building:

```sh
cd lvgl && git apply ../patches/lvgl-drm-free-crtc.patch
```

The release build scripts apply them automatically (with `make clean` so the
LVGL objects are recompiled from the patched source).

## lvgl-drm-free-crtc.patch

`lv_linux_drm.c` `drm_find_connector`: when the target connector has no bound
CRTC at startup, pick a CRTC that is **not already driving another output**
instead of the first possible one.

Without this, if the front panel is flagged non-desktop (see
`tools/setup-nondesktop-console.sh`) **and** an external monitor holds the
panel's usual CRTC, ug-paneld hands LVGL a connector whose first candidate CRTC
is the busy one; the atomic commit then fails (`Invalid argument`) and the panel
stays black. Preferring a free CRTC fixes it. Harmless otherwise (a normally
bound panel never reaches this branch).
