#include "RxdataParser.h"
#include <stdexcept>

namespace engines::rpgmaker {

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------

RxdataParser::RxdataParser(MarshalReader& reader)
    : m_reader(reader)
{}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

const std::vector<RxScript>& RxdataParser::scripts() const {
    return m_scripts;
}

// -----------------------------------------------------------------------
// parse()
// -----------------------------------------------------------------------

void RxdataParser::parse() {
    m_scripts.clear();

    m_reader.readHeader();
    MarshalValue root = m_reader.readValue();

    if (!root.isArray())
        throw std::runtime_error("RxdataParser: root value is not an array");

    for (const MarshalValue& entry : root.getArray())
        m_scripts.push_back(parseScriptEntry(entry));
}

// -----------------------------------------------------------------------
// parseScriptEntry()
// -----------------------------------------------------------------------

RxScript RxdataParser::parseScriptEntry(const MarshalValue& entry) {
    if (!entry.isArray())
        throw std::runtime_error("RxdataParser: script entry is not an array");

    const auto& fields = entry.getArray();

    if (fields.size() != 3)
        throw std::runtime_error("RxdataParser: script entry must have exactly 3 fields");

    const MarshalValue& scriptId     = fields[0];
    const MarshalValue& scriptName   = fields[1];
    const MarshalValue& scriptSource = fields[2];

    RxScript script;
    script.id   = scriptId.getInt();
    script.name = scriptName.getString();
    script.source = scriptSource.isString()
        ? scriptSource.getString()
        : std::string(
            reinterpret_cast<const char*>(scriptSource.getBytes().data()),
            scriptSource.getBytes().size()
          );

    return script;
}

} // namespace engines::rpgmaker