#include "../test.h"
#include "../../../../services/poison_rust_api/poison_rust_api.h"

MU_TEST(poison_rust_api_rejects_invalid_slices_and_paths) {
    const uint8_t path[] = "/ext/workload";
    mu_check(poison_rust_byte_slice_valid(
        (PoisonRustByteSlice){.ptr = path, .len = sizeof(path) - 1u}, POISON_RUST_MAX_PATH));
    mu_check(
        poison_rust_validate_path((PoisonRustByteSlice){.ptr = path, .len = sizeof(path) - 1u}) ==
        PoisonRustStatusOk);
    mu_check(!poison_rust_byte_slice_valid(
        (PoisonRustByteSlice){.ptr = NULL, .len = 1u}, POISON_RUST_MAX_PATH));
    mu_check(
        poison_rust_validate_path((PoisonRustByteSlice){
            .ptr = (const uint8_t*)"relative", .len = 8u}) == PoisonRustStatusInvalidArgument);
}

MU_TEST(poison_rust_api_enforces_capabilities) {
    mu_check(poison_rust_check_capability(1u, true) == PoisonRustStatusOk);
    mu_check(poison_rust_check_capability(1u, false) == PoisonRustStatusDenied);
    mu_check(poison_rust_check_capability(0u, true) == PoisonRustStatusInvalidArgument);
}

MU_TEST_SUITE(poison_rust_api_suite) {
    MU_RUN_TEST(poison_rust_api_rejects_invalid_slices_and_paths);
    MU_RUN_TEST(poison_rust_api_enforces_capabilities);
}

void poison_rust_api_run_tests(void) {
    MU_RUN_SUITE(poison_rust_api_suite);
}
