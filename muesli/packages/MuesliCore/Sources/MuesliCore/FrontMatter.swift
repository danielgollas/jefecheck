import Foundation

/// Muesli front matter is a deliberate YAML *subset* so every platform can
/// parse it without a YAML dependency: flat `key: value` pairs where value is a
/// scalar (string or number) or an inline list `[a, b, c]` of scalars.
public enum FrontMatterValue: Equatable {
    case string(String)
    case number(Double)
    case list([FrontMatterValue])

    public var stringValue: String? {
        if case .string(let s) = self { return s }
        return nil
    }

    public var numberValue: Double? {
        if case .number(let n) = self { return n }
        return nil
    }

    public var listValue: [FrontMatterValue]? {
        if case .list(let l) = self { return l }
        return nil
    }
}

public enum FormatError: Error, CustomStringConvertible {
    case frontMatter(String)
    case transcript(String)
    case annotation(String)
    case meta(String)

    public var description: String {
        switch self {
        case .frontMatter(let m): return "front matter: \(m)"
        case .transcript(let m): return "transcript: \(m)"
        case .annotation(let m): return "annotation: \(m)"
        case .meta(let m): return "meeting.json: \(m)"
        }
    }
}

public struct FrontMatter {
    /// Parsed key/value pairs, preserving document order.
    public var keys: [String]
    public var values: [String: FrontMatterValue]

    public subscript(key: String) -> FrontMatterValue? { values[key] }

    public init(keys: [String] = [], values: [String: FrontMatterValue] = [:]) {
        self.keys = keys
        self.values = values
    }

    public static func parse(_ text: String) throws -> (frontMatter: FrontMatter, body: String) {
        guard text.hasPrefix("---\n") else {
            throw FormatError.frontMatter("missing open marker")
        }
        guard let closeRange = text.range(of: "\n---\n", range: text.index(text.startIndex, offsetBy: 4)..<text.endIndex) else {
            throw FormatError.frontMatter("missing close marker")
        }
        let raw = String(text[text.index(text.startIndex, offsetBy: 4)..<closeRange.lowerBound])
        let body = String(text[closeRange.upperBound...])

        var fm = FrontMatter()
        for line in raw.split(separator: "\n", omittingEmptySubsequences: false) {
            let trimmed = line.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty { continue }
            guard let colon = line.firstIndex(of: ":") else {
                throw FormatError.frontMatter("unparseable line: \(line)")
            }
            let key = String(line[..<colon]).trimmingCharacters(in: .whitespaces)
            let rawValue = String(line[line.index(after: colon)...]).trimmingCharacters(in: .whitespaces)
            fm.keys.append(key)
            fm.values[key] = parseValue(rawValue)
        }
        return (fm, body)
    }

    private static func parseValue(_ s: String) -> FrontMatterValue {
        if s.hasPrefix("["), s.hasSuffix("]") {
            let inner = String(s.dropFirst().dropLast()).trimmingCharacters(in: .whitespaces)
            if inner.isEmpty { return .list([]) }
            return .list(splitTopLevel(inner).map {
                parseScalar($0.trimmingCharacters(in: .whitespaces))
            })
        }
        return parseScalar(s)
    }

    /// Split an inline list on commas, respecting quoted items.
    private static func splitTopLevel(_ s: String) -> [String] {
        var parts: [String] = []
        var current = ""
        var quote: Character? = nil
        for ch in s {
            if let q = quote {
                current.append(ch)
                if ch == q { quote = nil }
            } else if ch == "\"" || ch == "'" {
                current.append(ch)
                quote = ch
            } else if ch == "," {
                parts.append(current)
                current = ""
            } else {
                current.append(ch)
            }
        }
        parts.append(current)
        return parts
    }

    private static func parseScalar(_ s: String) -> FrontMatterValue {
        if s.count >= 2,
            (s.hasPrefix("\"") && s.hasSuffix("\"")) || (s.hasPrefix("'") && s.hasSuffix("'"))
        {
            return .string(String(s.dropFirst().dropLast()))
        }
        if s.range(of: "^-?\\d+(\\.\\d+)?$", options: .regularExpression) != nil,
            let n = Double(s)
        {
            return .number(n)
        }
        return .string(s)
    }

    public func serialize(body: String) -> String {
        let lines = keys.compactMap { key -> String? in
            guard let v = values[key] else { return nil }
            return "\(key): \(Self.serializeValue(v))"
        }
        return "---\n" + lines.joined(separator: "\n") + "\n---\n" + body
    }

    private static func serializeValue(_ v: FrontMatterValue) -> String {
        switch v {
        case .list(let items):
            return "[" + items.map(serializeScalar).joined(separator: ", ") + "]"
        default:
            return serializeScalar(v)
        }
    }

    private static func serializeScalar(_ v: FrontMatterValue) -> String {
        switch v {
        case .number(let n):
            if n == n.rounded(), n.magnitude < 1e15 { return String(Int64(n)) }
            return String(n)
        case .string(let s):
            let needsQuoting =
                s.isEmpty
                || s.rangeOfCharacter(from: CharacterSet(charactersIn: ":#,[]\"'")) != nil
                || s != s.trimmingCharacters(in: .whitespaces)
                || s.range(of: "^-?\\d+(\\.\\d+)?$", options: .regularExpression) != nil
            if needsQuoting {
                let escaped = s.replacingOccurrences(of: "\\", with: "\\\\")
                    .replacingOccurrences(of: "\"", with: "\\\"")
                return "\"\(escaped)\""
            }
            return s
        case .list:
            return serializeValue(v)
        }
    }
}
