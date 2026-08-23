#include "rpc_poison_package_catalog.h"
#include "rpc_i.h"

#include "../poison_packages/poison_package_catalog_internal.h"
#include "../poison_packages/poison_package_manager.h"

#include <string.h>

typedef struct {
    RpcSession* session;
} RpcPoisonPackageCatalog;

static void rpc_poison_package_catalog_record_encode(
    PB_Poison_PackageCatalogRecord* output,
    const PoisonPackageCatalogRecord* record,
    uint32_t ordinal,
    uint32_t generation) {
    strcpy(output->id, record->id);
    strcpy(output->version, record->version);
    strcpy(output->signer, record->signer);
    strcpy(output->digest, record->digest);
    output->source = (PB_Poison_PackageCatalogSource)record->source;
    strcpy(output->source_path, record->source_path);
    output->freshness = (PB_Poison_PackageCatalogFreshness)record->freshness;
    output->state = (PB_Poison_PackageCatalogState)record->state;
    output->verified = record->verified;
    output->signer_revoked = record->signer_revoked;
    output->conflicted = record->conflicted;
    output->ordinal = ordinal;
    output->generation = generation;
    output->compatible = record->compatible;
    output->capability_mask = record->capability_mask;
}

static void rpc_poison_package_catalog_process(const PB_Main* request, void* context) {
    RpcPoisonPackageCatalog* rpc_catalog = context;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    const PB_Poison_PackageCatalogRequest* input =
        &request->content.poison_package_catalog_request;
    if(request->which_content != PB_Main_poison_package_catalog_request_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(rpc_catalog->session) ||
       !rpc_session_get_secure_identity(rpc_catalog->session, &session_id, &role) ||
       session_id == 0u || role > PoisonRoleObserver || input->max_records == 0u ||
       input->max_records > POISON_PACKAGE_CATALOG_MAX_RECORDS) {
        rpc_send_and_release_empty(
            rpc_catalog->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PoisonPackageCatalog* catalog = malloc(sizeof(*catalog));
    if(!catalog) {
        rpc_send_and_release_empty(
            rpc_catalog->session, request->command_id, PB_CommandStatus_ERROR);
        return;
    }
    if(!poison_package_catalog_from_manager(
           catalog, poison_packages_manager(), poison_packages_authorities()) ||
       input->offset > catalog->count) {
        memset(catalog, 0, sizeof(*catalog));
        free(catalog);
        rpc_send_and_release_empty(
            rpc_catalog->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    const size_t remaining = catalog->count - input->offset;
    const size_t returned = remaining < input->max_records ? remaining : input->max_records;
    PB_Main* response = rpc_message_alloc();
    for(size_t index = 0u; index < returned; ++index) {
        const size_t catalog_index = input->offset + index;
        *response = (PB_Main)PB_Main_init_zero;
        response->command_id = request->command_id;
        response->command_status = PB_CommandStatus_OK;
        response->has_next = true;
        response->which_content = PB_Main_poison_package_catalog_record_tag;
        rpc_poison_package_catalog_record_encode(
            &response->content.poison_package_catalog_record,
            &catalog->records[catalog_index],
            (uint32_t)catalog_index,
            catalog->generation);
        rpc_send(rpc_catalog->session, response);
    }

    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_package_catalog_end_tag;
    response->content.poison_package_catalog_end.returned_records = (uint32_t)returned;
    response->content.poison_package_catalog_end.total_records = (uint32_t)catalog->count;
    response->content.poison_package_catalog_end.next_offset =
        (uint32_t)(input->offset + returned);
    response->content.poison_package_catalog_end.generation = catalog->generation;
    memset(catalog, 0, sizeof(*catalog));
    free(catalog);
    rpc_send(rpc_catalog->session, response);
    free(response);
}

bool rpc_poison_package_catalog_list_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    const PoisonPackageCatalog* catalog,
    PoisonPackageCatalogRecord* records,
    size_t record_capacity,
    size_t* record_count) {
    if(!catalog || !records || !record_count || record_capacity < catalog->count || !channel ||
       strcmp(channel, "package-catalog") != 0) {
        return false;
    }
    if(poison_session_authenticate_rx(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag) != PoisonSessionResultOk) {
        return false;
    }
    memcpy(records, catalog->records, catalog->count * sizeof(*records));
    *record_count = catalog->count;
    return true;
}

void* rpc_system_poison_package_catalog_alloc(RpcSession* session) {
    RpcPoisonPackageCatalog* catalog = malloc(sizeof(*catalog));
    furi_check(catalog);
    catalog->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_package_catalog_process,
        .decode_submessage = NULL,
        .context = catalog,
    };
    rpc_add_handler(session, PB_Main_poison_package_catalog_request_tag, &handler);
    return catalog;
}

void rpc_system_poison_package_catalog_free(void* context) {
    if(!context) return;
    RpcPoisonPackageCatalog* catalog = context;
    memset(catalog, 0, sizeof(*catalog));
    free(catalog);
}
