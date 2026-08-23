#include "../js_modules.h" // IWYU pragma: keep
#include <furi_hal_random.h>
#include <rpc/rpc_poison_crypto.h>

static const JsValueDeclaration js_crypto_one_int[] = {JS_VALUE_SIMPLE(JsValueTypeInt32)};
static const JsValueArguments js_crypto_one_int_args = JS_VALUE_ARGS(js_crypto_one_int);

static void js_crypto_random_bytes(struct mjs* mjs) {
    int32_t length;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_crypto_one_int_args, &length);
    if(length < 0 || length > 4096) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "randomBytes length must be 0..4096");
    }
    uint8_t bytes[4096];
    furi_hal_random_fill_buf(bytes, (uint32_t)length);
    mjs_return(mjs, mjs_mk_array_buf(mjs, (char*)bytes, (size_t)length));
}

static bool
    js_crypto_array_buffer(struct mjs* mjs, mjs_val_t value, const uint8_t** data, size_t* length) {
    if(!mjs_is_array_buf(value)) return false;
    char* bytes = mjs_array_buf_get_ptr(mjs, value, length);
    *data = (const uint8_t*)bytes;
    return *data != NULL || *length == 0;
}

static void js_crypto_sha256(struct mjs* mjs) {
    static const JsValueDeclaration args[] = {JS_VALUE_SIMPLE(JsValueTypeAny)};
    static const JsValueArguments parsed = JS_VALUE_ARGS(args);
    mjs_val_t value;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &parsed, &value);
    const uint8_t* data;
    size_t length;
    if(!js_crypto_array_buffer(mjs, value, &data, &length)) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "sha256 expects an ArrayBuffer");
    }
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
    if(poison_crypto_sha256(data, length, digest) != PoisonCryptoResultOk) {
        JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "sha256 failed");
    }
    mjs_return(mjs, mjs_mk_array_buf(mjs, (char*)digest, sizeof(digest)));
}

static void js_crypto_hmac_sha256(struct mjs* mjs) {
    static const JsValueDeclaration args[] = {
        JS_VALUE_SIMPLE(JsValueTypeAny), JS_VALUE_SIMPLE(JsValueTypeAny)};
    static const JsValueArguments parsed = JS_VALUE_ARGS(args);
    mjs_val_t key_value, data_value;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &parsed, &key_value, &data_value);
    const uint8_t *key, *data;
    size_t key_length, data_length;
    if(!js_crypto_array_buffer(mjs, key_value, &key, &key_length) ||
       !js_crypto_array_buffer(mjs, data_value, &data, &data_length)) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "hmacSha256 expects ArrayBuffer arguments");
    }
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
    if(poison_crypto_hmac_sha256(key, key_length, data, data_length, digest) !=
       PoisonCryptoResultOk) {
        JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "hmacSha256 failed");
    }
    mjs_return(mjs, mjs_mk_array_buf(mjs, (char*)digest, sizeof(digest)));
}

static void js_crypto_hkdf_sync(struct mjs* mjs) {
    static const JsValueDeclaration args[] = {
        JS_VALUE_SIMPLE(JsValueTypeAny),
        JS_VALUE_SIMPLE(JsValueTypeAny),
        JS_VALUE_SIMPLE(JsValueTypeAny),
        JS_VALUE_SIMPLE(JsValueTypeInt32),
    };
    static const JsValueArguments parsed = JS_VALUE_ARGS(args);
    mjs_val_t salt_value, ikm_value, info_value;
    int32_t length;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &parsed, &salt_value, &ikm_value, &info_value, &length);
    if(length <= 0 || length > (int32_t)POISON_CRYPTO_MAX_KDF_BYTES) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "hkdfSync length must be 1..64");
    }
    const uint8_t *salt, *ikm, *info;
    size_t salt_length, ikm_length, info_length;
    if(!js_crypto_array_buffer(mjs, salt_value, &salt, &salt_length) ||
       !js_crypto_array_buffer(mjs, ikm_value, &ikm, &ikm_length) ||
       !js_crypto_array_buffer(mjs, info_value, &info, &info_length)) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "hkdfSync expects ArrayBuffer arguments");
    }
    uint8_t output[POISON_CRYPTO_MAX_KDF_BYTES];
    PoisonCryptoResult result = poison_crypto_hkdf_sha256(
        salt, salt_length, ikm, ikm_length, info, info_length, output, (size_t)length);
    if(result != PoisonCryptoResultOk) {
        JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "hkdfSync failed");
    }
    mjs_return(mjs, mjs_mk_array_buf(mjs, (char*)output, (size_t)length));
}

static void* js_crypto_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules) {
    UNUSED(modules);
    *object = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, *object) {
        JS_FIELD("randomBytes", MJS_MK_FN(js_crypto_random_bytes));
        JS_FIELD("hkdfSync", MJS_MK_FN(js_crypto_hkdf_sync));
        JS_FIELD("sha256", MJS_MK_FN(js_crypto_sha256));
        JS_FIELD("hmacSha256", MJS_MK_FN(js_crypto_hmac_sha256));
    }
    return (void*)1;
}

static const JsModuleDescriptor js_crypto_desc = {"crypto", js_crypto_create, NULL, NULL};
static const FlipperAppPluginDescriptor js_crypto_plugin = {
    .appid = PLUGIN_APP_ID,
    .ep_api_version = PLUGIN_API_VERSION,
    .entry_point = &js_crypto_desc,
};

const FlipperAppPluginDescriptor* js_crypto_ep(void) {
    return &js_crypto_plugin;
}
