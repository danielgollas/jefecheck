import Foundation

public let muesliFormatVersion = 1

public struct Notes: Equatable {
    public var frontMatter: FrontMatter
    public var body: String

    public init(frontMatter: FrontMatter, body: String) {
        self.frontMatter = frontMatter
        self.body = body
    }

    public func annotations() throws -> [Annotation] {
        try Annotations.extract(from: body)
    }
}

extension FrontMatter: Equatable {
    public static func == (lhs: FrontMatter, rhs: FrontMatter) -> Bool {
        lhs.keys == rhs.keys && lhs.values == rhs.values
    }
}

public struct RawNotes: Equatable {
    public var text: String

    public init(text: String) {
        self.text = text
    }

    public func timestamps() throws -> [Double] {
        try Annotations.extract(from: text).compactMap { try $0.timestamp() }
    }
}

public struct MeetingFolder {
    public var meta: [String: JSONValue]
    public var transcript: [Utterance]?
    public var notes: Notes?
    public var rawNotes: RawNotes?

    public init(
        meta: [String: JSONValue], transcript: [Utterance]? = nil,
        notes: Notes? = nil, rawNotes: RawNotes? = nil
    ) {
        self.meta = meta
        self.transcript = transcript
        self.notes = notes
        self.rawNotes = rawNotes
    }

    // MARK: - Reading

    public static func read(from dir: URL) throws -> MeetingFolder {
        let metaURL = dir.appendingPathComponent("meeting.json")
        let metaData = try Data(contentsOf: metaURL)
        guard let meta = try JSONDecoder().decode(JSONValue.self, from: metaData).objectValue else {
            throw FormatError.meta("top level must be an object")
        }
        var folder = MeetingFolder(meta: meta)

        let transcriptURL = dir.appendingPathComponent("transcript.jsonl")
        if let jsonl = try? String(contentsOf: transcriptURL, encoding: .utf8) {
            folder.transcript = try Transcript.parse(jsonl)
        }

        let notesURL = dir.appendingPathComponent("notes.md")
        if let text = try? String(contentsOf: notesURL, encoding: .utf8) {
            let (fm, body) = try FrontMatter.parse(text)
            folder.notes = Notes(frontMatter: fm, body: body)
        }

        let rawNotesURL = dir.appendingPathComponent("raw-notes.md")
        if let text = try? String(contentsOf: rawNotesURL, encoding: .utf8) {
            folder.rawNotes = RawNotes(text: text)
        }

        return folder
    }

    // MARK: - Writing

    public func write(to dir: URL) throws {
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        let metaText = String(data: try encoder.encode(JSONValue.object(meta)), encoding: .utf8)! + "\n"
        try atomicWrite(metaText, to: dir.appendingPathComponent("meeting.json"))

        if let transcript {
            try atomicWrite(
                try Transcript.serialize(transcript),
                to: dir.appendingPathComponent("transcript.jsonl"))
            let title = meta["title"]?.stringValue ?? "Meeting"
            var speakerNames: [String: String] = [:]
            if let speakers = meta["speakers"]?.objectValue {
                for (k, v) in speakers {
                    if let name = v.stringValue { speakerNames[k] = name }
                }
            }
            try atomicWrite(
                Transcript.renderMarkdown(title: title, utterances: transcript, speakerNames: speakerNames),
                to: dir.appendingPathComponent("transcript.md"))
        }

        if let notes {
            try atomicWrite(
                notes.frontMatter.serialize(body: notes.body),
                to: dir.appendingPathComponent("notes.md"))
        }

        if let rawNotes {
            try atomicWrite(rawNotes.text, to: dir.appendingPathComponent("raw-notes.md"))
        }
    }

