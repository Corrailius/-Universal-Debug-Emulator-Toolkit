#include "MarshalReader.h"
#include <stdexcept>
#include <unordered_map>

// Ruby Marshal streams always open with two version bytes: 0x04 0x08
// (major=4, minor=8).  This has been stable since Ruby 1.8 and is the
// only version you will encounter in the wild for RPG Maker XP/VX/VXAce.
// Anything else is either a corrupt file or a non-Marshal stream.
void MarshalReader::readHeader() {
    const uint8_t major = m_reader.readU8();
    const uint8_t minor = m_reader.readU8();

    if (major != 0x04 || minor != 0x08) {
        throw std::runtime_error(
            "Invalid Marshal header: expected 0x04 0x08, got "
            + std::to_string(major) + " " + std::to_string(minor));
    }
}

// Consumes one type-tag byte and dispatches to the matching sub-reader.
// The tag is NOT forwarded — each sub-reader picks up immediately after it.
//
// Primitive values (Nil/True/False) are constructed inline; everything
// structural delegates to a dedicated method so this stays a flat switch.
MarshalValue MarshalReader::readValue() {
    const auto tag = static_cast<MarshalTag>(m_reader.readU8());

    switch (tag) {

        case MarshalTag::Nil:
            return MarshalValue{};              // default-constructs MarshalNull

        case MarshalTag::True:
            return MarshalValue{ true };

        case MarshalTag::False:
            return MarshalValue{ false };

        case MarshalTag::Integer:
            return MarshalValue{ readInt() };

        case MarshalTag::String: {
            // A bare '"' string carries no encoding annotation — store as raw
            // bytes so callers are never handed silently mis-decoded text.
            // Encoding-aware strings arrive as Ivar(String, ...) instead.
            auto value = MarshalValue{ readRawBytes() };
            m_objectRefs.push_back(value);
            return value;
        }

        case MarshalTag::Array: {
            // Reserve a slot BEFORE reading children so that any ObjectRef
            // inside the array (including self-references) resolves correctly.
            // We fill the slot in-place rather than building a temporary and
            // patching, avoiding any question of copy/move validity.
            const size_t refIdx = m_objectRefs.size();
            m_objectRefs.emplace_back(std::vector<MarshalValue>{}); // typed placeholder

            const int32_t count = readInt();
            if (count < 0) {
                throw std::runtime_error(
                    "Marshal array: negative count " + std::to_string(count));
            }

            auto& arr = std::get<std::vector<MarshalValue>>(m_objectRefs[refIdx].data);
            arr.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; i++) {
                arr.push_back(readValue());
            }

            return m_objectRefs[refIdx];
        }

        case MarshalTag::Hash: {
            // Standard hash — no default value.
            // Same reference-safety pattern as Array: reserve the slot first
            // so any value inside the hash can ObjectRef back to this hash.
            const size_t refIdx = m_objectRefs.size();
            m_objectRefs.emplace_back(MarshalHash{});

            const int32_t count = readInt();
            if (count < 0) {
                throw std::runtime_error(
                    "Marshal hash: negative count " + std::to_string(count));
            }

            auto& hash = std::get<MarshalHash>(m_objectRefs[refIdx].data);
            hash.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; i++) {
                MarshalValue k = readValue();
                MarshalValue v = readValue();
                hash.emplace_back(std::move(k), std::move(v));
            }

            return m_objectRefs[refIdx];
        }

        case MarshalTag::HashDef: {
            // Hash with a default value (Hash.new(default)).
            // Wire format: same as Hash, followed by one extra value for the
            // default.  We read and discard the default — callers that need
            // it can extend MarshalHash to carry an optional default field.
            const size_t refIdx = m_objectRefs.size();
            m_objectRefs.emplace_back(MarshalHash{});

            const int32_t count = readInt();
            if (count < 0) {
                throw std::runtime_error(
                    "Marshal hash (with default): negative count " + std::to_string(count));
            }

            auto& hash = std::get<MarshalHash>(m_objectRefs[refIdx].data);
            hash.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; i++) {
                MarshalValue k = readValue();
                MarshalValue v = readValue();
                hash.emplace_back(std::move(k), std::move(v));
            }

            readValue(); // consume default value — not represented in MarshalHash yet

            return m_objectRefs[refIdx];
        }

        case MarshalTag::Ivar:
            // Ivar is a modifier, not a new object identity.  It wraps an
            // inner value (typically String) with instance-variable metadata
            // such as encoding.  The inner value registers itself in
            // m_objectRefs when readValue() processes it — adding a second
            // slot here would corrupt all subsequent ObjectRef indices.
            return readIvar();

        case MarshalTag::Symbol:
            // Symbols are interned: readSymbol pushes into m_symbols so that
            // subsequent SymbolLink tags can resolve by index.
            // Symbols do NOT go into objectRefs.
            return MarshalValue{ readSymbol() };

        case MarshalTag::SymbolLink:
            return MarshalValue{ readSymbolLink() };

        case MarshalTag::ObjectRef: {
            const int32_t idx = readInt();
            if (idx < 1 || static_cast<size_t>(idx) > m_objectRefs.size()) {
                throw std::runtime_error(
                "Marshal object reference out of range: " + std::to_string(idx));
            }
            return m_objectRefs[static_cast<size_t>(idx) - 1];
        }

        // ---------------------------------------------------------------
        // Tags present in real RPG Maker data but not yet fully decoded.
        // Each stub consumes enough bytes to stay stream-synchronised and
        // throws a descriptive error so callers know exactly what hit them.
        // Replace the throw with real logic when you need the type.
        // ---------------------------------------------------------------

        case MarshalTag::Float: {
            // Wire: length-prefixed ASCII decimal string, e.g. "3.14".
            // Registered in objectRefs like any heap object.
            const auto bytes = readRawBytes();
            const std::string text(bytes.begin(), bytes.end());
            // Register a placeholder so objectRefs indices stay correct.
            m_objectRefs.emplace_back(MarshalNull{});
            throw std::runtime_error(
                "Marshal Float not yet supported: \"" + text + "\"");
        }

        case MarshalTag::Bignum: {
            // Wire: sign byte ('+'|'-'), then word-count, then LE 16-bit words.
            // Uncommon in RPG Maker data; skip gracefully.
            const uint8_t sign   = m_reader.readU8();
            const int32_t words  = readInt();
            if (words < 0) throw std::runtime_error("Marshal Bignum: negative word count");
            m_reader.skip(static_cast<size_t>(words) * 2);
            m_objectRefs.emplace_back(MarshalNull{});
            throw std::runtime_error(
                std::string("Marshal Bignum not yet supported (sign=")
                + static_cast<char>(sign) + ")");
        }

        case MarshalTag::Object: {
            // Wire: class name (Symbol/SymbolLink), then ivar count + pairs.
            // RPG Maker uses this for RPG::Map, RPG::Event, etc.
            // Registering in objectRefs is required before reading ivars.
            const size_t refIdx = m_objectRefs.size();
            m_objectRefs.emplace_back(MarshalNull{});

            const std::string className = std::get<std::string>(readValue().data);
            const int32_t count = readInt();
            if (count < 0) throw std::runtime_error("Marshal Object: negative ivar count");
            for (int32_t i = 0; i < count; i++) {
                readValue(); // key
                readValue(); // value
            }
            (void)refIdx;
            throw std::runtime_error(
                "Marshal Object not yet supported: " + className);
        }

        case MarshalTag::UserDef: {
            // Wire: class name (Symbol/SymbolLink), then raw byte payload.
            // RPG Maker uses this for Color (rgba floats) and Tone (rgbg floats).
            // Registering in objectRefs is required.
            const size_t refIdx = m_objectRefs.size();
            m_objectRefs.emplace_back(MarshalNull{});

            const std::string className = std::get<std::string>(readValue().data);
            const auto payload = readRawBytes();
            (void)refIdx;
            (void)payload;
            throw std::runtime_error(
                "Marshal UserDefined not yet supported: " + className);
        }

        default:
            throw std::runtime_error(
                "Unknown Marshal tag: 0x" +
                std::to_string(static_cast<unsigned>(static_cast<uint8_t>(tag)))
                );
    }
}

