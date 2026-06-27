#ifndef VERSION_H
#define VERSION_H

/* Single source of truth for the human-readable version, shown in the web UI
 * (and exposed via /api/stats). Bump this on every release alongside the README
 * badge. The .deb/tarball build scripts derive their package version from it
 * (turning a "-betaN" suffix into "~betaN" for dpkg ordering). */
#define UG_VERSION "1.7.0"

#endif
