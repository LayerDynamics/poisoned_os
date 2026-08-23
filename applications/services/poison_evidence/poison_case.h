#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_EVIDENCE_ID_MAX        (64u)
#define POISON_EVIDENCE_CASE_NAME_MAX (128u)

typedef enum {
    PoisonCaseOpen,
    PoisonCaseClosed
} PoisonCaseState;

typedef struct {
    bool active;
    char case_id[POISON_EVIDENCE_ID_MAX + 1u];
    char name[POISON_EVIDENCE_CASE_NAME_MAX + 1u];
    PoisonCaseState state;
} PoisonEvidenceCase;

bool poison_case_create(PoisonEvidenceCase* evidence_case, const char* case_id, const char* name);
bool poison_case_close(PoisonEvidenceCase* evidence_case);

#ifdef __cplusplus
}
#endif
