#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <ibutton_key.h>
#include <ibutton_protocols.h>

typedef struct {
    iButtonProtocols* protocols;
    iButtonKey* key;
    bool emulating;
} PoisonIbuttonHandle;

typedef struct {
    iButtonProtocolId protocol;
    size_t rendered_size;
    char rendered[192];
} PoisonIbuttonReadResult;

#ifdef __cplusplus
extern "C" {
#endif

PoisonIbuttonHandle* poison_ibutton_open(void);
void poison_ibutton_close(PoisonIbuttonHandle* handle);
bool poison_ibutton_read(PoisonIbuttonHandle* handle, PoisonIbuttonReadResult* result);
bool poison_ibutton_write_id(PoisonIbuttonHandle* handle);
bool poison_ibutton_emulate_start(PoisonIbuttonHandle* handle);
void poison_ibutton_emulate_stop(PoisonIbuttonHandle* handle);

#ifdef __cplusplus
}
#endif