// Ruby Marshal integer encoding — variable-length signed.
//
// Leading byte 'c' encodes both the value and how many follow-on bytes exist:
//   c == 0          → value is 0 (no further bytes)
//   1 <= c <= 4     → read c little-endian bytes, unsigned result
//   c >= 5          → small positive: value is (c - 5)
//   -4 <= c <= -1   → read -c little-endian bytes, result is sign-extended
//   c <= -5         → small negative: value is (c + 5)
int32_t MarshalReader::readInt() {
    const int8_t c = static_cast<int8_t>(m_reader.readU8());

    if (c == 0) return 0;

    if (c > 0) {
        if (c < 5) {
            // c follow-on bytes, little-endian, zero-extended
            int32_t result = 0;
            for (int i = 0; i < c; i++) {
                result |= static_cast<int32_t>(m_reader.readU8()) << (8 * i);
            }
            return result;
        }
        // Small positive packed directly in the leading byte
        return static_cast<int32_t>(c) - 5;
    }

    // c < 0
    if (c > -5) {
        // -c follow-on bytes, little-endian, sign-extended from -1
        int32_t result = -1;
        for (int i = 0; i < -c; i++) {
            result &= ~(0xFF << (8 * i));
            result |=  static_cast<int32_t>(m_reader.readU8()) << (8 * i);
        }
        return result;
    }

    // Small negative packed directly in the leading byte
    return static_cast<int32_t>(c) + 5;
}

