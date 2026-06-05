// src/core/FormatReader.h
#pragma once
#include "BinaryReader.h"


class FormatReader {
public:
    explicit FormatReader(BinaryReader& reader) : m_reader(reader) {}
    virtual ~FormatReader() = default;

    virtual void readHeader() = 0;  // every format has a header/magic

protected:
    BinaryReader& m_reader;
};

/*
    FormatReader
    Responsibility:
    - Common base for all binary format readers
    - Enforces the BinaryReader dependency contract
    Non-goals:
    - No parsing logic
    - No format knowledge
*/