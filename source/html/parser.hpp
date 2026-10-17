#pragma once
//=============================================================================
// parser.hpp - Analyseur HTML tolerant (tokeniser + arbre)
//=============================================================================
#include "dom.hpp"
#include <string>

// Parse du HTML brut et construction du Document.
// Tolérant : ne se bloque jamais sur un HTML malformé.
bool htmlParse(const std::string& html, Document& doc);

// Decode les entites HTML (&amp; &#38; ...) a partir de la position 'pos'.
// Retourne la nouvelle position. Le caractere decode est ajoute a 'out'.
size_t htmlDecodeEntity(const std::string& s, size_t pos, std::string& out, bool requireSemicolon = false);

// Tests unitaires (compilables sur PC).
int htmlSelfTest();