// Reads a length-prefixed byte sequence (no encoding assumed).
// Used for bare String tags and as the inner payload of Ivar strings.
std::vector<uint8_t> MarshalReader::readRawBytes() {
    const int32_t length = readInt();
    if (length < 0) {
        throw std::runtime_error(
            "Marshal string/bytes: negative length " + std::to_string(length));
    }
    return m_reader.readBytes(static_cast<size_t>(length));
}

// Reads a length-prefixed byte sequence and reinterprets it as a std::string.
// Callers are responsible for ensuring the bytes are valid for their encoding.
std::string MarshalReader::readRawString() {
    const int32_t length = readInt();
    if (length < 0) {
        throw std::runtime_error(
            "Marshal raw string: negative length " + std::to_string(length));
    }
    const auto bytes = m_reader.readBytes(static_cast<size_t>(length));
    return std::string(bytes.begin(), bytes.end());
}

// readArray() — public API for callers outside readValue.
// NOTE: readValue() no longer calls this; it fills the objectRefs slot
// in-place to guarantee reference-safety.  This helper remains available
// for engine-specific parsers that need a standalone array read where no
// self-reference is possible (e.g. fixed-layout tuples).
std::vector<MarshalValue> MarshalReader::readArray() {
    const int32_t count = readInt();
    if (count < 0) {
        throw std::runtime_error(
            "Marshal array: negative count " + std::to_string(count));
    }

    std::vector<MarshalValue> arr;
    arr.reserve(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; i++) {
        arr.push_back(readValue());
    }

    return arr;
}

// readHash() — public API for callers outside readValue.
// Same caveat as readArray(): readValue() fills the objectRefs slot in-place,
// so this helper is for contexts where self-reference is not a concern.
MarshalHash MarshalReader::readHash() {
    const int32_t count = readInt();
    if (count < 0) {
        throw std::runtime_error(
            "Marshal hash: negative count " + std::to_string(count));
    }

    MarshalHash hash;
    hash.reserve(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; i++) {
        MarshalValue k = readValue();
        MarshalValue v = readValue();
        hash.emplace_back(std::move(k), std::move(v));
    }

    return hash;
}

