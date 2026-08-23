#pragma once
#include <stdbool.h>
#include <stdint.h>
#define JS_DEVELOPER_POLICY_TEXT_MAX    (128u)
#define JS_DEVELOPER_POLICY_SYMBOLS_MAX (512u)
typedef struct {
    bool active;
    bool revoked;
    char signer[JS_DEVELOPER_POLICY_TEXT_MAX];
    char project_digest[JS_DEVELOPER_POLICY_TEXT_MAX];
    char symbol_scope[JS_DEVELOPER_POLICY_SYMBOLS_MAX];
    uint32_t expires_at;
} JsDeveloperPolicy;
void js_developer_policy_init(JsDeveloperPolicy* policy);
bool js_developer_policy_activate(
    JsDeveloperPolicy* policy,
    const char* signer,
    const char* project_digest,
    const char* symbol_scope,
    uint32_t now,
    uint32_t duration_ms,
    bool owner_confirmed);
bool js_developer_policy_allows(
    const JsDeveloperPolicy* policy,
    const char* signer,
    const char* project_digest,
    const char* symbol,
    uint32_t now);
bool js_developer_policy_revoke(JsDeveloperPolicy* policy);
bool js_developer_policy_expire(JsDeveloperPolicy* policy, uint32_t now);