    // Write-temp-then-rename, per storage-format.md sync rules.
    private func atomicWrite(_ content: String, to url: URL) throws {
        let tmp = url.appendingPathExtension("muesli-tmp")
        try content.write(to: tmp, atomically: true, encoding: .utf8)
        if FileManager.default.fileExists(atPath: url.path) {
            _ = try FileManager.default.replaceItemAt(url, withItemAt: tmp)
        } else {
            try FileManager.default.moveItem(at: tmp, to: url)
        }
    }

    // MARK: - Validation

    /// Validate a meeting folder; returns human-readable issues (empty = valid).
    public static func validate(dir: URL) -> [String] {
        let folder: MeetingFolder
        do {
            folder = try read(from: dir)
        } catch {
            return ["unreadable folder: \(error)"]
        }

        var issues: [String] = []

        if folder.meta["muesliVersion"]?.numberValue != Double(muesliFormatVersion) {
            issues.append("meeting.json: muesliVersion must be \(muesliFormatVersion)")
        }
        let metaID = folder.meta["id"]?.stringValue
        if metaID == nil || metaID == "" {
            issues.append("meeting.json: missing id")
        }
        if folder.meta["title"]?.stringValue == nil {
            issues.append("meeting.json: missing title")
        }

        if let notes = folder.notes {
            if notes.frontMatter["muesli"]?.numberValue != Double(muesliFormatVersion) {
                issues.append("notes.md: front matter 'muesli' must be \(muesliFormatVersion)")
            }
            if notes.frontMatter["id"]?.stringValue != metaID {
                issues.append("notes.md: front matter id does not match meeting.json id")
            }
            let count = folder.transcript?.count ?? 0
            do {
                for annotation in try notes.annotations() {
                    guard let ranges = try annotation.spanRanges() else { continue }
                    for range in ranges {
                        if range.start < 0 || range.end < range.start || range.end >= count {
                            issues.append(
                                "notes.md: span [\(range.start), \(range.end)] out of range (transcript has \(count) utterances)"
                            )
                        }
                    }
                }
            } catch {
                issues.append("notes.md: \(error)")
            }
        }

        if let rawNotes = folder.rawNotes {
            do {
                for t in try rawNotes.timestamps() where t < 0 {
                    issues.append("raw-notes.md: negative timestamp \(t)")
                }
            } catch {
                issues.append("raw-notes.md: \(error)")
            }
        }

        return issues
    }
}

// MARK: - Folder naming

/// Folder name: `YYYY-MM-DD HHMM <title>` in the meeting's local time.
/// Date parts are taken from the ISO string directly (which carries the meeting's
/// own UTC offset) so the name never depends on the machine's timezone.
public func meetingFolderName(startedAtISO: String, title: String) throws -> String {
    let pattern = "^(\\d{4})-(\\d{2})-(\\d{2})T(\\d{2}):(\\d{2})"
    let regex = try NSRegularExpression(pattern: pattern)
    let ns = startedAtISO as NSString
    guard let m = regex.firstMatch(in: startedAtISO, range: NSRange(location: 0, length: ns.length))
    else {
        throw FormatError.meta("unparseable startedAt: \(startedAtISO)")
    }
    let part = { (idx: Int) in ns.substring(with: m.range(at: idx)) }
    return "\(part(1))-\(part(2))-\(part(3)) \(part(4))\(part(5)) \(sanitizeMeetingTitle(title))"
}

public func sanitizeMeetingTitle(_ title: String) -> String {
    var forbidden = CharacterSet(charactersIn: "/\\:*?\"<>|")
    forbidden.formUnion(CharacterSet(charactersIn: UnicodeScalar(0)...UnicodeScalar(31)))
    let replaced = String(title.unicodeScalars.map { forbidden.contains($0) ? " " : Character($0) })
    let collapsed = replaced.split(separator: " ", omittingEmptySubsequences: true).joined(separator: " ")
    let trimmed = String(collapsed.prefix(80)).trimmingCharacters(in: .whitespaces)
    return trimmed.isEmpty ? "Meeting" : trimmed
}
