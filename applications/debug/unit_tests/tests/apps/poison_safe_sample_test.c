#include "../test.h"
#include <string.h>
#include "../../../../main/poison_safe_sample/poison_safe_sample.h"

MU_TEST(poison_safe_sample_bounds_and_determinism) {
    char first[POISON_SAFE_SAMPLE_ARTIFACT_MAX];
    char second[POISON_SAFE_SAMPLE_ARTIFACT_MAX];
    mu_check(!poison_safe_sample_validate_parameter(0));
    mu_check(poison_safe_sample_validate_parameter(1));
    mu_check(poison_safe_sample_validate_parameter(100));
    mu_check(!poison_safe_sample_validate_parameter(101));
    mu_check(poison_safe_sample_generate_artifact(7, first, sizeof(first)) > 0);
    mu_check(poison_safe_sample_generate_artifact(7, second, sizeof(second)) > 0);
    mu_check(strcmp(first, second) == 0);
    mu_check(poison_safe_sample_generate_artifact(7, first, 4) == 0);
}

MU_TEST_SUITE(poison_safe_sample_suite) {
    MU_RUN_TEST(poison_safe_sample_bounds_and_determinism);
}
void poison_safe_sample_run_tests(void) {
    MU_RUN_SUITE(poison_safe_sample_suite);
}
