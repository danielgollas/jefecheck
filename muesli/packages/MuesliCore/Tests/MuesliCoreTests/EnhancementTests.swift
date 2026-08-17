import Foundation
import XCTest

@testable import MuesliCore

private struct MockModel: NotesModel {
    let body: String
    func complete(_ request: EnhancementRequest) async throws -> EnhancementResult {
        XCTAssertTrue(request.system.contains("Ground every statement"))
        XCTAssertTrue(request.user.contains("# Transcript"))
        return EnhancementResult(body: body, model: "mock-model", inputTokens: 1000, outputTokens: 100)
    }
}

final class EnhancementTests: XCTestCase {
    static let muesliDir = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static let standup = muesliDir
        .appendingPathComponent("fixtures")
        .appendingPathComponent("2026-08-12 1000 Weekly Standup")

    func promptFile() throws -> String {
        try String(
            contentsOf: Self.muesliDir.appendingPathComponent("prompts/enhance-v1.md"),
            encoding: .utf8)
    }

    func testPromptExtraction() throws {
        let prompt = try promptFile()
        let system = try Enhancement.extractSystemPrompt(fromPromptFile: prompt)
        XCTAssertTrue(system.contains("Ground every statement"))
        XCTAssertFalse(system.contains("## Escalation heuristic"))
        XCTAssertEqual(try Enhancement.extractPromptVersion(fromPromptFile: prompt), "2026-08-16")
    }

    func testTranscriptRendering() throws {
        let utterances = [
            Utterance(i: 0, t0: 0, t1: 1, speaker: "S1", text: "Hello"),
            Utterance(i: 1, t0: 1, t1: 2, text: "No speaker"),
        ]
        let rendered = Enhancement.renderTranscriptForPrompt(
            utterances, speakerNames: ["S1": "Dana"])
        XCTAssertEqual(rendered, "[0] Dana: Hello\n[1] ?: No speaker")
    }

    func testModelChoice() {
        let short = [Utterance(i: 0, t0: 0, t1: 1, speaker: "S1", text: "hi")]
        XCTAssertEqual(Enhancement.chooseModel(transcript: short), Enhancement.defaultModel)
        XCTAssertEqual(
            Enhancement.chooseModel(transcript: short, forceEscalation: true),
            Enhancement.escalationModel)
        let manySpeakers = (0..<6).map {
            Utterance(i: $0, t0: Double($0), t1: Double($0) + 1, speaker: "S\($0)", text: "x")
        }
        XCTAssertEqual(
            Enhancement.chooseModel(transcript: manySpeakers), Enhancement.escalationModel)
    }

    func testEnhanceFixtureWithMockModel() async throws {
        var folder = try MeetingFolder.read(from: Self.standup)
        folder.meta.removeValue(forKey: "enhancement")
        folder.notes = nil

        let mockBody =
            "## Summary\n\n"
            + "Release slipped to Friday over the FreeType regression. <!--m:{\"spans\":[[1,6]]}-->\n\n"
            + "## Action items\n\n"
            + "- [ ] Dana: fix the atlas invalidation bug. <!--m:{\"spans\":[[7,7]]}-->"

        let enhanced = try await Enhancement.enhance(
            folder: folder,
            promptFile: try promptFile(),
            template: "## Summary\n\n## Action items\n",
            templateId: "default",
            model: MockModel(body: mockBody))

        let notes = try XCTUnwrap(enhanced.notes)
        XCTAssertEqual(notes.frontMatter["id"]?.stringValue, "mtg_01J8Z0K3D9")
        XCTAssertEqual(notes.frontMatter["date"]?.stringValue, "2026-08-12")
        XCTAssertEqual(
            notes.frontMatter["attendees"]?.listValue?.compactMap(\.stringValue),
            ["Dana Kim", "Luis Ortega"])
        XCTAssertEqual(try notes.annotations().count, 2)

        let record = try XCTUnwrap(enhanced.meta["enhancement"]?.objectValue)
        XCTAssertEqual(record["model"]?.stringValue, "mock-model")
        XCTAssertEqual(record["promptVersion"]?.stringValue, "2026-08-16")
        XCTAssertEqual(record["inputTokens"]?.numberValue, 1000)

        // Persist and confirm the folder validates end to end.
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent("muesli-enh-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: tmp) }
        try enhanced.write(to: tmp)
        XCTAssertEqual(MeetingFolder.validate(dir: tmp), [])
    }
}
