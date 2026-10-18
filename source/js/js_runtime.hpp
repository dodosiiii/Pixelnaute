#pragma once

struct JSRuntime;
struct JSContext;

// Cree un runtime/contexte isole pour une page, avec un tas borne et une
// pile JavaScript adaptes a la New 3DS.
bool jsPageContextCreate(JSRuntime*& runtime, JSContext*& context);
void jsPageContextDestroy(JSRuntime*& runtime, JSContext*& context);
