# Explicit plane readback (opt-in device diagnostic)

`drm_readback.cpp` reads an already active framebuffer from one explicitly supplied
DRM card and observed plane ID. It does not identify ownership: the caller must
first establish that the plane belongs to the foreground PenChat SDK surface.
Do not use it to capture another application's content.

Build with the existing `scripts/native-target.sh rk3562` compiler, C++14, and
standard libdrm UAPI headers (`drm.h`, `drm_mode.h`, `drm_fourcc.h`). No libdrm
link library is required. Example invocation on an authorized target:

```
drm-readback /dev/dri/card0 OBSERVED_PLANE_ID /explicit/private/path/frame.raw
```

The output is the complete physical framebuffer including row padding; the
adjacent `.raw.json` supplies physical width, height, pitch and verified fourcc.
Only linear, single-plane XRGB8888 and ARGB8888 are accepted. Little-endian byte
order is B,G,R,X or B,G,R,A. A host converter must use `pitch` when stepping rows,
and may ignore X/A for an opaque RGB preview. Preserve the physical raw original.
Any logical preview rotation must come from the actual Falcon screen configuration,
never dimensions or a presumed board mounting.

The card opens read-only, and mmap uses PROT_READ. Standard GETPLANE, GETFB2 and
MAP_DUMB queries are the only DRM operations apart from closing the GEM handles
returned to this process by GETFB2. No master, modeset, atomic, plane mutation,
buffer creation/destruction, camera, input or application database access occurs.
Every returned unique GEM handle and successful mapping is released on all exits.
If the selected FB/CRTC changes while copying, the helper retries (three total
attempts), and writes only a stable-identity attempt. It fails closed when GETFB2
or the supported format cannot be verified.

A stable framebuffer identity is not a guarantee of an atomic pixel snapshot:
the producer may modify the same framebuffer during memcpy. The image shows only
this plane's storage, before CRTC crop/scale/rotation, alpha composition or other
planes. A primary-plane capture can distinguish SDK-surface pixels missing from
`miniapp_cli` capture, but cannot prove the final LCD image, native overlay content,
or absence of physical display defects. This helper neither freezes nor modifies
the producer.

Host verification for this change: strict GCC `-Wall -Wextra -Werror` C++14 builds
for host and AArch64, invalid-ID argument rejection before opening a DRM device,
and source inspection of the complete ioctl/mmap call set. No device capture or
production payload build is part of this helper-only validation.
