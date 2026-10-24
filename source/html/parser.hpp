#pragma once
//=============================================================================
// parser.hpp - Analyseur HTML tolerant (tokeniser + arbre)
//=============================================================================
#include "dom.hpp"
#include <string>

// Parse du HTML brut et construction du Document.
// Tolérant : ne se bloque jamais sur un HTML malformé.
bool htmlParse(const std::string& html, Document& doc);

// Decode les références numériques et les entités nommées HTML5 à partir de
// la position 'pos'. Retourne la nouvelle position et ajoute le texte décodé.
size_t htmlDecodeEntity(const std::string& s, size_t pos, std::string& out, bool inAttribute = false);

// Tests unitaires (compilables sur PC).
int htmlSelfTest();