#pragma once
// ---------------------------------------------------------------------------
// Session groups (JEF-31/37).
//
// A GROUP is the named parent a session is created under -- "Client review",
// "Internal dailies" -- and it owns every HOST-side setting: whether joiners
// must knock, an optional password, the idle cutoff that stops billing, and
// the participant cap. Selecting the parent on the Cloud tab loads its
// settings; hosting applies them.
//
// Why per-group rather than global: these settings are properties of *how a
// kind of session is run*, not of the machine. A client review wants knocking
// and a tight cap; internal dailies want neither. One global set forces you to
// re-toggle them every time, which in practice means they end up wrong.
//
// Joiners never see any of this. Everything here is host-side by construction:
// the Cloud tab is the hosting tab.
//
// Persistence is QSettings under Remote/groups/<name>/*, matching the rest of
// the Qt preference model (see developer_notes §27). Group names are used as
// settings subkeys, so they are sanitized on write.
// ---------------------------------------------------------------------------

#include <QString>
#include <QStringList>

namespace jefe::qt {

struct SessionGroup {
    QString name;

    // --- admission -------------------------------------------------------
    /** Each joiner waits for the host's Admit/Deny (JEF-37). */
    bool requireKnock = true;
    /**
     * Optional shared password, checked in addition to the join code.
     * Empty = none. Knocking and a password are independent: knocking puts a
     * human in the loop, a password gates before anyone reaches the host.
     */
    QString password;

    // --- cost / lifetime --------------------------------------------------
    /**
     * Minutes of inactivity after which the session closes and BILLING STOPS.
     * 0 = no timeout. This exists because host-departure is not the only way a
     * session ends in practice: a sleeping laptop can hold the coordinator
     * socket open, and without a cutoff the meter keeps running against
     * wall-clock time nobody was using.
     */
    int idleTimeoutMinutes = 30;
    /** 0 = unlimited. Counts admitted participants, not pending knocks. */
    int maxParticipants = 8;

    // --- defaults ---------------------------------------------------------
    /** Pre-fills the session name field when this group is selected. */
    QString defaultSessionName;
    /** Per-group coordinator override; empty = the global Preferences value. */
    QString coordinatorUrl;
};

/** All group names, in creation order. Never empty: seeds a default. */
QStringList sessionGroupNames();

/** Load one group by name; returns defaults if it does not exist yet. */
SessionGroup loadSessionGroup(const QString& name);

/** Create or overwrite a group. */
void saveSessionGroup(const SessionGroup& group);

/** Remove a group and its settings. Refuses to remove the last one. */
bool removeSessionGroup(const QString& name);

/** The group last hosted under; falls back to the first available. */
QString activeSessionGroup();
void setActiveSessionGroup(const QString& name);

}  // namespace jefe::qt
