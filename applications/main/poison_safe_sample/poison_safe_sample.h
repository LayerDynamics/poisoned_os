#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_SAFE_SAMPLE_MIN_PARAMETER (1u)
#define POISON_SAFE_SAMPLE_MAX_PARAMETER (100u)
#define POISON_SAFE_SAMPLE_ARTIFACT_MAX  (128u)

bool poison_safe_sample_validate_parameter(uint32_t parameter);
size_t
    poison_safe_sample_generate_artifact(uint32_t parameter, char* output, size_t output_capacity);
int32_t poison_safe_sample_app(void* context);
