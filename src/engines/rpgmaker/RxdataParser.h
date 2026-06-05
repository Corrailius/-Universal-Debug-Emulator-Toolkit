#pragma once
#include <vector>
#include <string>
#include <cstdint>

#include "core/MarshalReader.h"

namespace engines::rpgmaker {

struct RxScript {
    int32_t     id;
    std::string name;
    std::string source;
};

class RxdataParser {
public:
    explicit RxdataParser(MarshalReader& reader);

    void parse();
    const std::vector<RxScript>& scripts() const;

private:
    MarshalReader&         m_reader;
    std::vector<RxScript> m_scripts;

    RxScript parseScriptEntry(const MarshalValue& entry);
};

} // namespace engines::rpgmaker


/*
    RxdataParser
    Responsibility:
    - Parses RPG Maker XP Scripts.rxdata into structured script entries
    - Interprets MarshalReader output into engine-meaningful data
    Scope:
    - RPG Maker XP specific
    - Scripts.rxdata format only
    Depends on:
    - MarshalReader
    Used by:
    - Engine adapter / tooling layer
    Non-goals:
    - No binary reading (BinaryReader is MarshalReader's concern)
    - No zlib decompression
    - No file writing or patching
*/