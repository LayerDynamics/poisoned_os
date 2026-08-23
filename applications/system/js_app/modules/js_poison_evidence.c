#include "js_poison_evidence.h"

#include <applications/services/poison_evidence/poison_evidence_i.h>

#define JS_POISON_EVIDENCE_MAX_BYTES (8u * 1024u * 1024u)

typedef struct {
    JsModules* modules;
} JsPoisonEvidence;

static bool js_poison_hex_decode(const char* input, uint8_t output[32u]) {
    if(!input || strlen(input) != 64u) return false;
    for(size_t i = 0; i < 32u; ++i) {
        uint8_t value = 0u;
        for(size_t nibble = 0; nibble < 2u; ++nibble) {
            const char c = input[i * 2u + nibble];
            if(c >= '0' && c <= '9')
                value = (uint8_t)((value << 4u) | (uint8_t)(c - '0'));
            else if(c >= 'a' && c <= 'f')
                value = (uint8_t)((value << 4u) | (uint8_t)(c - 'a' + 10));
            else
                return false;
        }
        output[i] = value;
    }
    return true;
}

static void js_poison_hex_encode(const uint8_t input[32u], char output[65u]) {
    static const char hex[] = "0123456789abcdef";
    for(size_t i = 0; i < 32u; ++i) {
        output[i * 2u] = hex[input[i] >> 4u];
        output[i * 2u + 1u] = hex[input[i] & 0x0fu];
    }
    output[64u] = '\0';
}

static void js_poison_evidence_capture_file(struct mjs* mjs) {
    static const JsValueDeclaration args_list[] = {
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_SIMPLE(JsValueTypeString),
    };
    static const JsValueArguments args = JS_VALUE_ARGS(args_list);
    const char *evidence_id, *case_id, *path, *sha256, *previous_audit_sha256;
    JS_VALUE_PARSE_ARGS_OR_RETURN(
        mjs, &args, &evidence_id, &case_id, &path, &sha256, &previous_audit_sha256);
    JsPoisonEvidence* evidence = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    uint8_t expected_sha256[32u];
    uint8_t previous_audit[32u];
    if(!evidence || !js_modules_resolve_project_path(evidence->modules, path, resolved) ||
       !poison_evidence_id_validate(evidence_id) || !poison_evidence_id_validate(case_id) ||
       !js_poison_hex_decode(sha256, expected_sha256) ||
       !js_poison_hex_decode(previous_audit_sha256, previous_audit)) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "invalid project evidence request");
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FileInfo info;
    const bool stat_ok = storage_common_stat(storage, resolved, &info) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(!stat_ok || file_info_is_dir(&info) || info.size == 0u ||
       info.size > JS_POISON_EVIDENCE_MAX_BYTES) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "invalid evidence source");
    }
    PoisonEvidenceRecord captured = {0};
    const bool ok = poison_evidence_capture_file_global(
        evidence_id, case_id, resolved, info.size, expected_sha256, true, previous_audit, &captured);
    memset(expected_sha256, 0, sizeof(expected_sha256));
    memset(previous_audit, 0, sizeof(previous_audit));
    if(!ok) JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "evidence capture failed");
    char audit_sha256[65u];
    js_poison_hex_encode(captured.audit_sha256, audit_sha256);
    mjs_val_t result = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, result) {
        JS_FIELD("evidenceId", mjs_mk_string(mjs, captured.evidence_id, ~0, true));
        JS_FIELD("caseId", mjs_mk_string(mjs, captured.case_id, ~0, true));
        JS_FIELD("size", mjs_mk_number(mjs, captured.content_length));
        JS_FIELD("auditSha256", mjs_mk_string(mjs, audit_sha256, ~0, true));
    }
    memset(&captured, 0, sizeof(captured));
    memset(audit_sha256, 0, sizeof(audit_sha256));
    mjs_return(mjs, result);
}

void* js_poison_evidence_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules) {
    if(!js_modules_is_managed(modules)) return NULL;
    JsPoisonEvidence* evidence = calloc(1u, sizeof(*evidence));
    if(!evidence) return NULL;
    evidence->modules = modules;
    *object = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, *object) {
        JS_FIELD(INST_PROP_NAME, mjs_mk_foreign(mjs, evidence));
        JS_FIELD("captureFile", MJS_MK_FN(js_poison_evidence_capture_file));
    }
    return evidence;
}

void js_poison_evidence_destroy(void* data) {
    JsPoisonEvidence* evidence = data;
    if(!evidence) return;
    memset(evidence, 0, sizeof(*evidence));
    free(evidence);
}
