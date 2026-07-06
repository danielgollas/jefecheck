// Single source of truth for mapping the global gfcSettings `sett` to/from
// Qt QSettings. The legacy XML saveSettings()/readSettings() are dead stubs;
// this is the only real preferences persistence.
#ifndef JEFECHECK_QT_PREFS_PERSIST_H
#define JEFECHECK_QT_PREFS_PERSIST_H

namespace jefe { namespace qt {

// Read every persisted preference key from QSettings into the global `sett`.
// Call once at startup, after `sett` is default-constructed. Missing keys keep
// the constructor default.
void loadPreferences();

// Persist every preference from the global `sett` to QSettings. Call on
// Preferences "Done".
void writePreferences();

} }  // namespace jefe::qt
#endif
