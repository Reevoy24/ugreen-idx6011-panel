#ifndef VERSION_H
#define VERSION_H

/* Single source of truth for the human-readable version, shown in the web UI
 * (and exposed via /api/stats). Bump this on every release alongside the README
 * badge. The .deb/tarball build scripts derive their package version from it
 * (turning a "-betaN" suffix into "~betaN" for dpkg ordering). */
#define UG_VERSION "1.7.2-beta1"

/* Standalone fan daemon (ug-fand) version. Released and versioned independently
 * of the panel (it ships on the non-Pro iDX6011, which has no display). Shown in
 * the ug-fand web dashboard footer and exposed via its /api/stats. The
 * build-fand.sh script derives the package version from this. */
#define UG_FAND_VERSION "1.1.0"

#endif
