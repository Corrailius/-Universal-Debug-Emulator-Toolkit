#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <variant>
#include <unordered_map>
#include <stdexcept>
#include "FormatReader.h"

/*
    MarshalReader
    Responsibility:
    - Interprets Ruby Marshal binary streams on top of BinaryReader
    - Decodes Marshal integers, strings, arrays, and metadata (IVAR)
    - Provides semantic reads (readInt, readString, etc.)
    Scope:
    - Engine-agnostic
    - Ruby Marshal format only
    - No knowledge of RPG Maker, Scripts.rxdata, or tooling
    Depends on:
    - BinaryReader (via FormatReader)
    Used by:
    - Engine-specific parsers (e.g. RxdataParser)
    Non-goals:
    - No zlib handling
    - No file patching
    - No Ruby execution semantics
*/

enum class MarshalTag : uint8_t {
    Nil        = 0x30, // '0'
    True       = 0x54, // 'T'
    False      = 0x46, // 'F'
    Integer    = 0x69, // 'i'
    String     = 0x22, // '"'
    Array      = 0x5B, // '['
    Hash       = 0x7B, // '{'
    HashDef    = 0x7D, // '}' — Hash with a default value
    Ivar       = 0x49, // 'I'
    Object     = 0x6F, // 'o'
    UserDef    = 0x75, // 'u' — UserDefined (e.g. Color, Tone in RPG Maker)
    Float      = 0x66, // 'f'
    Bignum     = 0x6C, // 'l'
    Symbol     = 0x3A, // ':'
    SymbolLink = 0x3B, // ';'
    ObjectRef  = 0x40, // '@'
};


struct MarshalNull {};  // explicit nil — avoids nullptr ambiguity

// Key type for Hash: Ruby hash keys are most commonly symbols (stored as
// std::string) or integers, but any Marshal value is a legal key.
// We use a flat vector of pairs rather than std::map/unordered_map because:
//   1. Keys are arbitrary MarshalValues — no cheap hash or comparator exists.
//   2. RPG Maker hashes are small; linear scan is fine.
//   3. Insertion order is preserved, which aids round-trip fidelity.
using MarshalPair = std::pair<MarshalValue, MarshalValue>;
using MarshalHash = std::vector<MarshalPair>;

struct MarshalValue {
        using Inner = std::variant<
        MarshalNull,
        bool,
        int32_t,
        std::string,
        std::vector<uint8_t>,
        std::vector<MarshalValue>,
        MarshalHash
    >;

    Inner data;

    // --- Constructors ---
    MarshalValue()                                     : data(MarshalNull{}) {}
    explicit MarshalValue(bool v)                      : data(v) {}
    explicit MarshalValue(int32_t v)                   : data(v) {}
    explicit MarshalValue(std::string v)               : data(std::move(v)) {}
    explicit MarshalValue(std::vector<uint8_t> v)      : data(std::move(v)) {}
    explicit MarshalValue(std::vector<MarshalValue> v) : data(std::move(v)) {}
    explicit MarshalValue(MarshalHash v)               : data(std::move(v)) {}

    // --- Type checks ---
    bool isNull()   const { return std::holds_alternative<MarshalNull>(data); }
    bool isBool()   const { return std::holds_alternative<bool>(data); }
    bool isInt()    const { return std::holds_alternative<int32_t>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isBytes()  const { return std::holds_alternative<std::vector<uint8_t>>(data); }
    bool isArray()  const { return std::holds_alternative<std::vector<MarshalValue>>(data); }
    bool isHash()   const { return std::holds_alternative<MarshalHash>(data); }

    // --- Accessors (throw std::bad_variant_access on wrong type) ---
    bool                             getBool()   const { return std::get<bool>(data); }
    int32_t                          getInt()    const { return std::get<int32_t>(data); }
    const std::string&               getString() const { return std::get<std::string>(data); }
    const std::vector<uint8_t>&      getBytes()  const { return std::get<std::vector<uint8_t>>(data); }
    const std::vector<MarshalValue>& getArray()  const { return std::get<std::vector<MarshalValue>>(data); }
    const MarshalHash&               getHash()   const { return std::get<MarshalHash>(data); }
};


class MarshalReader : public FormatReader {
public:
    explicit MarshalReader(BinaryReader& reader) : FormatReader(reader) {}

    // Validates the 0x04 0x08 version header — call once before any reads
    void readHeader() override;

    // Reads the next type-tag byte and dispatches to the correct reader
    MarshalValue readValue();

    // --- Direct reads (no tag byte consumed) ---
    // Public so engine-specific parsers can call mid-parse if needed
    int32_t                   readInt();
    std::vector<uint8_t>      readRawBytes();
    std::string               readRawString();
    std::vector<MarshalValue> readArray();
    MarshalHash               readHash();
    MarshalValue              readIvar();
    std::string               readSymbol();
    std::string               readSymbolLink();

private:
    std::vector<std::string>  m_symbols;  // symbol interning cache (per-stream)
    std::vector<MarshalValue> m_objectRefs; // object reference cache (per-stream)

    std::string resolveEncoding(
        const std::unordered_map<std::string, MarshalValue>& ivars) const;

    std::string decodeBytes(
        const std::vector<uint8_t>& bytes,
        const std::string& encoding) const;
};