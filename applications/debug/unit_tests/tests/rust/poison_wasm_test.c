#include "../test.h"
#include <lib/poison_wasm/poison_wasm.h>
#include <lib/poison_wasm/poison_wasm_host.h>
#include <lib/poison_wasm/poison_wasm_limits.h>
#include <applications/services/poison_workload/poison_workload_wasm_adapter.h>

MU_TEST(poison_wasm_admission_rejects_forged_and_accepts_bounded_modules) {
    static const uint8_t valid[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00, 'p', 'o', 'i', 's', 'o', 'n', '_', 'r'};
    static const uint8_t forged[] = {
        0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00, 'w', 'a', 's', 'i', '_'};
    PoisonWasmModule module;
    mu_check(poison_wasm_admit(valid, sizeof(valid), &module) == PoisonWasmOk);
    mu_check(poison_wasm_admit(forged, sizeof(forged), &module) == PoisonWasmForgedImport);
    mu_check(!poison_wasm_runtime_available());
}

MU_TEST(poison_wasm_limits_trip_and_cleanup) {
    PoisonWasmLimits limits;
    poison_wasm_limits_init(&limits, 10u, 100u, 2u, 4u, 1u);
    mu_check(poison_wasm_limits_consume(&limits, 5u, 20u, 1u, 1u, 0u));
    mu_check(!poison_wasm_limits_consume(&limits, 6u, 0u, 0u, 0u, 0u));
    mu_check(limits.tripped);
    PoisonWorkloadWasmAdapter adapter;
    mu_check(poison_workload_wasm_admit(&adapter, (const uint8_t[]){0}, 1u, 1u) == false);
    poison_workload_wasm_cleanup(&adapter);
    mu_check(!adapter.admitted);
}

MU_TEST_SUITE(poison_wasm_suite) {
    MU_RUN_TEST(poison_wasm_admission_rejects_forged_and_accepts_bounded_modules);
    MU_RUN_TEST(poison_wasm_limits_trip_and_cleanup);
}
void poison_wasm_run_tests(void) {
    MU_RUN_SUITE(poison_wasm_suite);
}
