#include "../test.h"

#include <string.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <poison_files.pb.h>

MU_TEST(poison_file_contract_round_trip_preserves_logical_path) {
    PB_Poison_LogicalPath input = PB_Poison_LogicalPath_init_zero;
    strncpy(input.path, "/evidence/run-1", sizeof(input.path) - 1u);
    uint8_t buffer[PB_Poison_LogicalPath_size] = {0};
    pb_ostream_t output = pb_ostream_from_buffer(buffer, sizeof(buffer));
    mu_check(pb_encode(&output, PB_Poison_LogicalPath_fields, &input));

    PB_Poison_LogicalPath decoded = PB_Poison_LogicalPath_init_zero;
    pb_istream_t input_stream = pb_istream_from_buffer(buffer, output.bytes_written);
    mu_check(pb_decode(&input_stream, PB_Poison_LogicalPath_fields, &decoded));
    mu_assert_string_eq("/evidence/run-1", decoded.path);
}

MU_TEST_SUITE(poison_file_contract_suite) {
    MU_RUN_TEST(poison_file_contract_round_trip_preserves_logical_path);
}

void poison_file_contract_run_tests(void) {
    MU_RUN_SUITE(poison_file_contract_suite);
}
