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

// Read every persisted `Text/*` key from QSettings (falling back to the
// GfcTextRenderer constructor defaults) and push them into the textRenderer()
// singleton via its setters. Call once at startup (from loadPreferences())
// and again on Preferences "Cancel" to revert live text-page edits, since
// text prefs use deferred (Done-writes) persistence — see
// PreferencesWindow_Qt::writeTextPrefs().
void applyTextPrefs();

} }  // namespace jefe::qt
#endif
