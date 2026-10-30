/* Lightloader 2.0 embedded mod metadata contract.
 *
 * Every Lightloader 2.0 compliant mod embeds ONE resource:
 *   type RT_RCDATA (10), name "F1C_MOD_META", language neutral,
 *   content UTF-8 JSON (no BOM), max 16 KiB.
 *
 * The manager reads it with LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE |
 * LOAD_LIBRARY_AS_IMAGE_RESOURCE): the DLL is mapped as data only, DllMain
 * never runs, DRM/protection code never runs. Because it is inside the
 * signed/hashed file, it cannot drift from the binary like a sidecar can.
 *
 * Required keys:
 *   "schemaVersion": 1
 *   "id":        catalog ID, [a-z0-9_]{2,60}  (e.g. "esc_pause_menu")
 *   "name":      display name
 *   "version":   semantic version, e.g. "1.1.0" or "1.1.0-rc.1"
 * Optional keys:
 *   "author", "description" (<= 2000 chars), "homepage" (https://mods.palloset.com/...)
 *   "requires":  ["shared_broker>=10"]   (IDs with optional >=version)
 *   "paid":      Lightloader catalog flag; false for every public mod
 *   "settings":  "esc_pause_menu.ini"   (INI in plugins\lightloader the mod reads)
 *   "lightloader": ">=2.0.0"            (minimum host)
 *
 * Usage in a mod: add to a .rc file compiled with windres / rc.exe:
 *
 *   #include "f1c_mod_meta.h"
 *   F1C_MOD_META_RESOURCE "esc_pause_menu.meta.json"
 *
 * and link the resulting object. See PUBLICATION_POLICY.md.
 */
#ifndef F1C_MOD_META_H
#define F1C_MOD_META_H

#define F1C_MOD_META_NAME "F1C_MOD_META"
#define F1C_MOD_META_SCHEMA 1
#define F1C_MOD_META_MAX_BYTES 16384

#ifdef RC_INVOKED
#define F1C_MOD_META_RESOURCE F1C_MOD_META RCDATA
#endif

#endif
