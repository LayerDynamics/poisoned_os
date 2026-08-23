#include "js_developer_policy.h"
#include <string.h>
static bool copy_text(char* destination, size_t capacity, const char* source) {
    if(!source || source[0] == '\0') return false;
    size_t length = strnlen(source, capacity);
    if(length == 0u || length >= capacity) return false;
    memcpy(destination, source, length + 1u);
    return true;
}
void js_developer_policy_init(JsDeveloperPolicy* policy) {
    if(policy) memset(policy, 0, sizeof(*policy));
}
bool js_developer_policy_activate(
    JsDeveloperPolicy* policy,
    const char* signer,
    const char* project_digest,
    const char* symbol_scope,
    uint32_t now,
    uint32_t duration_ms,
    bool owner_confirmed) {
    if(!policy || policy->active || policy->revoked || !owner_confirmed || duration_ms == 0u ||
       UINT32_MAX - now < duration_ms ||
       !copy_text(policy->signer, sizeof(policy->signer), signer) ||
       !copy_text(policy->project_digest, sizeof(policy->project_digest), project_digest) ||
       !copy_text(policy->symbol_scope, sizeof(policy->symbol_scope), symbol_scope))
        return false;
    policy->expires_at = now + duration_ms;
    policy->active = true;
    return true;
}
bool js_developer_policy_expire(JsDeveloperPolicy* policy, uint32_t now) {
    if(!policy || !policy->active) return false;
    if((int32_t)(now - policy->expires_at) >= 0) {
        policy->active = false;
        return true;
    }
    return false;
}
bool js_developer_policy_allows(
    const JsDeveloperPolicy* policy,
    const char* signer,
    const char* project_digest,
    const char* symbol,
    uint32_t now) {
    if(!policy || !policy->active || policy->revoked || !signer || !project_digest || !symbol ||
       (int32_t)(now - policy->expires_at) >= 0 || strcmp(policy->signer, signer) != 0 ||
       strcmp(policy->project_digest, project_digest) != 0)
        return false;
    const size_t symbol_length = strlen(symbol);
    const char* cursor = policy->symbol_scope;
    while(*cursor != '\0') {
        const char* end = strchr(cursor, ',');
        const size_t entry_length = end ? (size_t)(end - cursor) : strlen(cursor);
        if(entry_length == symbol_length && memcmp(cursor, symbol, symbol_length) == 0)
            return true;
        if(!end) break;
        cursor = end + 1u;
    }
    return false;
}
bool js_developer_policy_revoke(JsDeveloperPolicy* policy) {
    if(!policy || !policy->active) return false;
    policy->active = false;
    policy->revoked = true;
    return true;
}
