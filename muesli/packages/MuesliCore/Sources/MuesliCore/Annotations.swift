import Foundation

/// Muesli annotations are HTML comments of the form `<!--m:{...json...}-->`
/// embedded in Markdown. In notes.md the payload is `{"spans": [[start, end], ...]}`
/// (inclusive utterance-index ranges into transcript.jsonl). In raw-notes.md the
/// payload is `{"t": seconds}` — the capture-relative time the paragraph was typed.
public struct Annotation: Equatable {
    /// UTF-16 offset of the comment within the searched text.
    public let offset: Int
    public let data: [String: JSONValue]

    public init(offset: Int, data: [String: JSONValue]) {
        self.offset = offset
        self.data = data
    }

    /// Inclusive utterance-index ranges, or nil if this annotation carries none.
    public func spanRanges() throws -> [(start: Int, end: Int)]? {
        guard let spans = data["spans"] else { return nil }
        guard case .array(let ranges) = spans else {
            throw FormatError.annotation("malformed spans payload")
        }
        return try ranges.map { range in
            guard case .array(let pair) = range, pair.count == 2,
                let s = pair[0].numberValue, let e = pair[1].numberValue,
                s == s.rounded(), e == e.rounded()
            else {
                throw FormatError.annotation("malformed spans payload")
            }
            return (start: Int(s), end: Int(e))
        }
    }

    /// The typing timestamp, or nil if this annotation carries none.
    public func timestamp() throws -> Double? {
        guard let t = data["t"] else { return nil }
        guard let n = t.numberValue else {
            throw FormatError.annotation("malformed timestamp payload")
        }
        return n
    }
}

public enum Annotations {
    static let pattern = "<!--m:(\\{.*?\\})-->"

    public static func extract(from text: String) throws -> [Annotation] {
        let regex = try NSRegularExpression(
            pattern: pattern, options: [.dotMatchesLineSeparators])
        let ns = text as NSString
        let matches = regex.matches(in: text, range: NSRange(location: 0, length: ns.length))
        return try matches.map { match in
            let json = ns.substring(with: match.range(at: 1))
            guard let jsonData = json.data(using: .utf8),
                let value = try? JSONDecoder().decode(JSONValue.self, from: jsonData),
                let object = value.objectValue
            else {
                throw FormatError.annotation("invalid annotation JSON at offset \(match.range.location)")
            }
            return Annotation(offset: match.range.location, data: object)
        }
    }

    public static func format(_ data: [String: JSONValue]) throws -> String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys, .withoutEscapingSlashes]
        let json = String(data: try encoder.encode(JSONValue.object(data)), encoding: .utf8)!
        return "<!--m:\(json)-->"
    }
}