// Reads an Ivar (instance-variable) wrapper.
//
// Wire layout:
//   Ivar tag  (already consumed by readValue)
//   inner value
//   attribute count  (Marshal integer)
//   for each attribute:
//     key   (Symbol or SymbolLink — read as MarshalValue for transparent link resolution)
//     value (any Marshal value)
//
// Ivar is a *modifier*, not a new object.  Ruby gives the inner value the
// object identity; Ivar just annotates it with metadata.  Consequently this
// method must NOT register anything in m_objectRefs — the inner value's own
// readValue() dispatch already did so (for String, Array, etc.).
//
// The dominant use-case for RPG Maker data is a String wrapped with
// "E" => true, signalling UTF-8 encoding.  This method decodes that
// and returns a MarshalValue{ std::string } when it can; otherwise it
// returns the inner value unchanged (raw bytes remain as bytes).
MarshalValue MarshalReader::readIvar() {
    // Read the inner object first (typically a String / raw bytes)
    MarshalValue inner = readValue();

    const int32_t count = readInt();
    if (count < 0) {
        throw std::runtime_error(
            "Marshal ivar: negative attribute count " + std::to_string(count));
    }

    std::unordered_map<std::string, MarshalValue> ivars;
    ivars.reserve(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; i++) {
        // Keys are always symbols (tag ':' or ';'), not generic values.
        // We read them as generic values so SymbolLink resolution works
        // transparently; then extract the string name.
        MarshalValue keyVal = readValue();
        MarshalValue val    = readValue();

        if (keyVal.isString()) {
            ivars.emplace(keyVal.getString(), std::move(val));
        }
        // Non-string keys are uncommon in RPG Maker data; we skip them
        // rather than crashing on an unexpected symbol representation.
    }

    // If the inner value is raw bytes and we have encoding metadata,
    // promote to std::string using the declared encoding.
    if (inner.isBytes()) {
        const std::string encoding = resolveEncoding(ivars);
        return MarshalValue{ decodeBytes(inner.getBytes(), encoding) };
    }

    return inner;
}

// Reads a new symbol from the stream, interns it, and returns its name.
// The symbol is appended to m_symbols so SymbolLink can resolve it later.
std::string MarshalReader::readSymbol() {
    const int32_t length = readInt();
    if (length < 0) {
        throw std::runtime_error(
            "Marshal symbol: negative length " + std::to_string(length));
    }
    const auto bytes = m_reader.readBytes(static_cast<size_t>(length));
    std::string sym(bytes.begin(), bytes.end());
    m_symbols.push_back(sym);
    return sym;
}

// Resolves a SymbolLink index to a previously interned symbol name.
// Indices are 0-based into m_symbols.
std::string MarshalReader::readSymbolLink() {
    const int32_t idx = readInt();
    if (idx < 0 || static_cast<size_t>(idx) >= m_symbols.size()) {
        throw std::runtime_error(
            "Marshal symbol link out of range: " + std::to_string(idx));
    }
    return m_symbols[static_cast<size_t>(idx)];
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Determines the encoding string from a parsed ivar attribute map.
//
// Ruby's shorthand:
//   "E" => true   → UTF-8
//   "E" => false  → US-ASCII (binary-safe subset)
//   "encoding" => "<name>"  → explicit IANA name
//
// Returns "UTF-8" as the default — correct for virtually all RPG Maker data.
std::string MarshalReader::resolveEncoding(
    const std::unordered_map<std::string, MarshalValue>& ivars) const
{
    // Check shorthand "E" key first
    auto eIt = ivars.find("E");
    if (eIt != ivars.end()) {
        if (eIt->second.isBool()) {
            return eIt->second.getBool() ? "UTF-8" : "US-ASCII";
        }
    }

    // Check explicit "encoding" key
    auto encIt = ivars.find("encoding");
    if (encIt != ivars.end() && encIt->second.isString()) {
        return encIt->second.getString();
    }

    // Default: treat as UTF-8
    return "UTF-8";
}

// Converts raw bytes to a std::string.
//
// For UTF-8 and ASCII-compatible encodings the bytes are valid as-is —
// reinterpret directly.  For other encodings (Shift_JIS, EUC-JP, etc.)
// a full transcoding library (iconv/ICU) would be needed; we preserve
// the bytes verbatim and let the caller deal with it, which is the
// safest no-dependency option.
std::string MarshalReader::decodeBytes(
    const std::vector<uint8_t>& bytes,
    const std::string& encoding) const
{
    // UTF-8 and ASCII are byte-compatible — no transformation needed.
    if (encoding == "UTF-8"   ||
        encoding == "US-ASCII" ||
        encoding == "ASCII")
    {
        return std::string(bytes.begin(), bytes.end());
    }

    // For any other encoding, preserve bytes as-is.
    // Callers that need proper transcoding should replace this branch
    // with an iconv or ICU call.
    return std::string(bytes.begin(), bytes.end());
}