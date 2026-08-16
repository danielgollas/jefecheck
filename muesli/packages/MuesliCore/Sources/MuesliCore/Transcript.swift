import Foundation

public struct Utterance: Codable, Equatable {
    /// Stable index; notes.md spans reference these. Strictly increasing.
    public let i: Int
    public let t0: Double
    public let t1: Double
    public let speaker: String?
    public let text: String
    public let conf: Double?

    public init(i: Int, t0: Double, t1: Double, speaker: String? = nil, text: String, conf: Double? = nil) {
        self.i = i
        self.t0 = t0
        self.t1 = t1
        self.speaker = speaker
        self.text = text
        self.conf = conf
    }
}

public enum Transcript {
    public static func parse(_ jsonl: String) throws -> [Utterance] {
        let decoder = JSONDecoder()
        var utterances: [Utterance] = []
        let lines = jsonl.split(whereSeparator: \.isNewline).filter {
            !$0.trimmingCharacters(in: .whitespaces).isEmpty
        }
        for (index, line) in lines.enumerated() {
            guard let data = String(line).data(using: .utf8),
                let u = try? decoder.decode(Utterance.self, from: data)
            else {
                throw FormatError.transcript("line \(index + 1): invalid utterance")
            }
            utterances.append(u)
        }
        for n in 1..<max(utterances.count, 1) {
            if utterances[n].i <= utterances[n - 1].i {
                throw FormatError.transcript("utterance index not strictly increasing at line \(n + 1)")
            }
            if utterances[n].t0 < utterances[n - 1].t0 {
                throw FormatError.transcript("utterances not ordered by t0 at line \(n + 1)")
            }
        }
        return utterances
    }

    public static func serialize(_ utterances: [Utterance]) throws -> String {
        var lines: [String] = []
        for u in utterances {
            // Hand-assemble the object so key order is stable across platforms
            // (mirrors the TypeScript writer: i, t0, t1, speaker?, text, conf?).
            var fields: [String] = []
            fields.append("\"i\": \(u.i)")
            fields.append("\"t0\": \(formatNumber(u.t0))")
            fields.append("\"t1\": \(formatNumber(u.t1))")
            if let speaker = u.speaker {
                fields.append("\"speaker\": \(try encodeString(speaker))")
            }
            fields.append("\"text\": \(try encodeString(u.text))")
            if let conf = u.conf {
                fields.append("\"conf\": \(formatNumber(conf))")
            }
            lines.append("{" + fields.joined(separator: ", ") + "}")
        }
        return lines.joined(separator: "\n") + "\n"
    }

    private static func formatNumber(_ n: Double) -> String {
        if n == n.rounded(), n.magnitude < 1e15 { return String(Int64(n)) }
        return String(n)
    }

    private static func encodeString(_ s: String) throws -> String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.withoutEscapingSlashes]
        return String(data: try encoder.encode(s), encoding: .utf8)!
    }

    /// Render the human-readable transcript.md from utterances + speaker names.
    public static func renderMarkdown(
        title: String, utterances: [Utterance], speakerNames: [String: String] = [:]
    ) -> String {
        let lines = utterances.map { u -> String in
            let mm = Int(u.t0) / 60
            let ss = Int(u.t0) % 60
            let stamp = String(format: "%02d:%02d", mm, ss)
            var label = ""
            if let speaker = u.speaker {
                label = " \(speakerNames[speaker] ?? speaker):"
            }
            return "**[\(stamp)]\(label)** \(u.text)"
        }
        return "# Transcript — \(title)\n\n" + lines.joined(separator: "\n\n") + "\n"
    }
}
