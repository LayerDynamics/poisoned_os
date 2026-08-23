#include "../test.h"
#include "../../../../services/poison_evidence/poison_annotation.h"
#include "../../../../services/poison_evidence/poison_case.h"
#include "../../../../services/poison_evidence/poison_evidence.h"
#include "../../../../services/poison_evidence/poison_evidence_i.h"
#include "../../../../services/poison_evidence/poison_tool_run.h"
#include <storage/storage.h>

MU_TEST(poison_evidence_is_immutable_and_content_addressed) {
    PoisonEvidenceStore store;
    PoisonEvidenceStore linked_store;
    uint8_t previous[32] = {0};
    uint8_t linked_previous[32] = {0};
    linked_previous[0] = 1u;
    const uint8_t bytes[] = "known evidence";
    poison_evidence_store_init(&store);
    mu_check(poison_evidence_capture(
        &store, "ev-1", "case-1", bytes, sizeof(bytes) - 1u, false, previous));
    mu_check(!poison_evidence_capture(
        &store, "ev-1", "case-1", bytes, sizeof(bytes) - 1u, false, previous));
    mu_check(poison_evidence_find(&store, "ev-1")->content_length == sizeof(bytes) - 1u);
    poison_evidence_store_init(&linked_store);
    mu_check(poison_evidence_capture(
        &linked_store, "ev-1", "case-1", bytes, sizeof(bytes) - 1u, false, linked_previous));
    mu_check(
        memcmp(
            poison_evidence_find(&store, "ev-1")->content_sha256,
            poison_evidence_find(&linked_store, "ev-1")->content_sha256,
            32u) == 0);
    mu_check(
        memcmp(
            poison_evidence_find(&store, "ev-1")->audit_sha256,
            poison_evidence_find(&linked_store, "ev-1")->audit_sha256,
            32u) != 0);
}

MU_TEST(poison_case_and_run_transitions_are_bounded) {
    PoisonEvidenceCase evidence_case = {0};
    PoisonToolRun run = {0};
    mu_check(poison_case_create(&evidence_case, "case-1", "Training"));
    mu_check(poison_tool_run_start(&run, "run-1"));
    mu_check(poison_tool_run_finish(&run, PoisonToolRunCompleted));
    mu_check(poison_case_close(&evidence_case));
    mu_check(!poison_case_close(&evidence_case));
}

MU_TEST(poison_evidence_ids_reject_path_traversal) {
    PoisonEvidenceCase evidence_case = {0};
    mu_check(!poison_case_create(&evidence_case, "../case", "Traversal"));
    mu_check(!poison_case_create(&evidence_case, ".hidden", "Hidden"));
    mu_check(!poison_annotation_validate("../annotation", "evidence-1", "note"));
    mu_check(!poison_annotation_validate("annotation-1", "../evidence", "note"));
    mu_check(poison_case_create(&evidence_case, "case.safe-1", "Safe"));
    mu_check(poison_annotation_validate("annotation.safe-1", "evidence.safe-1", "note"));
}

MU_TEST(poison_evidence_transaction_publishes_only_after_complete_append) {
    PoisonEvidenceStore store;
    PoisonEvidenceTransaction transaction;
    uint8_t previous[32] = {0};
    const uint8_t first[] = "partial";
    const uint8_t second[] = " evidence";
    poison_evidence_store_init(&store);
    mu_check(poison_evidence_begin(
        &store,
        &transaction,
        "ev-2",
        "case-1",
        sizeof(first) - 1u + sizeof(second) - 1u,
        true,
        previous));
    mu_check(poison_evidence_append(&transaction, first, sizeof(first) - 1u));
    mu_check(poison_evidence_find(&store, "ev-2") == NULL);
    mu_check(poison_evidence_append(&transaction, second, sizeof(second) - 1u));
    mu_check(poison_evidence_commit(&store, &transaction));
    mu_check(poison_evidence_find(&store, "ev-2") != NULL);
    mu_check(!poison_evidence_append(&transaction, second, 1u));
}

MU_TEST(poison_evidence_global_capture_persists_object_and_record) {
    char evidence_id[65];
    snprintf(evidence_id, sizeof(evidence_id), "ev-persist-%lu", (unsigned long)furi_get_tick());
    const uint8_t bytes[] = "persistent evidence";
    const uint8_t previous_audit[32] = {0};
    mu_check(poison_evidence_capture_global(
        evidence_id, "case-persist", bytes, sizeof(bytes) - 1u, false, previous_audit));

    char record_path[192];
    snprintf(record_path, sizeof(record_path), "/ext/evidence/records/%s.pev", evidence_id);
    const char* object_path =
        "/ext/evidence/objects/30d0fe0a543c1674d5331e7ec6a19b2d4aa876c1c92654698a673c4ac71dec7d.bin";
    Storage* storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_file_exists(storage, record_path));
    mu_check(storage_file_exists(storage, object_path));
    File* record = storage_file_alloc(storage);
    uint8_t header[8];
    mu_check(storage_file_open(record, record_path, FSAM_READ, FSOM_OPEN_EXISTING));
    mu_check(storage_file_read(record, header, sizeof(header)) == sizeof(header));
    mu_check(memcmp(header, "POISEV1", 7u) == 0);
    storage_file_close(record);
    storage_file_free(record);
    mu_check(storage_simply_remove(storage, record_path));
    mu_check(storage_simply_remove(storage, object_path));
    furi_record_close(RECORD_STORAGE);
}

MU_TEST_SUITE(poison_evidence_suite) {
    MU_RUN_TEST(poison_evidence_is_immutable_and_content_addressed);
    MU_RUN_TEST(poison_case_and_run_transitions_are_bounded);
    MU_RUN_TEST(poison_evidence_ids_reject_path_traversal);
    MU_RUN_TEST(poison_evidence_transaction_publishes_only_after_complete_append);
    MU_RUN_TEST(poison_evidence_global_capture_persists_object_and_record);
}
void poison_evidence_run_tests(void) {
    MU_RUN_SUITE(poison_evidence_suite);
}
