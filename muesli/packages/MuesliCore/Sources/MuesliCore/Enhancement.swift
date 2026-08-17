import Foundation

/// Enhancement pipeline assembly — the one AI call per meeting. Mirrors
/// muesli-format/src/enhance.ts; the model itself is injected via `NotesModel`
/// so apps can supply the cloud API, an on-device model, or a test mock.
public struct EnhancementRequest: Equatable {
    public let system: String
    public let user: String
}

public struct EnhancementResult {
    public let body: String
    public let model: String
    public let inputTokens: Int?
    public let outputTokens: Int?

    public init(body: String, model: String, inputTokens: Int? = nil, outputTokens: Int? = nil) {
        self.body = body
        self.model = model
        self.inputTokens = inputTokens
        self.outputTokens = outputTokens
    }
}

public protocol NotesModel {
    func complete(_ request: EnhancementRequest) async throws -> EnhancementResult
}

public enum Enhancement {
    public static let defaultModel = "claude-haiku-4-5"
    public static let escalationModel = "claude-sonnet-5"

    /// The system prompt is the content between "## System prompt" and the next "## ".
    public static func extractSystemPrompt(fromPromptFile promptFile: String) throws -> String {
        guard let start = promptFile.range(of: "## System prompt") else {
            throw FormatError.meta("enhancement prompt file: '## System prompt' section not found")
        }
        guard let lineEnd = promptFile.range(of: "\n", range: start.upperBound..<promptFile.endIndex)
        else {
            throw FormatError.meta("enhancement prompt file: malformed system prompt section")
        }
        let rest = lineEnd.upperBound..<promptFile.endIndex
        let end = promptFile.range(of: "\n## ", range: rest)?.lowerBound ?? promptFile.endIndex
        return String(promptFile[lineEnd.upperBound..<end])
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    public static func extractPromptVersion(fromPromptFile promptFile: String) throws -> String {
        for line in promptFile.split(separator: "\n") {
            if line.hasPrefix("promptVersion:") {
                return line.dropFirst("promptVersion:".count)
                    .trimmingCharacters(in: .whitespaces)
            }
        }
        throw FormatError.meta("enhancement prompt file: promptVersion not found in front matter")
    }

    /// Render the transcript as numbered utterances the way the prompt expects.
    public static func renderTranscriptForPrompt(
        _ utterances: [Utterance], speakerNames: [String: String] = [:]
    ) -> String {
        utterances.map { u in
            let who = u.speaker.map { speakerNames[$0] ?? $0 } ?? "?"
            return "[\(u.i)] \(who): \(u.text)"
        }.joined(separator: "\n")
    }

    public static func buildRequest(
        promptFile: String,
        template: String,
        glossary: String? = nil,
        transcript: [Utterance],
        speakerNames: [String: String] = [:],
        rawNotesText: String
    ) throws -> EnhancementRequest {
        let glossaryText = glossary?.trimmingCharacters(in: .whitespacesAndNewlines)
        return EnhancementRequest(
            system: try extractSystemPrompt(fromPromptFile: promptFile),
            user: """
                # Template
                \(template)

                # Glossary
                \(glossaryText?.isEmpty == false ? glossaryText! : "(empty)")

                # Transcript
                \(renderTranscriptForPrompt(transcript, speakerNames: speakerNames))

                # Raw notes
                \(rawNotesText)
                """
        )
    }

    /// Escalation heuristic from prompts/enhance-v1.md.
    public static func chooseModel(
        transcript: [Utterance], forceEscalation: Bool = false
    ) -> String {
        if forceEscalation { return escalationModel }
        let approxTokens = transcript.reduce(0) { $0 + $1.text.count } / 4
        if approxTokens > 40_000 { return escalationModel }
        let speakers = Set(transcript.compactMap(\.speaker))
        if speakers.count > 4 { return escalationModel }
        return defaultModel
    }

    /// Build notes.md front matter from meeting metadata (storage-format.md).
    public static func notesFrontMatter(fromMeta meta: [String: JSONValue]) -> FrontMatter {
        var fm = FrontMatter()
        func set(_ key: String, _ value: FrontMatterValue) {
            fm.keys.append(key)
            fm.values[key] = value
        }
        set("muesli", .number(1))
        set("id", .string(meta["id"]?.stringValue ?? ""))
        set("title", .string(meta["title"]?.stringValue ?? "Meeting"))
        let startedAt = meta["startedAt"]?.stringValue ?? ""
        set("date", .string(String(startedAt.prefix(10))))
        var attendees: [FrontMatterValue] = []
        if case .array(let list)? = meta["attendees"] {
            for entry in list {
                if let name = entry.objectValue?["name"]?.stringValue {
                    attendees.append(.string(name))
                }
            }
        }
        set("attendees", .list(attendees))
        var tags: [FrontMatterValue] = []
        if case .array(let list)? = meta["tags"] {
            tags = list.compactMap { $0.stringValue.map(FrontMatterValue.string) }
        }
        set("tags", .list(tags))
        return fm
    }

    /// Run the enhancement for a captured folder and return it with notes
    /// attached and the `enhancement` record set. Pure with respect to disk.
    public static func enhance(
        folder: MeetingFolder,
        promptFile: String,
        template: String,
        templateId: String,
        glossary: String? = nil,
        model: NotesModel
    ) async throws -> MeetingFolder {
        guard let transcript = folder.transcript, !transcript.isEmpty else {
            throw FormatError.meta("cannot enhance: folder has no transcript")
        }
        var speakerNames: [String: String] = [:]
        if let speakers = folder.meta["speakers"]?.objectValue {
            for (key, value) in speakers {
                if let name = value.stringValue { speakerNames[key] = name }
            }
        }
        let request = try buildRequest(
            promptFile: promptFile,
            template: template,
            glossary: glossary,
            transcript: transcript,
            speakerNames: speakerNames,
            rawNotesText: folder.rawNotes?.text ?? "(none)")
        let result = try await model.complete(request)

        var enhanced = folder
        let body = result.body.hasSuffix("\n") ? result.body : result.body + "\n"
        enhanced.notes = Notes(frontMatter: notesFrontMatter(fromMeta: folder.meta), body: body)
        var record: [String: JSONValue] = [
            "model": .string(result.model),
            "template": .string(templateId),
            "promptVersion": .string(try extractPromptVersion(fromPromptFile: promptFile)),
        ]
        if let inputTokens = result.inputTokens {
            record["inputTokens"] = .number(Double(inputTokens))
        }
        if let outputTokens = result.outputTokens {
            record["outputTokens"] = .number(Double(outputTokens))
        }
        enhanced.meta["enhancement"] = .object(record)
        return enhanced
    }
}
