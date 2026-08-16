import Foundation
import XCTest

@testable import MuesliCore

final class FormatTests: XCTestCase {
    // Tests/MuesliCoreTests/FormatTests.swift → up 5 components → muesli/
    static let fixturesDir = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()  // FormatTests.swift
        .deletingLastPathComponent()  // MuesliCoreTests
        .deletingLastPathComponent()  // Tests
        .deletingLastPathComponent()  // MuesliCore
        .deletingLastPathComponent()  // packages
        .appendingPathComponent("fixtures")

    static let standup = fixturesDir.appendingPathComponent("2026-08-12 1000 Weekly Standup")
    static let minimal = fixturesDir.appendingPathComponent("2026-01-05 0930 Quick Sync")

    func testStandupFixtureParses() throws {
        let folder = try MeetingFolder.read(from: Self.standup)

        XCTAssertEqual(folder.meta["id"]?.stringValue, "mtg_01J8Z0K3D9")
        XCTAssertEqual(folder.meta["title"]?.stringValue, "Weekly Standup")
        XCTAssertEqual(folder.meta["muesliVersion"]?.numberValue, 1)

        let transcript = try XCTUnwrap(folder.transcript)
        XCTAssertEqual(transcript.count, 10)
        XCTAssertEqual(
            transcript.first?.text,
            "Okay, let's get started. Quick round of updates and then the release.")
        XCTAssertEqual(transcript.last?.text, "Alright, that's everything. Thanks all.")

        let notes = try XCTUnwrap(folder.notes)
        XCTAssertEqual(notes.frontMatter["title"]?.stringValue, "Weekly Standup")
        XCTAssertEqual(
            notes.frontMatter["attendees"]?.listValue?.compactMap(\.stringValue),
            ["Dana Kim", "Luis Ortega"])
        XCTAssertEqual(
            notes.frontMatter["tags"]?.listValue?.compactMap(\.stringValue), ["standup"])

        let spanSets = try notes.annotations().compactMap { try $0.spanRanges() }
        let asPairs = spanSets.map { set in set.map { [$0.start, $0.end] } }
        XCTAssertEqual(asPairs, [[[1, 5]], [[6, 6]], [[3, 4], [7, 7]], [[8, 8]]])

        let rawNotes = try XCTUnwrap(folder.rawNotes)
        XCTAssertEqual(try rawNotes.timestamps(), [2.1, 41.7, 93.0, 115.2])
    }

    func testMinimalFixtureParses() throws {
        let folder = try MeetingFolder.read(from: Self.minimal)
        XCTAssertEqual(folder.meta["id"]?.stringValue, "mtg_01HXQ9M2P0")
        XCTAssertNil(folder.notes)
        XCTAssertEqual(folder.transcript?.count, 2)
        XCTAssertEqual(try folder.rawNotes?.timestamps(), [10.0])
    }

    func testFixturesValidateCleanly() {
        XCTAssertEqual(MeetingFolder.validate(dir: Self.standup), [])
        XCTAssertEqual(MeetingFolder.validate(dir: Self.minimal), [])
    }

    func testRoundtripIsLossless() throws {
        let original = try MeetingFolder.read(from: Self.standup)
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("muesli-rt-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: tmp) }

        try original.write(to: tmp)
        let reread = try MeetingFolder.read(from: tmp)

        XCTAssertEqual(reread.meta, original.meta)
        XCTAssertEqual(reread.transcript, original.transcript)
        XCTAssertEqual(reread.notes, original.notes)
        XCTAssertEqual(reread.rawNotes, original.rawNotes)
        XCTAssertEqual(MeetingFolder.validate(dir: tmp), [])
    }

    func testUnknownMetaKeysSurviveRoundtrip() throws {
        let original = try MeetingFolder.read(from: Self.minimal)
        let custom = try XCTUnwrap(original.meta["x-custom"]?.objectValue)
        XCTAssertEqual(custom["importedFrom"]?.stringValue, "voice-memo")
        XCTAssertEqual(custom["revision"]?.numberValue, 3)

        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("muesli-uk-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: tmp) }

        try original.write(to: tmp)
        let reread = try MeetingFolder.read(from: tmp)
        XCTAssertEqual(reread.meta["x-custom"], original.meta["x-custom"])
    }

    func testFolderName() throws {
        XCTAssertEqual(
            try meetingFolderName(startedAtISO: "2026-08-12T10:00:03-07:00", title: "Weekly Standup"),
            "2026-08-12 1000 Weekly Standup")
        XCTAssertEqual(
            try meetingFolderName(
                startedAtISO: "2026-01-05T09:30:00+01:00", title: "Q3: Plan/Review \"Final\"?"),
            "2026-01-05 0930 Q3 Plan Review Final")
    }

    func testSanitizeTitleEdgeCases() {
        XCTAssertEqual(sanitizeMeetingTitle("///"), "Meeting")
        XCTAssertEqual(sanitizeMeetingTitle("  spaced   out  "), "spaced out")
        XCTAssertEqual(sanitizeMeetingTitle(String(repeating: "x", count: 200)).count, 80)
    }

    func testValidationCatchesOutOfRangeSpans() throws {
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("muesli-bad-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: tmp) }
        try FileManager.default.copyItem(at: Self.standup, to: tmp)

        let notesURL = tmp.appendingPathComponent("notes.md")
        let corrupted = try String(contentsOf: notesURL, encoding: .utf8)
            .replacingOccurrences(
                of: "<!--m:{\"spans\":[[6,6]]}-->", with: "<!--m:{\"spans\":[[98,99]]}-->")
        try corrupted.write(to: notesURL, atomically: true, encoding: .utf8)

        let issues = MeetingFolder.validate(dir: tmp)
        XCTAssertEqual(issues.count, 1)
        XCTAssertTrue(issues[0].contains("span [98, 99] out of range"))
    }
}
