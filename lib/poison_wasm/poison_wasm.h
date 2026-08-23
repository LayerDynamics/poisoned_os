#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_WASM_MAX_MODULE_BYTES (512u * 1024u)
#define POISON_WASM_MAX_IMPORTS 32u
#define POISON_WASM_MAX_MEMORY_PAGES 64u

typedef enum {
    PoisonWasmOk,
    PoisonWasmInvalidModule,
    PoisonWasmUnsupportedFeature,
    PoisonWasmForgedImport,
    PoisonWasmLimit,
    PoisonWasmRuntimeUnavailable,
} PoisonWasmResult;

typedef struct {
    const uint8_t* bytes;
    size_t size;
    uint16_t import_count;
    uint32_t memory_pages;
    bool has_table;
} PoisonWasmModule;

PoisonWasmResult poison_wasm_admit(const uint8_t* bytes, size_t size, PoisonWasmModule* module);
bool poison_wasm_runtime_available(void);

#ifdef __cplusplus
}
#endif
