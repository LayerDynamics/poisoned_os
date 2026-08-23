#include <furi.h>
#include <stdint.h>

#include <rpc/rpc.h>
#include <rpc/rpc_i.h>
#include <storage/storage.h>
#include <loader/loader.h>
#include <storage/filesystem_api_defines.h>

#include <lib/toolbox/api_lock.h>
#include <lib/toolbox/md5_calc.h>
#include <lib/toolbox/path.h>

#include <m-list.h>
#include "../test.h" // IWYU pragma: keep

#include <protobuf_version.h>
#include <pb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <storage.pb.h>
#include <flipper.pb.h>
#include <applications/services/poison_diagnostics/poison_diagnostics.h>
#include <applications/services/poison_app/poison_app.h>
#include <applications/services/poison_audit/poison_audit.h>
#include <applications/services/poison_tools/poison_tools.h>

LIST_DEF(MsgList, PB_Main, M_POD_OPLIST)
#define M_OPL_MsgList_t() LIST_OPLIST(MsgList)

#define TEST_RPC_SESSIONS 2

/* MinUnit test framework doesn't allow passing context into tests,
 * so we have to use global variables
 */
static Rpc* rpc = NULL;
static uint32_t command_id = 0;

typedef struct {
    RpcSession* session;
    FuriStreamBuffer* output_stream;
    FuriApiLock session_close_lock;
    FuriApiLock session_terminate_lock;
    uint32_t timeout;
} RpcSessionContext;

static RpcSessionContext rpc_session[TEST_RPC_SESSIONS];

#define TAG "UnitTestsRpc"

#define MAX_RECEIVE_OUTPUT_TIMEOUT 3000
#define MAX_NAME_LENGTH            255
#define MAX_DATA_SIZE              512u // have to be exact as in rpc_storage.c
#define TEST_DIR_NAME              EXT_PATH(".tmp/unit_tests/rpc")
#define TEST_DIR                   TEST_DIR_NAME "/"
#define MD5SUM_SIZE                16

#define PING_REQUEST  0
#define PING_RESPONSE 1
#define WRITE_REQUEST 0
#define READ_RESPONSE 1

#define DEBUG_PRINT 0

void poison_session_run_tests(void);
void poison_channel_run_tests(void);
void poison_crypto_run_tests(void);
void poison_session_state_run_tests(void);
void poison_policy_run_tests(void);
void poison_confirmation_run_tests(void);
void poison_diagnostics_run_tests(void);
void test_poison_pairing_ui(void);
void poison_file_contract_run_tests(void);
void poison_vfs_path_run_tests(void);
void poison_vfs_journal_run_tests(void);
void poison_migration_run_tests(void);
void poison_safe_sample_run_tests(void);
void poison_js_developer_policy_run_tests(void);
void poison_evidence_run_tests(void);
void poison_evidence_rpc_run_tests(void);
void poison_workspace_run_tests(void);
void poison_package_verify_run_tests(void);
void poison_package_transaction_run_tests(void);
void poison_package_catalog_run_tests(void);
void poison_app_protocol_run_tests(void);
void poison_profiles_run_tests(void);
void poison_tools_catalog_run_tests(void);
void poison_tool_nfc_run_tests(void);
void poison_tool_lfrfid_run_tests(void);
void poison_tool_ibutton_run_tests(void);
void poison_tool_infrared_run_tests(void);
void poison_tool_subghz_run_tests(void);
void poison_tool_gpio_run_tests(void);
void poison_rust_api_run_tests(void);
void poison_js_capabilities_run_tests(void);
void poison_js_limits_run_tests(void);
void poison_content_update_run_tests(void);
void poison_workload_run_tests(void);
void poison_lessons_run_tests(void);
void poison_assignments_run_tests(void);
void poison_wasm_run_tests(void);

#define BYTES(x) (x), sizeof(x)

#define DISABLE_TEST(code)  \
    do {                    \
        volatile int a = 0; \
        if(a) {             \
            code            \
        }                   \
    } while(0)

static void output_bytes_callback(void* ctx, uint8_t* got_bytes, size_t got_size);
static void
    test_rpc_add_empty_to_list(MsgList_t msg_list, PB_CommandStatus status, uint32_t command_id);
static void test_rpc_encode_and_feed(MsgList_t msg_list, uint8_t session);
static void test_rpc_encode_and_feed_one(PB_Main* request, uint8_t session);
static void test_rpc_compare_messages(PB_Main* result, PB_Main* expected);
static void test_rpc_decode_and_compare(MsgList_t expected_msg_list, uint8_t session);
static void test_rpc_free_msg_list(MsgList_t msg_list);
static void test_rpc_session_close_callback(void* context);
static void test_rpc_session_terminated_callback(void* context);

static void test_rpc_setup_owner(RpcOwner owner) {
    furi_check(!rpc);
    furi_check(!(rpc_session[0].session));

    rpc = furi_record_open(RECORD_RPC);
    for(int i = 0; !(rpc_session[0].session) && (i < 10000); ++i) {
        rpc_session[0].session = rpc_session_open(rpc, owner);
        furi_delay_tick(1);
    }
    furi_check(rpc_session[0].session);

    rpc_session[0].output_stream = furi_stream_buffer_alloc(4096, 1);
    rpc_session_set_send_bytes_callback(rpc_session[0].session, output_bytes_callback);
    rpc_session[0].session_close_lock = api_lock_alloc_locked();
    rpc_session[0].session_terminate_lock = api_lock_alloc_locked();
    rpc_session_set_close_callback(rpc_session[0].session, test_rpc_session_close_callback);
    rpc_session_set_terminated_callback(
        rpc_session[0].session, test_rpc_session_terminated_callback);
    rpc_session_set_context(rpc_session[0].session, &rpc_session[0]);
}

static void test_rpc_setup(void) {
    test_rpc_setup_owner(RpcOwnerUnknown);
}

static void test_rpc_setup_second_session(void) {
    furi_check(rpc);
    furi_check(!(rpc_session[1].session));

    for(int i = 0; !(rpc_session[1].session) && (i < 10000); ++i) {
        rpc_session[1].session = rpc_session_open(rpc, RpcOwnerUnknown);
        furi_delay_tick(1);
    }
    furi_check(rpc_session[1].session);

    rpc_session[1].output_stream = furi_stream_buffer_alloc(1000, 1);
    rpc_session_set_send_bytes_callback(rpc_session[1].session, output_bytes_callback);
    rpc_session[1].session_close_lock = api_lock_alloc_locked();
    rpc_session[1].session_terminate_lock = api_lock_alloc_locked();
    rpc_session_set_close_callback(rpc_session[1].session, test_rpc_session_close_callback);
    rpc_session_set_terminated_callback(
        rpc_session[1].session, test_rpc_session_terminated_callback);
    rpc_session_set_context(rpc_session[1].session, &rpc_session[1]);
}

static void test_rpc_teardown(void) {
    furi_check(rpc_session[0].session_close_lock);
    api_lock_relock(rpc_session[0].session_terminate_lock);
    rpc_session_close(rpc_session[0].session);
    api_lock_wait_unlock(rpc_session[0].session_terminate_lock);
    furi_record_close(RECORD_RPC);
    furi_stream_buffer_free(rpc_session[0].output_stream);
    api_lock_free(rpc_session[0].session_close_lock);
    api_lock_free(rpc_session[0].session_terminate_lock);
    ++command_id;
    rpc_session[0].output_stream = NULL;
    rpc_session[0].session_close_lock = NULL;
    rpc = NULL;
    rpc_session[0].session = NULL;
}

static void test_rpc_teardown_second_session(void) {
    furi_check(rpc_session[1].session_close_lock);
    api_lock_relock(rpc_session[1].session_terminate_lock);
    rpc_session_close(rpc_session[1].session);
    api_lock_wait_unlock(rpc_session[1].session_terminate_lock);
    furi_stream_buffer_free(rpc_session[1].output_stream);
    api_lock_free(rpc_session[1].session_close_lock);
    api_lock_free(rpc_session[1].session_terminate_lock);
    ++command_id;
    rpc_session[1].output_stream = NULL;
    rpc_session[1].session_close_lock = NULL;
    rpc_session[1].session = NULL;
}

static void test_rpc_storage_clean_directory(Storage* fs_api, const char* clean_dir) {
    furi_check(fs_api);
    furi_check(clean_dir);
    storage_simply_remove_recursive(fs_api, clean_dir);
    FS_Error error = storage_common_mkdir(fs_api, clean_dir);
    furi_check(error == FSE_OK);
}

static void test_rpc_storage_create_file(Storage* fs_api, const char* path, size_t size) {
    File* file = storage_file_alloc(fs_api);

    bool success = false;
    do {
        if(!storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;
        if(!storage_file_seek(file, size, true)) break;
        success = true;
    } while(false);

    storage_file_close(file);
    storage_file_free(file);

    furi_check(success);
}

static void test_rpc_storage_setup(void) {
    test_rpc_setup();

    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    test_rpc_storage_clean_directory(fs_api, TEST_DIR_NAME);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file100", 100);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file250", 250);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file500", 200);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file1000", 1000);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file2500", 2500);
    test_rpc_storage_create_file(fs_api, TEST_DIR_NAME "/file5000", 5000);
    furi_record_close(RECORD_STORAGE);
}

static void test_rpc_storage_teardown(void) {
    test_rpc_teardown();

    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    test_rpc_storage_clean_directory(fs_api, TEST_DIR_NAME);
    furi_record_close(RECORD_STORAGE);
}

static void test_rpc_session_close_callback(void* context) {
    furi_check(context);
    RpcSessionContext* callbacks_context = context;

    api_lock_unlock(callbacks_context->session_close_lock);
}

static void test_rpc_session_terminated_callback(void* context) {
    furi_check(context);
    RpcSessionContext* callbacks_context = context;

    api_lock_unlock(callbacks_context->session_terminate_lock);
}

static void test_rpc_print_message_list(MsgList_t msg_list) {
#if DEBUG_PRINT
    MsgList_reverse(msg_list);
    for
        M_EACH(msg, msg_list, MsgList_t) {
            rpc_debug_print_message(msg);
        }
    MsgList_reverse(msg_list);
#else
    UNUSED(msg_list);
#endif
}

static PB_CommandStatus test_rpc_storage_get_file_error(File* file) {
    FS_Error fs_error = storage_file_get_error(file);
    PB_CommandStatus pb_error;
    switch(fs_error) {
    case FSE_OK:
        pb_error = PB_CommandStatus_OK;
        break;
    case FSE_INVALID_NAME:
        pb_error = PB_CommandStatus_ERROR_STORAGE_INVALID_NAME;
        break;
    case FSE_INVALID_PARAMETER:
        pb_error = PB_CommandStatus_ERROR_STORAGE_INVALID_PARAMETER;
        break;
    case FSE_INTERNAL:
        pb_error = PB_CommandStatus_ERROR_STORAGE_INTERNAL;
        break;
    case FSE_ALREADY_OPEN:
        pb_error = PB_CommandStatus_ERROR_STORAGE_ALREADY_OPEN;
        break;
    case FSE_DENIED:
        pb_error = PB_CommandStatus_ERROR_STORAGE_DENIED;
        break;
    case FSE_EXIST:
        pb_error = PB_CommandStatus_ERROR_STORAGE_EXIST;
        break;
    case FSE_NOT_EXIST:
        pb_error = PB_CommandStatus_ERROR_STORAGE_NOT_EXIST;
        break;
    case FSE_NOT_READY:
        pb_error = PB_CommandStatus_ERROR_STORAGE_NOT_READY;
        break;
    case FSE_NOT_IMPLEMENTED:
        pb_error = PB_CommandStatus_ERROR_STORAGE_NOT_IMPLEMENTED;
        break;
    default:
        pb_error = PB_CommandStatus_ERROR;
        break;
    }

    return pb_error;
}

static void output_bytes_callback(void* ctx, uint8_t* got_bytes, size_t got_size) {
    RpcSessionContext* callbacks_context = ctx;

    size_t bytes_sent = furi_stream_buffer_send(
        callbacks_context->output_stream, got_bytes, got_size, FuriWaitForever);
    (void)bytes_sent;
    furi_check(bytes_sent == got_size);
}

static void test_rpc_add_ping_to_list(MsgList_t msg_list, bool request, uint32_t command_id) {
    PB_Main* response = MsgList_push_new(msg_list);
    response->command_id = command_id;
    response->command_status = PB_CommandStatus_OK;
    response->cb_content.funcs.encode = NULL;
    response->has_next = false;
    response->which_content = (request == PING_REQUEST) ? PB_Main_system_ping_request_tag :
                                                          PB_Main_system_ping_response_tag;
}
static void test_rpc_fill_basic_message(PB_Main* message, uint16_t tag, uint32_t command_id) {
    message->command_id = command_id;
    message->command_status = PB_CommandStatus_OK;
    message->cb_content.funcs.encode = NULL;
    message->which_content = tag;
    message->has_next = false;
}

static void test_rpc_create_storage_list_request(
    PB_Main* message,
    const char* path,
    bool include_md5,
    uint32_t command_id,
    uint32_t filter_max_size) {
    furi_check(message);
    furi_check(path);
    test_rpc_fill_basic_message(message, PB_Main_storage_list_request_tag, command_id);
    message->content.storage_list_request.path = strdup(path);
    message->content.storage_list_request.include_md5 = include_md5;
    message->content.storage_list_request.filter_max_size = filter_max_size;
}

static void test_rpc_create_simple_message(
    PB_Main* message,
    uint16_t tag,
    const char* str,
    uint32_t command_id) {
    furi_check(message);

    char* str_copy = NULL;
    if(str) {
        str_copy = strdup(str);
    }
    test_rpc_fill_basic_message(message, tag, command_id);
    switch(tag) {
    case PB_Main_storage_info_request_tag:
        message->content.storage_info_request.path = str_copy;
        break;
    case PB_Main_storage_stat_request_tag:
        message->content.storage_stat_request.path = str_copy;
        break;
    case PB_Main_storage_mkdir_request_tag:
        message->content.storage_mkdir_request.path = str_copy;
        break;
    case PB_Main_storage_read_request_tag:
        message->content.storage_read_request.path = str_copy;
        break;
    case PB_Main_storage_delete_request_tag:
        message->content.storage_delete_request.path = str_copy;
        break;
    case PB_Main_storage_md5sum_request_tag:
        message->content.storage_md5sum_request.path = str_copy;
        break;
    case PB_Main_storage_md5sum_response_tag: {
        char* md5sum = message->content.storage_md5sum_response.md5sum;
        size_t md5sum_size = sizeof(message->content.storage_md5sum_response.md5sum);
        furi_check((strlen(str) + 1) <= md5sum_size);
        memcpy(md5sum, str_copy, md5sum_size);
        free(str_copy);
        break;
    }
    default:
        furi_check(0);
        break;
    }
}

static void test_rpc_add_read_or_write_to_list(
    MsgList_t msg_list,
    bool write,
    const char* path,
    const uint8_t* pattern,
    size_t pattern_size,
    size_t pattern_repeats,
    uint32_t command_id) {
    furi_check(pattern_repeats > 0);

    do {
        PB_Main* request = MsgList_push_new(msg_list);
        PB_Storage_File* msg_file = NULL;

        request->command_id = command_id;
        request->command_status = PB_CommandStatus_OK;

        if(write == WRITE_REQUEST) {
            request->content.storage_write_request.path = strdup(path);
            request->which_content = PB_Main_storage_write_request_tag;
            request->content.storage_write_request.has_file = true;
            msg_file = &request->content.storage_write_request.file;
        } else {
            request->which_content = PB_Main_storage_read_response_tag;
            request->content.storage_read_response.has_file = true;
            msg_file = &request->content.storage_read_response.file;
        }

        msg_file->data = malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(pattern_size));
        msg_file->data->size = pattern_size;

        memcpy(msg_file->data->bytes, pattern, pattern_size);

        --pattern_repeats;
        request->has_next = (pattern_repeats > 0);
    } while(pattern_repeats);
}

static void test_rpc_encode_and_feed_one(PB_Main* request, uint8_t session) {
    furi_check(request);
    furi_check(session < TEST_RPC_SESSIONS);

    pb_ostream_t ostream = PB_OSTREAM_SIZING;

    bool result = pb_encode_ex(&ostream, &PB_Main_msg, request, PB_ENCODE_DELIMITED);
    furi_check(result && ostream.bytes_written);

    uint8_t* buffer = malloc(ostream.bytes_written);
    ostream = pb_ostream_from_buffer(buffer, ostream.bytes_written);

    pb_encode_ex(&ostream, &PB_Main_msg, request, PB_ENCODE_DELIMITED);

    size_t bytes_left = ostream.bytes_written;
    uint8_t* buffer_ptr = buffer;
    do {
        size_t bytes_sent =
            rpc_session_feed(rpc_session[session].session, buffer_ptr, bytes_left, 1000);
        mu_check(bytes_sent > 0);

        bytes_left -= bytes_sent;
        buffer_ptr += bytes_sent;
    } while(bytes_left);

    free(buffer);
    pb_release(&PB_Main_msg, request);
}

static void test_rpc_encode_and_feed(MsgList_t msg_list, uint8_t session) {
    MsgList_reverse(msg_list);
    for
        M_EACH(request, msg_list, MsgList_t) {
            test_rpc_encode_and_feed_one(request, session);
        }
    MsgList_reverse(msg_list);
}

static void
    test_rpc_compare_file(PB_Storage_File* result_msg_file, PB_Storage_File* expected_msg_file) {
    mu_check(!result_msg_file->name == !expected_msg_file->name);
    if(result_msg_file->name) {
        mu_check(!strcmp(result_msg_file->name, expected_msg_file->name));
    }
    mu_check(result_msg_file->size == expected_msg_file->size);
    mu_check(result_msg_file->type == expected_msg_file->type);
    mu_assert_string_eq(expected_msg_file->md5sum, result_msg_file->md5sum);

    if(result_msg_file->data && result_msg_file->type != PB_Storage_File_FileType_DIR) {
        mu_check(!result_msg_file->data == !expected_msg_file->data); // Zlo: WTF???
        mu_check(result_msg_file->data->size == expected_msg_file->data->size);
        for(int i = 0; i < result_msg_file->data->size; ++i) {
            mu_check(result_msg_file->data->bytes[i] == expected_msg_file->data->bytes[i]);
        }
    }
}

static void test_rpc_compare_messages(PB_Main* result, PB_Main* expected) {
    mu_assert_int_eq(expected->command_id, result->command_id);
    mu_assert_int_eq(expected->command_status, result->command_status);
    mu_assert_int_eq(expected->has_next, result->has_next);
    mu_assert_int_eq(expected->which_content, result->which_content);
    if(result->command_status != PB_CommandStatus_OK) {
        mu_check(result->which_content == PB_Main_empty_tag);
    }

    switch(result->which_content) {
    case PB_Main_empty_tag:
    case PB_Main_system_ping_response_tag:
        /* nothing to check */
        break;
    case PB_Main_system_ping_request_tag:
    case PB_Main_storage_list_request_tag:
    case PB_Main_storage_read_request_tag:
    case PB_Main_storage_write_request_tag:
    case PB_Main_storage_delete_request_tag:
    case PB_Main_storage_mkdir_request_tag:
    case PB_Main_storage_md5sum_request_tag:
        /* rpc doesn't send it */
        mu_check(0);
        break;
    case PB_Main_app_lock_status_response_tag: {
        bool result_locked = result->content.app_lock_status_response.locked;
        bool expected_locked = expected->content.app_lock_status_response.locked;
        mu_check(result_locked == expected_locked);
        mu_assert_string_eq(
            expected->content.app_lock_status_response.active_application,
            result->content.app_lock_status_response.active_application);
        break;
    }
    case PB_Main_storage_info_response_tag: {
        uint64_t result_total_space = result->content.storage_info_response.total_space;
        uint64_t expected_total_space = expected->content.storage_info_response.total_space;
        mu_check(result_total_space == expected_total_space);

        uint64_t result_free_space = result->content.storage_info_response.free_space;
        uint64_t expected_free_space = expected->content.storage_info_response.free_space;
        mu_check(result_free_space == expected_free_space);
    } break;
    case PB_Main_storage_stat_response_tag: {
        bool result_has_msg_file = result->content.storage_stat_response.has_file;
        bool expected_has_msg_file = expected->content.storage_stat_response.has_file;
        mu_check(result_has_msg_file == expected_has_msg_file);

        if(result_has_msg_file) {
            PB_Storage_File* result_msg_file = &result->content.storage_stat_response.file;
            PB_Storage_File* expected_msg_file = &expected->content.storage_stat_response.file;
            test_rpc_compare_file(result_msg_file, expected_msg_file);
        } else {
            mu_check(0);
        }
    } break;
    case PB_Main_storage_read_response_tag: {
        bool result_has_msg_file = result->content.storage_read_response.has_file;
        bool expected_has_msg_file = expected->content.storage_read_response.has_file;
        mu_check(result_has_msg_file == expected_has_msg_file);

        if(result_has_msg_file) {
            PB_Storage_File* result_msg_file = &result->content.storage_read_response.file;
            PB_Storage_File* expected_msg_file = &expected->content.storage_read_response.file;
            test_rpc_compare_file(result_msg_file, expected_msg_file);
        } else {
            mu_check(0);
        }
    } break;
    case PB_Main_storage_list_response_tag: {
        size_t expected_msg_files = expected->content.storage_list_response.file_count;
        size_t result_msg_files = result->content.storage_list_response.file_count;
        mu_assert_int_eq(expected_msg_files, result_msg_files);
        for(size_t i = 0; i < expected_msg_files; ++i) {
            PB_Storage_File* result_msg_file = &result->content.storage_list_response.file[i];
            PB_Storage_File* expected_msg_file = &expected->content.storage_list_response.file[i];
            test_rpc_compare_file(result_msg_file, expected_msg_file);
        }
        break;
    }
    case PB_Main_storage_md5sum_response_tag: {
        char* result_md5sum = result->content.storage_md5sum_response.md5sum;
        char* expected_md5sum = expected->content.storage_md5sum_response.md5sum;
        mu_check(!strcmp(result_md5sum, expected_md5sum));
        break;
    }
    case PB_Main_system_protobuf_version_response_tag: {
        uint32_t major_version_expected = expected->content.system_protobuf_version_response.major;
        uint32_t minor_version_expected = expected->content.system_protobuf_version_response.minor;
        uint32_t major_version_result = result->content.system_protobuf_version_response.major;
        uint32_t minor_version_result = result->content.system_protobuf_version_response.minor;
        mu_check(major_version_expected == major_version_result);
        mu_check(minor_version_expected == minor_version_result);
        break;
    }
    default:
        furi_check(0);
        break;
    }
}

static bool test_rpc_pb_stream_read(pb_istream_t* istream, pb_byte_t* buf, size_t count) {
    RpcSessionContext* session_context = istream->state;
    size_t bytes_received = 0;

    uint32_t now = furi_get_tick();
    int32_t time_left = session_context->timeout - now;
    time_left = MAX(time_left, 0);
    bytes_received =
        furi_stream_buffer_receive(session_context->output_stream, buf, count, time_left);
    return count == bytes_received;
}

static void
    test_rpc_storage_list_create_expected_list_root(MsgList_t msg_list, uint32_t command_id) {
    PB_Main* message = MsgList_push_new(msg_list);
    message->has_next = false;
    message->cb_content.funcs.encode = NULL;
    message->command_id = command_id;
    message->which_content = PB_Main_storage_list_response_tag;

    message->content.storage_list_response.file_count = 3;
    message->content.storage_list_response.file[0].data = NULL;
    message->content.storage_list_response.file[1].data = NULL;
    message->content.storage_list_response.file[2].data = NULL;

    message->content.storage_list_response.file[0].size = 0;
    message->content.storage_list_response.file[1].size = 0;
    message->content.storage_list_response.file[2].size = 0;

    message->content.storage_list_response.file[0].type = PB_Storage_File_FileType_DIR;
    message->content.storage_list_response.file[1].type = PB_Storage_File_FileType_DIR;
    message->content.storage_list_response.file[2].type = PB_Storage_File_FileType_DIR;

    char* str = malloc(4);
    strcpy(str, "any");
    message->content.storage_list_response.file[0].name = str;
    str = malloc(4);
    strcpy(str, "int");
    message->content.storage_list_response.file[1].name = str;
    str = malloc(4);
    strcpy(str, "ext");
    message->content.storage_list_response.file[2].name = str;
}

static bool test_rpc_system_storage_list_filter(
    const FileInfo* fileinfo,
    const char* name,
    size_t filter_max_size) {
    bool result = false;

    do {
        if(!path_contains_only_ascii(name)) break;
        if(filter_max_size) {
            if(fileinfo->size > filter_max_size) break;
        }
        result = true;
    } while(false);

    return result;
}

static void test_rpc_storage_list_create_expected_list(
    MsgList_t msg_list,
    const char* path,
    uint32_t command_id,
    bool append_md5,
    size_t filter_max_size) {
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    File* dir = storage_file_alloc(fs_api);

    FuriString* md5 = furi_string_alloc();
    FuriString* md5_path = furi_string_alloc();
    File* file = storage_file_alloc(fs_api);

    PB_Main response = {
        .command_id = command_id,
        .has_next = false,
        .which_content = PB_Main_storage_list_response_tag,
        /* other fields (e.g. msg_files ptrs) explicitly initialized by 0 */
    };
    PB_Storage_ListResponse* list = &response.content.storage_list_response;

    bool finish = false;
    int i = 0;

    if(storage_dir_open(dir, path)) {
        response.command_status = PB_CommandStatus_OK;
    } else {
        response.command_status = test_rpc_storage_get_file_error(dir);
        response.which_content = PB_Main_empty_tag;
        finish = true;
    }

    while(!finish) {
        FileInfo fileinfo;
        char* name = malloc(MAX_NAME_LENGTH + 1);
        if(storage_dir_read(dir, &fileinfo, name, MAX_NAME_LENGTH)) {
            if(i == COUNT_OF(list->file)) {
                list->file_count = i;
                response.has_next = true;
                MsgList_push_back(msg_list, response);
                i = 0;
            }

            if(test_rpc_system_storage_list_filter(&fileinfo, name, filter_max_size)) {
                list->file[i].type = file_info_is_dir(&fileinfo) ? PB_Storage_File_FileType_DIR :
                                                                   PB_Storage_File_FileType_FILE;
                list->file[i].size = fileinfo.size;
                list->file[i].data = NULL;
                /* memory free inside rpc_encode_and_send() -> pb_release() */
                list->file[i].name = name;

                if(append_md5 && !file_info_is_dir(&fileinfo)) {
                    furi_string_printf(md5_path, "%s/%s", path, name);

                    if(md5_string_calc_file(file, furi_string_get_cstr(md5_path), md5, NULL)) {
                        char* md5sum = list->file[i].md5sum;
                        size_t md5sum_size = sizeof(list->file[i].md5sum);
                        snprintf(md5sum, md5sum_size, "%s", furi_string_get_cstr(md5));
                    }
                }

                ++i;
            }
        } else {
            finish = true;
            free(name);
        }
    }

    list->file_count = i;
    response.has_next = false;
    MsgList_push_back(msg_list, response);

    furi_string_free(md5);
    furi_string_free(md5_path);
    storage_file_free(file);

    storage_dir_close(dir);
    storage_file_free(dir);

    furi_record_close(RECORD_STORAGE);
}

static void test_rpc_decode_and_compare(MsgList_t expected_msg_list, uint8_t session) {
    furi_check(!MsgList_empty_p(expected_msg_list));
    furi_check(session < TEST_RPC_SESSIONS);

    rpc_session[session].timeout = furi_get_tick() + MAX_RECEIVE_OUTPUT_TIMEOUT;
    pb_istream_t istream = {
        .callback = test_rpc_pb_stream_read,
        .state = &rpc_session[session],
        .errmsg = NULL,
        .bytes_left = 0x7FFFFFFF,
    };
    /* other fields explicitly initialized by 0 */
    PB_Main result = {.cb_content.funcs.decode = NULL};

    /* mlib adds msg_files into start of list, so reverse it */
    MsgList_reverse(expected_msg_list);
    for
        M_EACH(expected_msg, expected_msg_list, MsgList_t) {
            if(!pb_decode_ex(&istream, &PB_Main_msg, &result, PB_DECODE_DELIMITED)) {
                mu_fail(
                    "not all expected messages decoded (maybe increase MAX_RECEIVE_OUTPUT_TIMEOUT)");
                break;
            }

            test_rpc_compare_messages(&result, expected_msg);
            pb_release(&PB_Main_msg, &result);
        }

    rpc_session[session].timeout = furi_get_tick() + 50;
    if(pb_decode_ex(&istream, &PB_Main_msg, &result, PB_DECODE_DELIMITED)) {
        mu_fail("decoded more than expected");
    }
    MsgList_reverse(expected_msg_list);
}

static void test_rpc_free_msg_list(MsgList_t msg_list) {
    for
        M_EACH(it, msg_list, MsgList_t) {
            pb_release(&PB_Main_msg, it);
        }
    MsgList_clear(msg_list);
}

static void test_rpc_storage_list_run(
    const char* path,
    uint32_t command_id,
    bool md5,
    size_t filter_max_size) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_storage_list_request(&request, path, md5, command_id, filter_max_size);
    if(!strcmp(path, "/")) {
        test_rpc_storage_list_create_expected_list_root(expected_msg_list, command_id);
    } else {
        test_rpc_storage_list_create_expected_list(
            expected_msg_list, path, command_id, md5, filter_max_size);
    }
    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_list) {
    test_rpc_storage_list_run("/", ++command_id, false, 0);
    test_rpc_storage_list_run(EXT_PATH("nfc"), ++command_id, false, 0);
    test_rpc_storage_list_run(STORAGE_INT_PATH_PREFIX, ++command_id, false, 0);
    test_rpc_storage_list_run(STORAGE_EXT_PATH_PREFIX, ++command_id, false, 0);
    test_rpc_storage_list_run(EXT_PATH("infrared"), ++command_id, false, 0);
    test_rpc_storage_list_run(EXT_PATH("ibutton"), ++command_id, false, 0);
    test_rpc_storage_list_run(EXT_PATH("lfrfid"), ++command_id, false, 0);
    test_rpc_storage_list_run("error_path", ++command_id, false, 0);
}

MU_TEST(test_storage_list_md5) {
    test_rpc_storage_list_run("/", ++command_id, true, 0);
    test_rpc_storage_list_run(EXT_PATH("nfc"), ++command_id, true, 0);
    test_rpc_storage_list_run(STORAGE_INT_PATH_PREFIX, ++command_id, true, 0);
    test_rpc_storage_list_run(STORAGE_EXT_PATH_PREFIX, ++command_id, true, 0);
    test_rpc_storage_list_run(EXT_PATH("infrared"), ++command_id, true, 0);
    test_rpc_storage_list_run(EXT_PATH("ibutton"), ++command_id, true, 0);
    test_rpc_storage_list_run(EXT_PATH("lfrfid"), ++command_id, true, 0);
    test_rpc_storage_list_run("error_path", ++command_id, true, 0);
}

MU_TEST(test_storage_list_size) {
    test_rpc_storage_list_run(TEST_DIR_NAME, ++command_id, false, 0);
    test_rpc_storage_list_run(TEST_DIR_NAME, ++command_id, false, 1);
    test_rpc_storage_list_run(TEST_DIR_NAME, ++command_id, false, 1000);
    test_rpc_storage_list_run(TEST_DIR_NAME, ++command_id, false, 2500);
}

static void
    test_rpc_add_empty_to_list(MsgList_t msg_list, PB_CommandStatus status, uint32_t command_id) {
    PB_Main* response = MsgList_push_new(msg_list);
    response->command_id = command_id;
    response->command_status = status;
    response->cb_content.funcs.encode = NULL;
    response->has_next = false;
    response->which_content = PB_Main_empty_tag;
}

static void test_rpc_add_read_to_list_by_reading_real_file(
    MsgList_t msg_list,
    const char* path,
    uint32_t command_id) {
    furi_check(MsgList_empty_p(msg_list));
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(fs_api);

    bool result = false;

    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t size_left = storage_file_size(file);

        do {
            PB_Main* response = MsgList_push_new(msg_list);
            response->command_id = command_id;
            response->command_status = PB_CommandStatus_OK;
            response->has_next = false;
            response->which_content = PB_Main_storage_read_response_tag;
            response->content.storage_read_response.has_file = true;

            response->content.storage_read_response.file.data =
                malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(MIN(size_left, MAX_DATA_SIZE)));
            uint8_t* buffer = response->content.storage_read_response.file.data->bytes;
            uint16_t* read_size_msg = &response->content.storage_read_response.file.data->size;
            size_t read_size = MIN(size_left, MAX_DATA_SIZE);
            *read_size_msg = storage_file_read(file, buffer, read_size);
            size_left -= read_size;
            result = (*read_size_msg == read_size);

            if(result) {
                response->has_next = (size_left > 0);
            }
        } while((size_left != 0) && result);

        if(!result) {
            test_rpc_add_empty_to_list(
                msg_list, test_rpc_storage_get_file_error(file), command_id);
        }
    } else {
        test_rpc_add_empty_to_list(msg_list, test_rpc_storage_get_file_error(file), command_id);
    }

    storage_file_close(file);
    storage_file_free(file);

    furi_record_close(RECORD_STORAGE);
}

static void test_storage_read_run(const char* path, uint32_t command_id) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_add_read_to_list_by_reading_real_file(expected_msg_list, path, command_id);
    test_rpc_create_simple_message(&request, PB_Main_storage_read_request_tag, path, command_id);
    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

static bool test_is_exists(const char* path) {
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    FileInfo fileinfo;
    FS_Error result = storage_common_stat(fs_api, path, &fileinfo);
    furi_check((result == FSE_OK) || (result == FSE_NOT_EXIST));
    furi_record_close(RECORD_STORAGE);
    return result == FSE_OK;
}

static void test_create_dir(const char* path) {
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    FS_Error error = storage_common_mkdir(fs_api, path);
    (void)error;
    furi_check((error == FSE_OK) || (error == FSE_EXIST));
    furi_record_close(RECORD_STORAGE);
    furi_check(test_is_exists(path));
}

static void test_create_file(const char* path, size_t size) {
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(fs_api);

    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint8_t buf[128] = {0};
        for(size_t i = 0; i < sizeof(buf); ++i) {
            buf[i] = '0' + (i % 10);
        }
        while(size) {
            size_t written = storage_file_write(file, buf, MIN(size, sizeof(buf)));
            furi_check(written);
            size -= written;
        }
    }

    storage_file_close(file);
    storage_file_free(file);

    furi_record_close(RECORD_STORAGE);
    furi_check(test_is_exists(path));
}

static void test_rpc_storage_info_run(const char* path, uint32_t command_id) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_simple_message(&request, PB_Main_storage_info_request_tag, path, command_id);

    PB_Main* response = MsgList_push_new(expected_msg_list);
    response->command_id = command_id;

    Storage* fs_api = furi_record_open(RECORD_STORAGE);

    FS_Error error = storage_common_fs_info(
        fs_api,
        path,
        &response->content.storage_info_response.total_space,
        &response->content.storage_info_response.free_space);

    response->command_status = rpc_system_storage_get_error(error);
    if(error == FSE_OK) {
        response->which_content = PB_Main_storage_info_response_tag;
    } else {
        response->which_content = PB_Main_empty_tag;
    }

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

static void test_rpc_storage_stat_run(const char* path, uint32_t command_id) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_simple_message(&request, PB_Main_storage_stat_request_tag, path, command_id);

    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    FileInfo fileinfo;
    FS_Error error = storage_common_stat(fs_api, path, &fileinfo);
    furi_record_close(RECORD_STORAGE);

    PB_Main* response = MsgList_push_new(expected_msg_list);
    response->command_id = command_id;
    response->command_status = rpc_system_storage_get_error(error);
    response->has_next = false;
    response->which_content = PB_Main_empty_tag;

    if(error == FSE_OK) {
        response->which_content = PB_Main_storage_stat_response_tag;
        response->content.storage_stat_response.has_file = true;
        response->content.storage_stat_response.file.type = file_info_is_dir(&fileinfo) ?
                                                                PB_Storage_File_FileType_DIR :
                                                                PB_Storage_File_FileType_FILE;
        response->content.storage_stat_response.file.size = fileinfo.size;
    }

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_info) {
    test_rpc_storage_info_run(STORAGE_ANY_PATH_PREFIX, ++command_id);
    test_rpc_storage_info_run(STORAGE_INT_PATH_PREFIX, ++command_id);
    test_rpc_storage_info_run(STORAGE_EXT_PATH_PREFIX, ++command_id);
}

#define TEST_DIR_STAT_NAME TEST_DIR "stat_dir"
#define TEST_DIR_STAT      TEST_DIR_STAT_NAME "/"
MU_TEST(test_storage_stat) {
    test_create_dir(TEST_DIR_STAT_NAME);
    test_create_file(TEST_DIR_STAT "empty.txt", 0);
    test_create_file(TEST_DIR_STAT "l33t.txt", 1337);

    test_rpc_storage_stat_run("/", ++command_id);
    test_rpc_storage_stat_run(STORAGE_INT_PATH_PREFIX, ++command_id);
    test_rpc_storage_stat_run(STORAGE_EXT_PATH_PREFIX, ++command_id);

    test_rpc_storage_stat_run(TEST_DIR_STAT "empty.txt", ++command_id);
    test_rpc_storage_stat_run(TEST_DIR_STAT "l33t.txt", ++command_id);
    test_rpc_storage_stat_run(TEST_DIR_STAT "missing", ++command_id);
    test_rpc_storage_stat_run(TEST_DIR_STAT_NAME, ++command_id);

    test_rpc_storage_stat_run(TEST_DIR_STAT, ++command_id);
}

MU_TEST(test_storage_read) {
    test_create_file(TEST_DIR "empty.txt", 0);
    test_create_file(TEST_DIR "file1.txt", 1);
    test_create_file(TEST_DIR "file2.txt", MAX_DATA_SIZE);
    test_create_file(TEST_DIR "file3.txt", MAX_DATA_SIZE + 1);
    test_create_file(TEST_DIR "file4.txt", (MAX_DATA_SIZE * 2) + 1);

    test_storage_read_run(TEST_DIR "empty.txt", ++command_id);
    test_storage_read_run(TEST_DIR "file1.txt", ++command_id);
    test_storage_read_run(TEST_DIR "file2.txt", ++command_id);
    test_storage_read_run(TEST_DIR "file3.txt", ++command_id);
    test_storage_read_run(TEST_DIR "file4.txt", ++command_id);
}

static void test_storage_write_run(
    const char* path,
    size_t write_size,
    size_t write_count,
    uint32_t command_id,
    PB_CommandStatus status) {
    MsgList_t input_msg_list;
    MsgList_init(input_msg_list);
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    uint8_t* buf = malloc(write_size);
    for(size_t i = 0; i < write_size; ++i) {
        buf[i] = '0' + (i % 10);
    }

    test_rpc_add_read_or_write_to_list(
        input_msg_list, WRITE_REQUEST, path, buf, write_size, write_count, command_id);
    test_rpc_add_empty_to_list(expected_msg_list, status, command_id);
    test_rpc_encode_and_feed(input_msg_list, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(input_msg_list);
    test_rpc_free_msg_list(expected_msg_list);

    free(buf);
}

static void test_storage_write_read_run(
    const char* path,
    const uint8_t* pattern,
    size_t pattern_size,
    size_t pattern_repeats,
    uint32_t* command_id) {
    MsgList_t input_msg_list;
    MsgList_init(input_msg_list);
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_add_read_or_write_to_list(
        input_msg_list, WRITE_REQUEST, path, pattern, pattern_size, pattern_repeats, ++*command_id);
    test_rpc_add_empty_to_list(expected_msg_list, PB_CommandStatus_OK, *command_id);

    test_rpc_create_simple_message(
        MsgList_push_raw(input_msg_list), PB_Main_storage_read_request_tag, path, ++*command_id);
    test_rpc_add_read_or_write_to_list(
        expected_msg_list,
        READ_RESPONSE,
        path,
        pattern,
        pattern_size,
        pattern_repeats,
        *command_id);

    test_rpc_print_message_list(input_msg_list);
    test_rpc_print_message_list(expected_msg_list);

    test_rpc_encode_and_feed(input_msg_list, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(input_msg_list);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_write_read) {
    uint8_t pattern1[] = "abcdefgh";
    test_storage_write_read_run(TEST_DIR "test1.txt", pattern1, sizeof(pattern1), 1, &command_id);
    test_storage_write_read_run(TEST_DIR "test2.txt", pattern1, 1, 1, &command_id);
    test_storage_write_read_run(TEST_DIR "test3.txt", pattern1, 0, 1, &command_id);
}

MU_TEST(test_storage_write) {
    test_storage_write_run(
        TEST_DIR "afaefo/aefaef/aef/aef/test1.txt",
        1,
        1,
        ++command_id,
        PB_CommandStatus_ERROR_STORAGE_NOT_EXIST);
    test_storage_write_run(TEST_DIR "test1.txt", 100, 1, ++command_id, PB_CommandStatus_OK);
    test_storage_write_run(TEST_DIR "test2.txt", 100, 3, ++command_id, PB_CommandStatus_OK);
    test_storage_write_run(TEST_DIR "test1.txt", 100, 3, ++command_id, PB_CommandStatus_OK);
    test_storage_write_run(TEST_DIR "test2.txt", 100, 3, ++command_id, PB_CommandStatus_OK);
    test_storage_write_run(
        TEST_DIR "afaefo/aefaef/aef/aef/test1.txt",
        1,
        1,
        ++command_id,
        PB_CommandStatus_ERROR_STORAGE_NOT_EXIST);
    test_storage_write_run(TEST_DIR "test2.txt", 1, 50, ++command_id, PB_CommandStatus_OK);
    test_storage_write_run(TEST_DIR "test2.txt", 512, 3, ++command_id, PB_CommandStatus_OK);
}

MU_TEST(test_storage_interrupt_continuous_same_system) {
    MsgList_t input_msg_list;
    MsgList_init(input_msg_list);
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    uint8_t pattern[16] = {0};

    test_rpc_add_read_or_write_to_list(
        input_msg_list,
        WRITE_REQUEST,
        TEST_DIR "test1.txt",
        pattern,
        sizeof(pattern),
        3,
        command_id);

    /* replace last packet (has_next == false) with another command */
    PB_Main message_to_remove;
    MsgList_pop_back(&message_to_remove, input_msg_list);
    pb_release(&PB_Main_msg, &message_to_remove);
    test_rpc_create_simple_message(
        MsgList_push_new(input_msg_list),
        PB_Main_storage_mkdir_request_tag,
        TEST_DIR "dir1",
        command_id + 1);
    test_rpc_add_read_or_write_to_list(
        input_msg_list,
        WRITE_REQUEST,
        TEST_DIR "test2.txt",
        pattern,
        sizeof(pattern),
        3,
        command_id);

    test_rpc_add_empty_to_list(
        expected_msg_list, PB_CommandStatus_ERROR_CONTINUOUS_COMMAND_INTERRUPTED, command_id);
    test_rpc_add_empty_to_list(expected_msg_list, PB_CommandStatus_OK, command_id + 1);

    test_rpc_encode_and_feed(input_msg_list, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(input_msg_list);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_interrupt_continuous_another_system) {
    MsgList_t input_msg_list;
    MsgList_init(input_msg_list);
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    uint8_t pattern[16] = {0};

    test_rpc_add_read_or_write_to_list(
        input_msg_list,
        WRITE_REQUEST,
        TEST_DIR "test1.txt",
        pattern,
        sizeof(pattern),
        3,
        command_id);

    PB_Main message = {
        .command_id = command_id + 1,
        .command_status = PB_CommandStatus_OK,
        .cb_content.funcs.encode = NULL,
        .has_next = false,
        .which_content = PB_Main_system_ping_request_tag,
    };

    MsgList_it_t it;
    MsgList_it(it, input_msg_list);
    MsgList_next(it);
    MsgList_insert(input_msg_list, it, message);

    test_rpc_add_read_or_write_to_list(
        input_msg_list,
        WRITE_REQUEST,
        TEST_DIR "test2.txt",
        pattern,
        sizeof(pattern),
        3,
        command_id + 2);

    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, command_id + 1);
    test_rpc_add_empty_to_list(expected_msg_list, PB_CommandStatus_OK, command_id);
    test_rpc_add_empty_to_list(expected_msg_list, PB_CommandStatus_OK, command_id + 2);

    test_rpc_encode_and_feed(input_msg_list, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(input_msg_list);
    test_rpc_free_msg_list(expected_msg_list);
}

static void test_storage_delete_run(
    const char* path,
    size_t command_id,
    PB_CommandStatus status,
    bool recursive) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_simple_message(&request, PB_Main_storage_delete_request_tag, path, command_id);
    request.content.storage_delete_request.recursive = recursive;
    test_rpc_add_empty_to_list(expected_msg_list, status, command_id);

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

#define TEST_DIR_RMRF_NAME TEST_DIR "rmrf_test"
#define TEST_DIR_RMRF      TEST_DIR_RMRF_NAME "/"
MU_TEST(test_storage_delete_recursive) {
    test_create_dir(TEST_DIR_RMRF_NAME);

    test_create_dir(TEST_DIR_RMRF "dir1");
    test_create_file(TEST_DIR_RMRF "dir1/file1", 1);

    test_create_dir(TEST_DIR_RMRF "dir1/dir1");
    test_create_dir(TEST_DIR_RMRF "dir1/dir2");
    test_create_file(TEST_DIR_RMRF "dir1/dir2/file1", 1);
    test_create_file(TEST_DIR_RMRF "dir1/dir2/file2", 1);
    test_create_dir(TEST_DIR_RMRF "dir1/dir3");
    test_create_dir(TEST_DIR_RMRF "dir1/dir3/dir1");
    test_create_dir(TEST_DIR_RMRF "dir1/dir3/dir1/dir1");
    test_create_dir(TEST_DIR_RMRF "dir1/dir3/dir1/dir1/dir1");
    test_create_dir(TEST_DIR_RMRF "dir1/dir3/dir1/dir1/dir1/dir1");

    test_create_dir(TEST_DIR_RMRF "dir2");
    test_create_dir(TEST_DIR_RMRF "dir2/dir1");
    test_create_dir(TEST_DIR_RMRF "dir2/dir2");
    test_create_file(TEST_DIR_RMRF "dir2/dir2/file1", 1);

    test_create_dir(TEST_DIR_RMRF "dir2/dir2/dir1");
    test_create_dir(TEST_DIR_RMRF "dir2/dir2/dir1/dir1");
    test_create_dir(TEST_DIR_RMRF "dir2/dir2/dir1/dir1/dir1");
    test_create_file(TEST_DIR_RMRF "dir2/dir2/dir1/dir1/dir1/file1", 1);

    test_storage_delete_run(
        TEST_DIR_RMRF_NAME, ++command_id, PB_CommandStatus_ERROR_STORAGE_DIR_NOT_EMPTY, false);
    mu_check(test_is_exists(TEST_DIR_RMRF_NAME));
    test_storage_delete_run(TEST_DIR_RMRF_NAME, ++command_id, PB_CommandStatus_OK, true);
    mu_check(!test_is_exists(TEST_DIR_RMRF_NAME));
    test_storage_delete_run(TEST_DIR_RMRF_NAME, ++command_id, PB_CommandStatus_OK, false);
    mu_check(!test_is_exists(TEST_DIR_RMRF_NAME));

    test_create_dir(TEST_DIR_RMRF_NAME);
    test_storage_delete_run(TEST_DIR_RMRF_NAME, ++command_id, PB_CommandStatus_OK, true);
    mu_check(!test_is_exists(TEST_DIR_RMRF_NAME));

    test_create_dir(TEST_DIR "file1");
    test_storage_delete_run(TEST_DIR "file1", ++command_id, PB_CommandStatus_OK, true);
    mu_check(!test_is_exists(TEST_DIR "file1"));
}

MU_TEST(test_storage_delete) {
    test_storage_delete_run(NULL, ++command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS, false);

    furi_check(!test_is_exists(TEST_DIR "empty.txt"));
    test_storage_delete_run(TEST_DIR "empty.txt", ++command_id, PB_CommandStatus_OK, false);
    mu_check(!test_is_exists(TEST_DIR "empty.txt"));

    test_create_file(TEST_DIR "empty.txt", 0);
    test_storage_delete_run(TEST_DIR "empty.txt", ++command_id, PB_CommandStatus_OK, false);
    mu_check(!test_is_exists(TEST_DIR "empty.txt"));

    furi_check(!test_is_exists(TEST_DIR "dir1"));
    test_create_dir(TEST_DIR "dir1");
    test_storage_delete_run(TEST_DIR "dir1", ++command_id, PB_CommandStatus_OK, false);
    mu_check(!test_is_exists(TEST_DIR "dir1"));

    test_storage_delete_run(TEST_DIR "dir1", ++command_id, PB_CommandStatus_OK, false);
    mu_check(!test_is_exists(TEST_DIR "dir1"));
}

static void test_storage_mkdir_run(const char* path, size_t command_id, PB_CommandStatus status) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_simple_message(&request, PB_Main_storage_mkdir_request_tag, path, command_id);
    test_rpc_add_empty_to_list(expected_msg_list, status, command_id);

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_mkdir) {
    furi_check(!test_is_exists(TEST_DIR "dir1"));
    test_storage_mkdir_run(TEST_DIR "dir1", ++command_id, PB_CommandStatus_OK);
    mu_check(test_is_exists(TEST_DIR "dir1"));

    test_storage_mkdir_run(TEST_DIR "dir1", ++command_id, PB_CommandStatus_ERROR_STORAGE_EXIST);
    mu_check(test_is_exists(TEST_DIR "dir1"));

    furi_check(!test_is_exists(TEST_DIR "dir2"));
    test_create_dir(TEST_DIR "dir2");
    test_storage_mkdir_run(TEST_DIR "dir2", ++command_id, PB_CommandStatus_ERROR_STORAGE_EXIST);
    mu_check(test_is_exists(TEST_DIR "dir2"));
}

static void test_storage_calculate_md5sum(const char* path, char* md5sum, size_t md5sum_size) {
    Storage* api = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(api);
    FuriString* md5 = furi_string_alloc();

    if(md5_string_calc_file(file, path, md5, NULL)) {
        snprintf(md5sum, md5sum_size, "%s", furi_string_get_cstr(md5));
    } else {
        furi_check(0);
    }

    furi_string_free(md5);
    storage_file_close(file);
    storage_file_free(file);

    furi_record_close(RECORD_STORAGE);
}

static void test_storage_md5sum_run(
    const char* path,
    uint32_t command_id,
    const char* md5sum,
    PB_CommandStatus status) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_create_simple_message(&request, PB_Main_storage_md5sum_request_tag, path, command_id);
    if(status == PB_CommandStatus_OK) {
        PB_Main* response = MsgList_push_new(expected_msg_list);
        test_rpc_create_simple_message(
            response, PB_Main_storage_md5sum_response_tag, md5sum, command_id);
        response->command_status = status;
    } else {
        test_rpc_add_empty_to_list(expected_msg_list, status, command_id);
    }

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_md5sum) {
    char md5sum1[MD5SUM_SIZE * 2 + 1] = {0};
    char md5sum2[MD5SUM_SIZE * 2 + 1] = {0};
    char md5sum3[MD5SUM_SIZE * 2 + 1] = {0};

    test_storage_md5sum_run(
        TEST_DIR "test1.txt", ++command_id, "", PB_CommandStatus_ERROR_STORAGE_NOT_EXIST);

    test_create_file(TEST_DIR "file1.txt", 0);
    test_create_file(TEST_DIR "file2.txt", 1);
    test_create_file(TEST_DIR "file3.txt", 512);
    test_storage_calculate_md5sum(TEST_DIR "file1.txt", md5sum1, MD5SUM_SIZE * 2 + 1);
    test_storage_calculate_md5sum(TEST_DIR "file2.txt", md5sum2, MD5SUM_SIZE * 2 + 1);
    test_storage_calculate_md5sum(TEST_DIR "file3.txt", md5sum3, MD5SUM_SIZE * 2 + 1);

    test_storage_md5sum_run(TEST_DIR "file1.txt", ++command_id, md5sum1, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file1.txt", ++command_id, md5sum1, PB_CommandStatus_OK);

    test_storage_md5sum_run(TEST_DIR "file2.txt", ++command_id, md5sum2, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file2.txt", ++command_id, md5sum2, PB_CommandStatus_OK);

    test_storage_md5sum_run(TEST_DIR "file3.txt", ++command_id, md5sum3, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file3.txt", ++command_id, md5sum3, PB_CommandStatus_OK);

    test_storage_md5sum_run(TEST_DIR "file2.txt", ++command_id, md5sum2, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file3.txt", ++command_id, md5sum3, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file1.txt", ++command_id, md5sum1, PB_CommandStatus_OK);
    test_storage_md5sum_run(TEST_DIR "file2.txt", ++command_id, md5sum2, PB_CommandStatus_OK);
}

static void test_rpc_storage_rename_run(
    const char* old_path,
    const char* new_path,
    uint32_t command_id,
    PB_CommandStatus status) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    char* str_old_path = strdup(old_path);
    char* str_new_path = strdup(new_path);

    request.command_id = command_id;
    request.command_status = PB_CommandStatus_OK;
    request.cb_content.funcs.encode = NULL;
    request.which_content = PB_Main_storage_rename_request_tag;
    request.has_next = false;
    request.content.storage_rename_request.old_path = str_old_path;
    request.content.storage_rename_request.new_path = str_new_path;

    test_rpc_add_empty_to_list(expected_msg_list, status, command_id);

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_storage_rename) {
    test_rpc_storage_rename_run("", "", ++command_id, PB_CommandStatus_ERROR_STORAGE_INVALID_NAME);

    furi_check(!test_is_exists(TEST_DIR "empty.txt"));
    test_create_file(TEST_DIR "empty.txt", 0);
    test_rpc_storage_rename_run(
        TEST_DIR "empty.txt", TEST_DIR "empty2.txt", ++command_id, PB_CommandStatus_OK);
    mu_check(!test_is_exists(TEST_DIR "empty.txt"));
    mu_check(test_is_exists(TEST_DIR "empty2.txt"));

    furi_check(!test_is_exists(TEST_DIR "dir1"));
    test_create_dir(TEST_DIR "dir1");
    test_rpc_storage_rename_run(
        TEST_DIR "dir1", TEST_DIR "dir2", ++command_id, PB_CommandStatus_OK);
    mu_check(!test_is_exists(TEST_DIR "dir1"));
    mu_check(test_is_exists(TEST_DIR "dir2"));
}

MU_TEST(test_ping) {
    MsgList_t input_msg_list;
    MsgList_init(input_msg_list);
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 0);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 1);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 0);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 500);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, (uint32_t)-1);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 700);
    test_rpc_add_ping_to_list(input_msg_list, PING_REQUEST, 1);

    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 0);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 1);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 0);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 500);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, (uint32_t)-1);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 700);
    test_rpc_add_ping_to_list(expected_msg_list, PING_RESPONSE, 1);

    test_rpc_encode_and_feed(input_msg_list, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(input_msg_list);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_system_protobuf_version) {
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    PB_Main request;
    request.command_id = ++command_id;
    request.command_status = PB_CommandStatus_OK;
    request.cb_content.funcs.decode = NULL;
    request.has_next = false;
    request.which_content = PB_Main_system_protobuf_version_request_tag;

    PB_Main* response = MsgList_push_new(expected_msg_list);
    response->command_id = command_id;
    response->command_status = PB_CommandStatus_OK;
    response->cb_content.funcs.encode = NULL;
    response->has_next = false;
    response->which_content = PB_Main_system_protobuf_version_response_tag;
    response->content.system_protobuf_version_response.major = PROTOBUF_MAJOR_VERSION;
    response->content.system_protobuf_version_response.minor = PROTOBUF_MINOR_VERSION;

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST_SUITE(test_rpc_system) {
    MU_SUITE_CONFIGURE(&test_rpc_setup, &test_rpc_teardown);

    MU_RUN_TEST(test_ping);
    MU_RUN_TEST(test_system_protobuf_version);
}

MU_TEST_SUITE(test_rpc_storage) {
    MU_SUITE_CONFIGURE(&test_rpc_storage_setup, &test_rpc_storage_teardown);

    MU_RUN_TEST(test_storage_info);
    MU_RUN_TEST(test_storage_stat);
    MU_RUN_TEST(test_storage_list);
    MU_RUN_TEST(test_storage_list_md5);
    MU_RUN_TEST(test_storage_list_size);
    MU_RUN_TEST(test_storage_read);
    MU_RUN_TEST(test_storage_write_read);
    MU_RUN_TEST(test_storage_write);
    MU_RUN_TEST(test_storage_delete);
    MU_RUN_TEST(test_storage_delete_recursive);
    MU_RUN_TEST(test_storage_mkdir);
    MU_RUN_TEST(test_storage_md5sum);
    MU_RUN_TEST(test_storage_rename);

    DISABLE_TEST(MU_RUN_TEST(test_storage_interrupt_continuous_same_system););
    MU_RUN_TEST(test_storage_interrupt_continuous_another_system);
}

static void test_app_create_request(
    PB_Main* request,
    const char* app_name,
    const char* app_args,
    uint32_t command_id) {
    request->command_id = command_id;
    request->command_status = PB_CommandStatus_OK;
    request->cb_content.funcs.encode = NULL;
    request->which_content = PB_Main_app_start_request_tag;
    request->has_next = false;

    if(app_name) {
        char* msg_app_name = strdup(app_name);
        request->content.app_start_request.name = msg_app_name;
    } else {
        request->content.app_start_request.name = NULL;
    }

    if(app_args) {
        char* msg_app_args = strdup(app_args);
        request->content.app_start_request.args = msg_app_args;
    } else {
        request->content.app_start_request.args = NULL;
    }
}

static void test_app_start_run(
    const char* app_name,
    const char* app_args,
    PB_CommandStatus status,
    uint32_t command_id) {
    PB_Main request;
    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);

    test_app_create_request(&request, app_name, app_args, command_id);
    test_rpc_add_empty_to_list(expected_msg_list, status, command_id);

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

static void test_app_get_status_lock_run(bool locked_expected, uint32_t command_id) {
    PB_Main request = {
        .command_id = command_id,
        .command_status = PB_CommandStatus_OK,
        .which_content = PB_Main_app_lock_status_request_tag,
        .has_next = false,
    };

    MsgList_t expected_msg_list;
    MsgList_init(expected_msg_list);
    PB_Main* response = MsgList_push_new(expected_msg_list);
    response->command_id = command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_app_lock_status_response_tag;
    response->has_next = false;
    response->content.app_lock_status_response.locked = locked_expected;
    strlcpy(
        response->content.app_lock_status_response.active_application,
        locked_expected ? "Delay Test" : "",
        sizeof(response->content.app_lock_status_response.active_application));

    test_rpc_encode_and_feed_one(&request, 0);
    test_rpc_decode_and_compare(expected_msg_list, 0);

    pb_release(&PB_Main_msg, &request);
    test_rpc_free_msg_list(expected_msg_list);
}

MU_TEST(test_app_start_and_lock_status) {
    test_app_get_status_lock_run(false, ++command_id);
    test_app_start_run(
        NULL, EXT_PATH("file"), PB_CommandStatus_ERROR_INVALID_PARAMETERS, ++command_id);
    test_app_start_run(NULL, NULL, PB_CommandStatus_ERROR_INVALID_PARAMETERS, ++command_id);
    test_app_get_status_lock_run(false, ++command_id);
    test_app_start_run(
        "skynet_destroy_world_app", NULL, PB_CommandStatus_ERROR_INVALID_PARAMETERS, ++command_id);
    test_app_get_status_lock_run(false, ++command_id);

    test_app_start_run("Delay Test", "0", PB_CommandStatus_OK, ++command_id);
    furi_delay_ms(100);
    test_app_get_status_lock_run(false, ++command_id);

    test_app_start_run("Delay Test", "200", PB_CommandStatus_OK, ++command_id);
    test_app_get_status_lock_run(true, ++command_id);
    furi_delay_ms(100);
    test_app_get_status_lock_run(true, ++command_id);
    test_app_start_run("Delay Test", "0", PB_CommandStatus_ERROR_APP_SYSTEM_LOCKED, ++command_id);
    furi_delay_ms(200);
    test_app_get_status_lock_run(false, ++command_id);

    test_app_start_run("Delay Test", "500", PB_CommandStatus_OK, ++command_id);
    furi_delay_ms(100);
    test_app_get_status_lock_run(true, ++command_id);
    test_app_start_run("Infrared", "0", PB_CommandStatus_ERROR_APP_SYSTEM_LOCKED, ++command_id);
    furi_delay_ms(100);
    test_app_get_status_lock_run(true, ++command_id);
    test_app_start_run(
        "2_girls_1_app", "0", PB_CommandStatus_ERROR_INVALID_PARAMETERS, ++command_id);
    furi_delay_ms(100);
    test_app_get_status_lock_run(true, ++command_id);
    furi_delay_ms(500);
    test_app_get_status_lock_run(false, ++command_id);
}

MU_TEST_SUITE(test_rpc_app) {
    MU_SUITE_CONFIGURE(&test_rpc_setup, &test_rpc_teardown);

    DISABLE_TEST(MU_RUN_TEST(test_app_start_and_lock_status););
}

static void
    test_send_rubbish(RpcSession* session, const char* pattern, size_t pattern_size, size_t size) {
    UNUSED(session);
    uint8_t* buf = malloc(size);
    for(size_t i = 0; i < size; ++i) {
        buf[i] = pattern[i % pattern_size];
    }

    size_t bytes_sent = rpc_session_feed(rpc_session[0].session, buf, size, 1000);
    furi_check(bytes_sent == size);
    free(buf);
}

static void test_rpc_feed_rubbish_run(
    MsgList_t input_before,
    MsgList_t input_after,
    MsgList_t expected,
    const char* pattern,
    size_t pattern_size,
    size_t size) {
    test_rpc_setup();

    test_rpc_add_empty_to_list(expected, PB_CommandStatus_ERROR_DECODE, 0);

    furi_check(api_lock_is_locked(rpc_session[0].session_close_lock));
    test_rpc_encode_and_feed(input_before, 0);
    test_send_rubbish(rpc_session[0].session, pattern, pattern_size, size);
    test_rpc_encode_and_feed(input_after, 0);

    test_rpc_decode_and_compare(expected, 0);

    test_rpc_teardown();
}

#define RUN_TEST_RPC_FEED_RUBBISH(ib, ia, e, b, c) \
    test_rpc_feed_rubbish_run(ib, ia, e, b, sizeof(b), c)

#define INIT_LISTS()            \
    MsgList_init(input_before); \
    MsgList_init(input_after);  \
    MsgList_init(expected);

#define FREE_LISTS()                      \
    test_rpc_free_msg_list(input_before); \
    test_rpc_free_msg_list(input_after);  \
    test_rpc_free_msg_list(expected);

MU_TEST(test_rpc_feed_rubbish) {
    MsgList_t input_before;
    MsgList_t input_after;
    MsgList_t expected;

    INIT_LISTS();
    // input is empty
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x12\x30rubbi\x42sh", 50);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x2\x2\x2\x5\x99\x1", 30);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_after, PING_REQUEST, ++command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x12\x30rubbi\x42sh", 50);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    test_rpc_add_ping_to_list(input_after, PING_REQUEST, command_id);
    test_rpc_add_ping_to_list(input_after, PING_REQUEST, command_id);
    test_rpc_add_ping_to_list(input_after, PING_REQUEST, command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x99\x2\x2\x5\x99\x1", 300);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_after, PING_REQUEST, ++command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x1\x99\x2\x5\x99\x1", 300);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_before, PING_REQUEST, ++command_id);
    test_rpc_add_ping_to_list(expected, PING_RESPONSE, command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x2\x2\x2\x5\x99\x1", 30);
    FREE_LISTS();

    INIT_LISTS();
    test_rpc_add_ping_to_list(input_before, PING_RESPONSE, ++command_id);
    test_rpc_add_empty_to_list(expected, PB_CommandStatus_ERROR_NOT_IMPLEMENTED, command_id);
    test_rpc_add_ping_to_list(input_before, PING_RESPONSE, ++command_id);
    test_rpc_add_empty_to_list(expected, PB_CommandStatus_ERROR_NOT_IMPLEMENTED, command_id);
    RUN_TEST_RPC_FEED_RUBBISH(input_before, input_after, expected, "\x12\x30rubbi\x42sh", 50);
    FREE_LISTS();
}

MU_TEST(test_rpc_multisession_ping) {
    MsgList_t input_0;
    MsgList_init(input_0);
    MsgList_t input_1;
    MsgList_init(input_1);
    MsgList_t expected_0;
    MsgList_init(expected_0);
    MsgList_t expected_1;
    MsgList_init(expected_1);

    test_rpc_setup();

    test_rpc_setup_second_session();
    test_rpc_teardown_second_session();

    test_rpc_setup_second_session();

    test_rpc_add_ping_to_list(input_0, PING_REQUEST, 0);
    test_rpc_add_ping_to_list(input_1, PING_REQUEST, 1);
    test_rpc_add_ping_to_list(expected_0, PING_RESPONSE, 0);
    test_rpc_add_ping_to_list(expected_1, PING_RESPONSE, 1);

    test_rpc_encode_and_feed(input_0, 0);
    test_rpc_encode_and_feed(input_1, 1);
    test_rpc_decode_and_compare(expected_0, 0);
    test_rpc_decode_and_compare(expected_1, 1);

    test_rpc_free_msg_list(input_0);
    test_rpc_free_msg_list(input_1);
    test_rpc_free_msg_list(expected_0);
    test_rpc_free_msg_list(expected_1);

    test_rpc_teardown_second_session();
    test_rpc_teardown();
}

MU_TEST(test_rpc_multisession_storage) {
    MsgList_t input_0;
    MsgList_init(input_0);
    MsgList_t input_1;
    MsgList_init(input_1);
    MsgList_t expected_0;
    MsgList_init(expected_0);
    MsgList_t expected_1;
    MsgList_init(expected_1);

    test_rpc_storage_setup();
    test_rpc_setup_second_session();

    uint8_t pattern[16] = "0123456789abcdef";

    test_rpc_add_read_or_write_to_list(
        input_0, WRITE_REQUEST, TEST_DIR "file0.txt", pattern, sizeof(pattern), 1, ++command_id);
    test_rpc_add_empty_to_list(expected_0, PB_CommandStatus_OK, command_id);

    test_rpc_add_read_or_write_to_list(
        input_1, WRITE_REQUEST, TEST_DIR "file1.txt", pattern, sizeof(pattern), 1, ++command_id);
    test_rpc_add_empty_to_list(expected_1, PB_CommandStatus_OK, command_id);

    test_rpc_create_simple_message(
        MsgList_push_raw(input_0),
        PB_Main_storage_read_request_tag,
        TEST_DIR "file0.txt",
        ++command_id);
    test_rpc_add_read_or_write_to_list(
        expected_0, READ_RESPONSE, TEST_DIR "file0.txt", pattern, sizeof(pattern), 1, command_id);

    test_rpc_create_simple_message(
        MsgList_push_raw(input_1),
        PB_Main_storage_read_request_tag,
        TEST_DIR "file1.txt",
        ++command_id);
    test_rpc_add_read_or_write_to_list(
        expected_1, READ_RESPONSE, TEST_DIR "file1.txt", pattern, sizeof(pattern), 1, command_id);

    test_rpc_print_message_list(input_0);
    test_rpc_print_message_list(input_1);
    test_rpc_print_message_list(expected_0);
    test_rpc_print_message_list(expected_1);

    test_rpc_encode_and_feed(input_0, 0);
    test_rpc_encode_and_feed(input_1, 1);

    test_rpc_decode_and_compare(expected_0, 0);
    test_rpc_decode_and_compare(expected_1, 1);

    test_rpc_free_msg_list(input_0);
    test_rpc_free_msg_list(input_1);
    test_rpc_free_msg_list(expected_0);
    test_rpc_free_msg_list(expected_1);

    test_rpc_teardown_second_session();
    test_rpc_storage_teardown();
}

typedef struct {
    bool called;
    bool approve;
    bool request_valid;
} TestRpcPairingApproval;

typedef struct {
    bool called;
} TestRpcStructuredApp;

typedef struct {
    bool called;
} TestRpcProfileApproval;

typedef struct {
    const char* const* values;
    size_t count;
} TestRpcStringList;

static bool
    test_rpc_encode_strings(pb_ostream_t* stream, const pb_field_t* field, void* const* argument) {
    const TestRpcStringList* strings = *argument;
    for(size_t index = 0u; index < strings->count; ++index) {
        if(!pb_encode_tag_for_field(stream, field) ||
           !pb_encode_string(
               stream, (const uint8_t*)strings->values[index], strlen(strings->values[index]))) {
            return false;
        }
    }
    return true;
}

static bool test_rpc_structured_app_command(const PoisonAppCommand* command, void* context) {
    TestRpcStructuredApp* app = context;
    app->called = command->protocol_version == POISON_APP_PROTOCOL_VERSION &&
                  strcmp(command->app_id, "org.poison.rpc-test") == 0 &&
                  strcmp(command->run_id, "run-1") == 0 &&
                  strcmp(command->command_id, "inspect") == 0 &&
                  strlen(command->payload_json) == 400u && !command->cancel;
    for(size_t index = 0u; app->called && index < 400u; index++) {
        app->called = command->payload_json[index] == 'a';
    }
    return app->called;
}

static bool test_rpc_profile_confirmation(
    void* context,
    const char* profile_id,
    const char* version,
    uint64_t capability_mask) {
    TestRpcProfileApproval* approval = context;
    approval->called = strcmp(profile_id, "rpc.field") == 0 && strcmp(version, "1.0.0") == 0 &&
                       capability_mask == POISON_CAPABILITY_STATUS;
    return approval->called;
}

static bool test_rpc_pairing_confirmation(
    void* context,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    uint32_t requested_role,
    uint32_t requested_capabilities) {
    TestRpcPairingApproval* approval = context;
    if(!approval) return false;

    approval->called = true;
    approval->request_valid =
        confirmation_code && strlen(confirmation_code) == 6u && fingerprint &&
        strlen(fingerprint) == 16u && client_name && strcmp(client_name, "field-console") == 0 &&
        requested_role == PoisonRoleOperator &&
        requested_capabilities == (POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                                   POISON_CAPABILITY_FILES | POISON_CAPABILITY_DESTRUCTIVE);
    return approval->approve;
}

static bool test_rpc_decode_one(PB_Main* message, uint8_t session) {
    rpc_session[session].timeout = furi_get_tick() + MAX_RECEIVE_OUTPUT_TIMEOUT;
    pb_istream_t stream = {
        .callback = test_rpc_pb_stream_read,
        .state = &rpc_session[session],
        .errmsg = NULL,
        .bytes_left = 0x7FFFFFFF,
    };
    memset(message, 0, sizeof(*message));
    return pb_decode_ex(&stream, &PB_Main_msg, message, PB_DECODE_DELIMITED);
}

static bool test_rpc_secure_round_trip(
    PoisonSession* client_session,
    uint64_t session_id,
    PB_Main* inner_request,
    PB_Main* inner_response) {
    uint8_t plaintext[768u];
    pb_ostream_t inner_output = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    if(!pb_encode(&inner_output, &PB_Main_msg, inner_request)) return false;

    PB_Main envelope_message = PB_Main_init_zero;
    envelope_message.command_id = inner_request->command_id;
    envelope_message.which_content = PB_Main_poison_session_envelope_tag;
    PB_Poison_SessionEnvelope* envelope = &envelope_message.content.poison_session_envelope;
    envelope->protocol_version = 2u;
    envelope->session_id = session_id;
    strcpy(envelope->channel, "rpc");
    envelope->payload.size = inner_output.bytes_written;
    envelope->authentication_tag.size = POISON_SESSION_AUTH_TAG_BYTES;
    if(poison_session_encrypt_tx(
           client_session,
           0u,
           envelope->channel,
           plaintext,
           inner_output.bytes_written,
           &envelope->sequence,
           envelope->payload.bytes,
           envelope->authentication_tag.bytes) != PoisonSessionResultOk) {
        return false;
    }
    test_rpc_encode_and_feed_one(&envelope_message, 0);

    PB_Main encrypted_response;
    if(!test_rpc_decode_one(&encrypted_response, 0) ||
       encrypted_response.which_content != PB_Main_poison_session_envelope_tag) {
        return false;
    }
    const PB_Poison_SessionEnvelope* response_envelope =
        &encrypted_response.content.poison_session_envelope;
    uint8_t response_plaintext[768u];
    const bool decrypted = poison_session_decrypt_rx(
                               client_session,
                               response_envelope->sequence,
                               response_envelope->acknowledgement,
                               response_envelope->channel,
                               response_envelope->payload.bytes,
                               response_envelope->payload.size,
                               response_envelope->authentication_tag.bytes,
                               response_plaintext) == PoisonSessionResultOk;
    if(decrypted) {
        *inner_response = (PB_Main)PB_Main_init_zero;
        pb_istream_t input =
            pb_istream_from_buffer(response_plaintext, response_envelope->payload.size);
        if(!pb_decode(&input, &PB_Main_msg, inner_response)) {
            pb_release(&PB_Main_msg, &encrypted_response);
            return false;
        }
    }
    pb_release(&PB_Main_msg, &encrypted_response);
    return decrypted;
}

MU_TEST(test_rpc_poison_pairing_and_encrypted_ping_use_real_dispatcher) {
    test_rpc_setup_owner(RpcOwnerUart);
    TestRpcPairingApproval approval = {
        .called = false,
        .approve = true,
        .request_valid = false,
    };
    rpc_session_set_pairing_confirmation_callback(
        rpc_session[0].session, test_rpc_pairing_confirmation, &approval);
    TestRpcProfileApproval profile_approval = {0};
    rpc_session_set_profile_confirmation_callback(
        rpc_session[0].session, test_rpc_profile_confirmation, &profile_approval);

    PB_Main pre_pair_plaintext_ping = PB_Main_init_zero;
    pre_pair_plaintext_ping.command_id = ++command_id;
    pre_pair_plaintext_ping.which_content = PB_Main_system_ping_request_tag;
    test_rpc_encode_and_feed_one(&pre_pair_plaintext_ping, 0u);
    PB_Main pre_pair_plaintext_response = PB_Main_init_zero;
    mu_check(test_rpc_decode_one(&pre_pair_plaintext_response, 0u));
    mu_check(pre_pair_plaintext_response.command_id == pre_pair_plaintext_ping.command_id);
    mu_check(
        pre_pair_plaintext_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    mu_check(pre_pair_plaintext_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &pre_pair_plaintext_response);

    uint8_t client_private[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t client_public[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t identity_private[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t identity_public[POISON_CRYPTO_P256_PUBLIC_BYTES];
    mu_check(
        poison_crypto_generate_p256_keypair(client_private, client_public) ==
        PoisonCryptoResultOk);
    mu_check(
        poison_crypto_generate_p256_keypair(identity_private, identity_public) ==
        PoisonCryptoResultOk);
    PB_Main hello = PB_Main_init_zero;
    hello.command_id = ++command_id;
    hello.which_content = PB_Main_poison_pairing_hello_tag;
    PB_Poison_PairingHello* pairing_hello = &hello.content.poison_pairing_hello;
    pairing_hello->protocol_version = 2u;
    pairing_hello->client_ephemeral_public_key.size = sizeof(client_public);
    memcpy(pairing_hello->client_ephemeral_public_key.bytes, client_public, sizeof(client_public));
    pairing_hello->client_identity_public_key.size = sizeof(identity_public);
    memcpy(
        pairing_hello->client_identity_public_key.bytes, identity_public, sizeof(identity_public));
    strcpy(pairing_hello->client_name, "field-console");
    pairing_hello->requested_role = PoisonRoleOperator;
    pairing_hello->requested_capabilities = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                                            POISON_CAPABILITY_FILES |
                                            POISON_CAPABILITY_DESTRUCTIVE;
    pairing_hello->client_nonce.size = 32u;
    for(size_t index = 0; index < pairing_hello->client_nonce.size; ++index)
        pairing_hello->client_nonce.bytes[index] = (uint8_t)(index + 1u);
    uint8_t client_nonce[32u];
    memcpy(client_nonce, pairing_hello->client_nonce.bytes, sizeof(client_nonce));
    test_rpc_encode_and_feed_one(&hello, 0);

    PB_Main challenge_message;
    mu_check(test_rpc_decode_one(&challenge_message, 0));
    mu_check(challenge_message.command_status == PB_CommandStatus_OK);
    mu_check(challenge_message.which_content == PB_Main_poison_pairing_challenge_tag);
    PB_Poison_PairingChallenge* challenge = &challenge_message.content.poison_pairing_challenge;

    uint8_t shared_secret[POISON_CRYPTO_SHARED_SECRET_BYTES];
    uint8_t salt[64u];
    uint8_t info[45u];
    uint8_t directional_keys[POISON_SESSION_KEY_BYTES * 2u];
    mu_check(
        poison_crypto_p256_shared_secret(
            client_private, challenge->device_ephemeral_public_key.bytes, shared_secret) ==
        PoisonCryptoResultOk);
    memcpy(salt, client_nonce, sizeof(client_nonce));
    memcpy(salt + sizeof(client_nonce), challenge->device_nonce.bytes, 32u);
    memcpy(info, "poison-rpc-v2", 13u);
    memcpy(info + 13u, challenge->transcript_digest.bytes, 32u);
    mu_check(
        poison_crypto_hkdf_sha256(
            salt,
            sizeof(salt),
            shared_secret,
            sizeof(shared_secret),
            info,
            sizeof(info),
            directional_keys,
            sizeof(directional_keys)) == PoisonCryptoResultOk);

    PB_Main confirm = PB_Main_init_zero;
    confirm.command_id = ++command_id;
    confirm.which_content = PB_Main_poison_pairing_confirm_tag;
    confirm.content.poison_pairing_confirm.transcript_digest.size = 32u;
    memcpy(
        confirm.content.poison_pairing_confirm.transcript_digest.bytes,
        challenge->transcript_digest.bytes,
        32u);
    strcpy(confirm.content.poison_pairing_confirm.confirmation_code, challenge->confirmation_code);
    confirm.content.poison_pairing_confirm.physical_confirmation = true;
    size_t identity_signature_size = 0u;
    mu_check(
        poison_crypto_sign_p256_sha256(
            identity_private,
            challenge->transcript_digest.bytes,
            confirm.content.poison_pairing_confirm.client_identity_signature.bytes,
            &identity_signature_size) == PoisonCryptoResultOk);
    confirm.content.poison_pairing_confirm.client_identity_signature.size =
        identity_signature_size;
    const uint64_t session_id = challenge->session_id;
    test_rpc_encode_and_feed_one(&confirm, 0);
    pb_release(&PB_Main_msg, &challenge_message);

    PB_Main confirm_response;
    mu_check(test_rpc_decode_one(&confirm_response, 0));
    mu_check(confirm_response.which_content == PB_Main_empty_tag);
    mu_check(confirm_response.command_status == PB_CommandStatus_OK);
    mu_check(approval.called);
    mu_check(approval.request_valid);
    pb_release(&PB_Main_msg, &confirm_response);
    const PoisonAuditChain* pairing_audit = poison_audit_get();
    mu_check(pairing_audit->event_count > 0u);
    const size_t pairing_event_index =
        (pairing_audit->write_index + POISON_AUDIT_RING_SIZE - 1u) % POISON_AUDIT_RING_SIZE;
    mu_check(
        strcmp(pairing_audit->events[pairing_event_index].action, "pairing.authenticate") == 0);
    mu_check(pairing_audit->events[pairing_event_index].decision == PoisonAuditDecisionAllowed);

    PoisonSession client_session;
    poison_session_init(&client_session);
    mu_check(poison_session_begin_negotiation(&client_session, 2u) == PoisonSessionResultOk);
    mu_check(
        poison_session_begin_confirmation(&client_session, session_id) == PoisonSessionResultOk);
    mu_check(
        poison_session_set_directional_keys(
            &client_session, directional_keys + POISON_SESSION_KEY_BYTES, directional_keys) ==
        PoisonSessionResultOk);
    mu_check(poison_session_confirm(&client_session, true) == PoisonSessionResultOk);
    mu_check(poison_session_activate(&client_session) == PoisonSessionResultOk);

    PB_Main unopened_ping = PB_Main_init_zero;
    unopened_ping.command_id = ++command_id;
    unopened_ping.which_content = PB_Main_system_ping_request_tag;
    unopened_ping.content.system_ping_request.data->size = 1u;
    unopened_ping.content.system_ping_request.data->bytes[0] = 0x42u;
    PB_Main unopened_ping_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &unopened_ping, &unopened_ping_response));
    mu_check(unopened_ping_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    mu_check(unopened_ping_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &unopened_ping_response);

    PB_Main channel_open = PB_Main_init_zero;
    channel_open.command_id = ++command_id;
    channel_open.which_content = PB_Main_poison_channel_open_tag;
    strcpy(channel_open.content.poison_channel_open.channel, "rpc");
    channel_open.content.poison_channel_open.initial_credits = 2u;
    channel_open.content.poison_channel_open.resume_sequence = 0u;
    PB_Main channel_open_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &channel_open, &channel_open_response));
    mu_check(channel_open_response.command_status == PB_CommandStatus_OK);
    mu_check(channel_open_response.which_content == PB_Main_poison_channel_opened_tag);
    mu_check(strcmp(channel_open_response.content.poison_channel_opened.channel, "rpc") == 0);
    mu_check(channel_open_response.content.poison_channel_opened.granted_credits == 2u);
    mu_check(channel_open_response.content.poison_channel_opened.next_sequence == 0u);
    pb_release(&PB_Main_msg, &channel_open_response);

    PB_Main credit_update = PB_Main_init_zero;
    credit_update.command_id = ++command_id;
    credit_update.which_content = PB_Main_poison_credit_update_tag;
    strcpy(credit_update.content.poison_credit_update.channel, "rpc");
    credit_update.content.poison_credit_update.credits = 2u;
    PB_Main credit_response;
    mu_check(
        test_rpc_secure_round_trip(&client_session, session_id, &credit_update, &credit_response));
    mu_check(credit_response.command_status == PB_CommandStatus_OK);
    mu_check(credit_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &credit_response);

    PB_Main frame_notification = PB_Main_init_zero;
    frame_notification.which_content = PB_Main_gui_screen_frame_tag;
    frame_notification.content.gui_screen_frame.data = malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(1024u));
    frame_notification.content.gui_screen_frame.data->size = 1024u;
    memset(frame_notification.content.gui_screen_frame.data->bytes, 0xA5, 1024u);
    rpc_send(rpc_session[0].session, &frame_notification);
    pb_release(&PB_Main_msg, &frame_notification);

    PB_Main encrypted_frame;
    mu_check(test_rpc_decode_one(&encrypted_frame, 0));
    mu_check(encrypted_frame.which_content == PB_Main_poison_session_envelope_tag);
    const PB_Poison_SessionEnvelope* frame_envelope =
        &encrypted_frame.content.poison_session_envelope;
    mu_check(frame_envelope->payload.size > 1024u);
    uint8_t frame_plaintext[1280u];
    mu_check(
        poison_session_decrypt_rx(
            &client_session,
            frame_envelope->sequence,
            frame_envelope->acknowledgement,
            frame_envelope->channel,
            frame_envelope->payload.bytes,
            frame_envelope->payload.size,
            frame_envelope->authentication_tag.bytes,
            frame_plaintext) == PoisonSessionResultOk);
    PB_Main decoded_frame = PB_Main_init_zero;
    pb_istream_t frame_input =
        pb_istream_from_buffer(frame_plaintext, frame_envelope->payload.size);
    mu_check(pb_decode(&frame_input, &PB_Main_msg, &decoded_frame));
    mu_check(decoded_frame.command_id == 0u);
    mu_check(decoded_frame.command_status == PB_CommandStatus_OK);
    mu_check(decoded_frame.which_content == PB_Main_gui_screen_frame_tag);
    mu_check(decoded_frame.content.gui_screen_frame.data->size == 1024u);
    mu_check(decoded_frame.content.gui_screen_frame.data->bytes[1023u] == 0xA5u);
    pb_release(&PB_Main_msg, &decoded_frame);
    pb_release(&PB_Main_msg, &encrypted_frame);
    memset(frame_plaintext, 0, sizeof(frame_plaintext));

    PB_Main policy_request = PB_Main_init_zero;
    policy_request.command_id = ++command_id;
    policy_request.which_content = PB_Main_poison_policy_request_tag;
    policy_request.content.poison_policy_request.role = PB_Poison_Role_ROLE_OPERATOR;
    policy_request.content.poison_policy_request.requested_capabilities =
        POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL;
    policy_request.content.poison_policy_request.policy_version = 1u;
    PB_Main policy_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &policy_request, &policy_response));
    mu_check(policy_response.command_status == PB_CommandStatus_OK);
    mu_check(policy_response.which_content == PB_Main_poison_policy_decision_tag);
    mu_check(policy_response.content.poison_policy_decision.allowed);
    mu_check(
        policy_response.content.poison_policy_decision.granted_capabilities ==
        (POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL));
    pb_release(&PB_Main_msg, &policy_response);

    policy_request.command_id = ++command_id;
    policy_request.content.poison_policy_request.role = PB_Poison_Role_ROLE_OWNER;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &policy_request, &policy_response));
    mu_check(policy_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    mu_check(policy_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &policy_response);

    PB_Main tool_start = PB_Main_init_zero;
    tool_start.command_id = ++command_id;
    tool_start.which_content = PB_Main_poison_tool_run_tag;
    strcpy(tool_start.content.poison_tool_run.tool_id, "usb-hid.inspect");
    strcpy(tool_start.content.poison_tool_run.run_id, "rpc-cancel-run");
    strcpy(tool_start.content.poison_tool_run.case_id, "local");
    strcpy(tool_start.content.poison_tool_run.tool_version, "builtin");
    strcpy(tool_start.content.poison_tool_run.state, "start");
    PB_Main tool_start_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &tool_start, &tool_start_response));
    mu_check(tool_start_response.command_status == PB_CommandStatus_OK);
    mu_check(poison_tools_run_is_active("rpc-cancel-run"));
    pb_release(&PB_Main_msg, &tool_start_response);

    PB_Main tool_cancel = PB_Main_init_zero;
    tool_cancel.command_id = ++command_id;
    tool_cancel.which_content = PB_Main_poison_cancel_request_tag;
    tool_cancel.content.poison_cancel_request.command_id = tool_start.command_id;
    strcpy(tool_cancel.content.poison_cancel_request.reason, "unit-test");
    PB_Main tool_cancel_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &tool_cancel, &tool_cancel_response));
    mu_check(tool_cancel_response.command_status == PB_CommandStatus_OK);
    mu_check(tool_cancel_response.which_content == PB_Main_poison_cancelled_tag);
    mu_check(tool_cancel_response.content.poison_cancelled.command_id == tool_start.command_id);
    mu_check(tool_cancel_response.content.poison_cancelled.accepted);
    mu_check(!poison_tools_run_is_active("rpc-cancel-run"));
    pb_release(&PB_Main_msg, &tool_cancel_response);

    TestRpcStructuredApp structured_app = {0};
    mu_check(poison_app_endpoint_register(
        "org.poison.rpc-test", "run-1", test_rpc_structured_app_command, &structured_app));

    PB_Main app_command = PB_Main_init_zero;
    app_command.command_id = ++command_id;
    app_command.which_content = PB_Main_poison_app_command_tag;
    strcpy(app_command.content.poison_app_command.app_id, "org.poison.rpc-test");
    strcpy(app_command.content.poison_app_command.run_id, "run-1");
    strcpy(app_command.content.poison_app_command.command_id, "inspect");
    app_command.content.poison_app_command.protocol_version = POISON_APP_PROTOCOL_VERSION;
    app_command.content.poison_app_command.chunk_count = 2u;
    app_command.content.poison_app_command.payload_chunk.size = 384u;
    memset(app_command.content.poison_app_command.payload_chunk.bytes, 'a', 384u);
    PB_Main app_response;
    mu_check(test_rpc_secure_round_trip(&client_session, session_id, &app_command, &app_response));
    mu_check(app_response.command_status == PB_CommandStatus_OK);
    mu_check(app_response.which_content == PB_Main_empty_tag);
    mu_check(!structured_app.called);
    pb_release(&PB_Main_msg, &app_response);

    app_command.command_id = ++command_id;
    app_command.content.poison_app_command.chunk_index = 1u;
    app_command.content.poison_app_command.payload_chunk.size = 16u;
    memset(app_command.content.poison_app_command.payload_chunk.bytes, 'a', 16u);
    mu_check(test_rpc_secure_round_trip(&client_session, session_id, &app_command, &app_response));
    mu_check(app_response.command_status == PB_CommandStatus_OK);
    mu_check(app_response.which_content == PB_Main_empty_tag);
    mu_check(structured_app.called);
    pb_release(&PB_Main_msg, &app_response);
    poison_app_endpoint_unregister(&structured_app);

    PB_Main profile_preview = PB_Main_init_zero;
    profile_preview.command_id = ++command_id;
    profile_preview.which_content = PB_Main_poison_profile_tag;
    PB_Poison_Profile* profile = &profile_preview.content.poison_profile;
    profile->format = 1u;
    strcpy(profile->id, "rpc.field");
    strcpy(profile->version, "1.0.0");
    strcpy(profile->role, "field");
    strcpy(profile->policy_id, "builtin.field");
    strcpy(profile->theme_id, "builtin.field-console");
    strcpy(profile->font_pack_id, "builtin.default");
    strcpy(profile->icon_pack_id, "builtin.default");
    strcpy(profile->menu_id, "builtin.field-console");
    strcpy(profile->dashboard_layout, "field-console");
    strcpy(profile->home_presentation, "builtin.field-console");
    strcpy(profile->status_presentation, "builtin.field-console");
    strcpy(profile->lock_behavior, "pin");
    strcpy(profile->tool_defaults_json, "{}");
    strcpy(profile->transport_policy, "local-only");
    strcpy(profile->logging_policy, "metadata");
    strcpy(profile->evidence_policy, "digest-only");
    strcpy(profile->radio_region, "device");
    strcpy(profile->peripheral_safety, "guarded");
    strcpy(profile->classroom_policy, "none");
    profile->notifications_enabled = true;
    profile->haptics_enabled = true;
    profile->contrast_ratio_x10 = 45u;
    profile->capability_mask = POISON_CAPABILITY_STATUS;
    PB_Main profile_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &profile_preview, &profile_response));
    mu_check(profile_response.command_status == PB_CommandStatus_OK);
    mu_check(profile_response.which_content == PB_Main_poison_profile_status_tag);
    mu_check(profile_response.content.poison_profile_status.preview);
    mu_check(
        profile_response.content.poison_profile_status.confirmation_token.size ==
        POISON_CONFIRMATION_TOKEN_BYTES);

    PB_Main profile_apply = PB_Main_init_zero;
    profile_apply.command_id = ++command_id;
    profile_apply.which_content = PB_Main_poison_profile_apply_tag;
    strcpy(profile_apply.content.poison_profile_apply.profile_id, "rpc.field");
    profile_apply.content.poison_profile_apply.confirmation_token_bytes.size =
        POISON_CONFIRMATION_TOKEN_BYTES;
    memcpy(
        profile_apply.content.poison_profile_apply.confirmation_token_bytes.bytes,
        profile_response.content.poison_profile_status.confirmation_token.bytes,
        POISON_CONFIRMATION_TOKEN_BYTES);
    pb_release(&PB_Main_msg, &profile_response);
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &profile_apply, &profile_response));
    mu_check(profile_response.command_status == PB_CommandStatus_OK);
    mu_check(profile_response.which_content == PB_Main_poison_profile_status_tag);
    mu_check(!profile_response.content.poison_profile_status.preview);
    mu_check(profile_approval.called);
    pb_release(&PB_Main_msg, &profile_response);

    profile_preview.command_id = ++command_id;
    strcpy(profile->id, "rpc.untrusted-assets");
    strcpy(profile->theme_id, "org.poison.theme.missing");
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &profile_preview, &profile_response));
    mu_check(profile_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    pb_release(&PB_Main_msg, &profile_response);

    PB_Main forbidden_plaintext_ping = PB_Main_init_zero;
    forbidden_plaintext_ping.command_id = ++command_id;
    forbidden_plaintext_ping.which_content = PB_Main_system_ping_request_tag;
    test_rpc_encode_and_feed_one(&forbidden_plaintext_ping, 0);
    PB_Main forbidden_response;
    mu_check(test_rpc_decode_one(&forbidden_response, 0));
    mu_check(forbidden_response.which_content == PB_Main_poison_session_envelope_tag);
    const PB_Poison_SessionEnvelope* forbidden_envelope =
        &forbidden_response.content.poison_session_envelope;
    uint8_t forbidden_plaintext[768u];
    mu_check(
        poison_session_decrypt_rx(
            &client_session,
            forbidden_envelope->sequence,
            forbidden_envelope->acknowledgement,
            forbidden_envelope->channel,
            forbidden_envelope->payload.bytes,
            forbidden_envelope->payload.size,
            forbidden_envelope->authentication_tag.bytes,
            forbidden_plaintext) == PoisonSessionResultOk);
    PB_Main forbidden_inner = PB_Main_init_zero;
    pb_istream_t forbidden_input =
        pb_istream_from_buffer(forbidden_plaintext, forbidden_envelope->payload.size);
    mu_check(pb_decode(&forbidden_input, &PB_Main_msg, &forbidden_inner));
    mu_check(forbidden_inner.command_id == forbidden_plaintext_ping.command_id);
    mu_check(forbidden_inner.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    pb_release(&PB_Main_msg, &forbidden_inner);
    pb_release(&PB_Main_msg, &forbidden_response);

    PB_Main inner_ping = PB_Main_init_zero;
    inner_ping.command_id = ++command_id;
    inner_ping.which_content = PB_Main_system_ping_request_tag;
    uint8_t plaintext[768u];
    pb_ostream_t inner_output = pb_ostream_from_buffer(plaintext, sizeof(plaintext));
    mu_check(pb_encode(&inner_output, &PB_Main_msg, &inner_ping));

    PB_Main envelope_message = PB_Main_init_zero;
    envelope_message.command_id = inner_ping.command_id;
    envelope_message.which_content = PB_Main_poison_session_envelope_tag;
    PB_Poison_SessionEnvelope* envelope = &envelope_message.content.poison_session_envelope;
    envelope->protocol_version = 2u;
    envelope->session_id = session_id;
    envelope->acknowledgement = 0u;
    strcpy(envelope->channel, "rpc");
    envelope->payload.size = inner_output.bytes_written;
    envelope->authentication_tag.size = POISON_SESSION_AUTH_TAG_BYTES;
    mu_check(
        poison_session_encrypt_tx(
            &client_session,
            envelope->acknowledgement,
            envelope->channel,
            plaintext,
            inner_output.bytes_written,
            &envelope->sequence,
            envelope->payload.bytes,
            envelope->authentication_tag.bytes) == PoisonSessionResultOk);
    test_rpc_encode_and_feed_one(&envelope_message, 0);

    PB_Main encrypted_response;
    mu_check(test_rpc_decode_one(&encrypted_response, 0));
    mu_check(encrypted_response.which_content == PB_Main_poison_session_envelope_tag);
    const PB_Poison_SessionEnvelope* response_envelope =
        &encrypted_response.content.poison_session_envelope;
    mu_check(response_envelope->session_id == session_id);
    mu_check(response_envelope->acknowledgement == 0u);
    uint8_t response_plaintext[768u];
    mu_check(
        poison_session_decrypt_rx(
            &client_session,
            response_envelope->sequence,
            response_envelope->acknowledgement,
            response_envelope->channel,
            response_envelope->payload.bytes,
            response_envelope->payload.size,
            response_envelope->authentication_tag.bytes,
            response_plaintext) == PoisonSessionResultOk);
    PB_Main inner_response = PB_Main_init_zero;
    pb_istream_t inner_input =
        pb_istream_from_buffer(response_plaintext, response_envelope->payload.size);
    mu_check(pb_decode(&inner_input, &PB_Main_msg, &inner_response));
    mu_check(inner_response.which_content == PB_Main_system_ping_response_tag);
    mu_check(inner_response.command_id == inner_ping.command_id);
    mu_check(
        memcmp(
            response_envelope->payload.bytes,
            response_plaintext,
            response_envelope->payload.size) != 0);

    PB_Main update_import = PB_Main_init_zero;
    update_import.command_id = ++command_id;
    update_import.which_content = PB_Main_poison_content_update_request_tag;
    PB_Poison_ContentUpdateRequest* update = &update_import.content.poison_content_update_request;
    update->operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_IMPORT;
    strcpy(update->update_id, "firmware-2");
    strcpy(update->manifest_path, "/ext/update/poison/update.poison");
    update->content_type = PB_Poison_ContentUpdateType_CONTENT_UPDATE_TYPE_FIRMWARE;
    strcpy(
        update->candidate_digest,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    strcpy(
        update->previous_digest,
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
    update->release_sequence = 2u;
    update->content_bytes = 8192u;
    PB_Main update_response;
    mu_check(
        test_rpc_secure_round_trip(&client_session, session_id, &update_import, &update_response));
    mu_check(update_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    mu_check(update_response.which_content == 0u);

    PB_Main package_inspect = PB_Main_init_zero;
    package_inspect.command_id = ++command_id;
    package_inspect.which_content = PB_Main_poison_package_operation_request_tag;
    package_inspect.content.poison_package_operation_request.operation =
        PB_Poison_PackageOperation_PACKAGE_OPERATION_INSPECT;
    strcpy(
        package_inspect.content.poison_package_operation_request.package_id,
        "org.poison.dispatch-test");
    PB_Main package_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &package_inspect, &package_response));
    mu_check(package_response.command_status == PB_CommandStatus_OK);
    mu_check(package_response.which_content == PB_Main_poison_package_operation_status_tag);
    mu_check(
        strcmp(package_response.content.poison_package_operation_status.result, "not-found") == 0);

    static const char* const transfer_digest =
        "7cb2062b7be22ae0c9f9add987d054a6d9b18bd97d60eb26e07bf7d9485a51e2";
    static const char* const transfer_root = "/int/config/.rpc-workload";
    static const char* const transfer_version = "/int/config/.rpc-workload/versions/v1";
    static const char* const transfer_path = "/int/config/.rpc-workload/versions/v1/main.js";
    static const char* const transfer_temporary_path =
        "/int/config/.rpc-workload/versions/v1/main.js.poison-upload";
    Storage* transfer_storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_remove(transfer_storage, transfer_path);
    (void)storage_common_remove(transfer_storage, transfer_temporary_path);
    (void)storage_common_remove(transfer_storage, transfer_version);
    (void)storage_common_remove(transfer_storage, "/int/config/.rpc-workload/versions");
    (void)storage_common_remove(transfer_storage, transfer_root);

    PB_Main transfer_begin = PB_Main_init_zero;
    transfer_begin.command_id = ++command_id;
    transfer_begin.which_content = PB_Main_poison_file_transfer_begin_tag;
    strcpy(transfer_begin.content.poison_file_transfer_begin.operation_id, "rpc-transfer-1");
    strcpy(
        transfer_begin.content.poison_file_transfer_begin.path,
        "/config/.rpc-workload/versions/v1/main.js");
    transfer_begin.content.poison_file_transfer_begin.size = sizeof("rpc-transfer") - 1u;
    strcpy(transfer_begin.content.poison_file_transfer_begin.sha256, transfer_digest);
    PB_Main transfer_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_begin, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    mu_check(transfer_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &transfer_response);

    PB_Main transfer_chunk = PB_Main_init_zero;
    transfer_chunk.command_id = ++command_id;
    transfer_chunk.which_content = PB_Main_poison_file_transfer_chunk_tag;
    strcpy(transfer_chunk.content.poison_file_transfer_chunk.operation_id, "rpc-transfer-1");
    transfer_chunk.content.poison_file_transfer_chunk.offset = 0u;
    transfer_chunk.content.poison_file_transfer_chunk.data.size = sizeof("rpc-transfer") - 1u;
    memcpy(
        transfer_chunk.content.poison_file_transfer_chunk.data.bytes,
        "rpc-transfer",
        sizeof("rpc-transfer") - 1u);
    strcpy(transfer_chunk.content.poison_file_transfer_chunk.sha256, transfer_digest);
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_chunk, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    pb_release(&PB_Main_msg, &transfer_response);

    PB_Main transfer_complete = PB_Main_init_zero;
    transfer_complete.command_id = ++command_id;
    transfer_complete.which_content = PB_Main_poison_file_transfer_complete_tag;
    strcpy(transfer_complete.content.poison_file_transfer_complete.operation_id, "rpc-transfer-1");
    strcpy(transfer_complete.content.poison_file_transfer_complete.sha256, transfer_digest);
    transfer_complete.content.poison_file_transfer_complete.size = sizeof("rpc-transfer") - 1u;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_complete, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    mu_check(storage_file_exists(transfer_storage, transfer_path));
    mu_check(!storage_file_exists(transfer_storage, transfer_temporary_path));
    pb_release(&PB_Main_msg, &transfer_response);

    transfer_begin.command_id = ++command_id;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_begin, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    pb_release(&PB_Main_msg, &transfer_response);
    transfer_chunk.command_id = ++command_id;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_chunk, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    pb_release(&PB_Main_msg, &transfer_response);
    transfer_complete.command_id = ++command_id;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &transfer_complete, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    mu_check(storage_file_exists(transfer_storage, transfer_path));
    pb_release(&PB_Main_msg, &transfer_response);

    if(storage_sd_status(transfer_storage) == FSE_OK) {
        const char* evidence_object_path =
            "/ext/evidence/objects/7cb2062b7be22ae0c9f9add987d054a6d9b18bd97d60eb26e07bf7d9485a51e2.bin";
        const bool evidence_object_preexisting =
            storage_file_exists(transfer_storage, evidence_object_path);
        char case_id[65u];
        char evidence_id[65u];
        snprintf(case_id, sizeof(case_id), "rpc-case-%lu", (unsigned long)furi_get_tick());
        snprintf(
            evidence_id, sizeof(evidence_id), "rpc-evidence-%lu", (unsigned long)furi_get_tick());

        PB_Main invalid_case_request = PB_Main_init_zero;
        invalid_case_request.command_id = ++command_id;
        invalid_case_request.which_content = PB_Main_poison_case_tag;
        strcpy(invalid_case_request.content.poison_case.case_id, "../rpc-case");
        strcpy(invalid_case_request.content.poison_case.name, "Invalid case");
        strcpy(invalid_case_request.content.poison_case.purpose, "Traversal regression");
        strcpy(invalid_case_request.content.poison_case.retention_policy, "manual");
        PB_Main case_response;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &invalid_case_request, &case_response));
        mu_check(case_response.command_status == PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        pb_release(&PB_Main_msg, &case_response);

        PB_Main case_request = PB_Main_init_zero;
        case_request.command_id = ++command_id;
        case_request.which_content = PB_Main_poison_case_tag;
        strcpy(case_request.content.poison_case.case_id, case_id);
        strcpy(case_request.content.poison_case.name, "RPC evidence case");
        strcpy(case_request.content.poison_case.purpose, "Encrypted dispatcher regression");
        strcpy(case_request.content.poison_case.retention_policy, "manual");
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &case_request, &case_response));
        mu_check(case_response.command_status == PB_CommandStatus_OK);
        mu_check(case_response.which_content == PB_Main_poison_case_tag);
        mu_check(strcmp(case_response.content.poison_case.case_id, case_id) == 0);
        mu_check(strncmp(case_response.content.poison_case.owner_id, "session-", 8u) == 0);
        mu_check(case_response.content.poison_case.created_at_ms > 0u);
        const uint64_t case_created_at_ms = case_response.content.poison_case.created_at_ms;
        pb_release(&PB_Main_msg, &case_response);
        case_request.command_id = ++command_id;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &case_request, &case_response));
        mu_check(case_response.command_status == PB_CommandStatus_OK);
        mu_check(case_response.content.poison_case.created_at_ms == case_created_at_ms);
        pb_release(&PB_Main_msg, &case_response);

        PB_Main evidence_request = PB_Main_init_zero;
        evidence_request.command_id = ++command_id;
        evidence_request.which_content = PB_Main_poison_evidence_record_tag;
        strcpy(evidence_request.content.poison_evidence_record.evidence_id, evidence_id);
        strcpy(evidence_request.content.poison_evidence_record.case_id, case_id);
        strcpy(evidence_request.content.poison_evidence_record.source_app_id, "rpc-test");
        strcpy(evidence_request.content.poison_evidence_record.content_sha256, transfer_digest);
        evidence_request.content.poison_evidence_record.content_length =
            sizeof("rpc-transfer") - 1u;
        strcpy(
            evidence_request.content.poison_evidence_record.media_type,
            "application/octet-stream");
        strcpy(
            evidence_request.content.poison_evidence_record.source_path,
            "/config/.rpc-transfer-test");
        PB_Main evidence_response;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &evidence_request, &evidence_response));
        mu_check(evidence_response.command_status == PB_CommandStatus_OK);
        mu_check(evidence_response.which_content == PB_Main_poison_evidence_record_tag);
        mu_check(
            strcmp(evidence_response.content.poison_evidence_record.evidence_id, evidence_id) ==
            0);
        mu_check(
            strcmp(
                evidence_response.content.poison_evidence_record.content_sha256,
                transfer_digest) == 0);
        mu_check(strlen(evidence_response.content.poison_evidence_record.audit_sha256) == 64u);
        pb_release(&PB_Main_msg, &evidence_response);

        char evidence_record_path[192u];
        snprintf(
            evidence_record_path,
            sizeof(evidence_record_path),
            "/ext/evidence/records/%s.pev",
            evidence_id);
        mu_check(storage_file_exists(transfer_storage, evidence_record_path));
        mu_check(storage_file_exists(transfer_storage, evidence_object_path));

        char annotation_id[65u];
        snprintf(
            annotation_id,
            sizeof(annotation_id),
            "rpc-annotation-%lu",
            (unsigned long)furi_get_tick());
        PB_Main annotation_request = PB_Main_init_zero;
        annotation_request.command_id = ++command_id;
        annotation_request.which_content = PB_Main_poison_annotation_tag;
        strcpy(annotation_request.content.poison_annotation.annotation_id, annotation_id);
        strcpy(annotation_request.content.poison_annotation.evidence_id, evidence_id);
        strcpy(annotation_request.content.poison_annotation.text, "Verified RPC capture");
        annotation_request.content.poison_annotation.tags_count = 2u;
        strcpy(annotation_request.content.poison_annotation.tags[0], "rpc");
        strcpy(annotation_request.content.poison_annotation.tags[1], "verified");
        PB_Main annotation_response;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &annotation_request, &annotation_response));
        mu_check(annotation_response.command_status == PB_CommandStatus_OK);
        mu_check(annotation_response.which_content == PB_Main_poison_annotation_tag);
        mu_check(
            strcmp(annotation_response.content.poison_annotation.annotation_id, annotation_id) ==
            0);
        mu_check(annotation_response.content.poison_annotation.tags_count == 2u);
        mu_check(
            strncmp(annotation_response.content.poison_annotation.author_id, "session-", 8u) == 0);
        pb_release(&PB_Main_msg, &annotation_response);

        char export_id[65u];
        snprintf(export_id, sizeof(export_id), "rpc-export-%lu", (unsigned long)furi_get_tick());
        const char* export_values[] = {evidence_id};
        TestRpcStringList export_list = {
            .values = export_values,
            .count = COUNT_OF(export_values),
        };
        PB_Main export_request = PB_Main_init_zero;
        export_request.command_id = ++command_id;
        export_request.which_content = PB_Main_poison_export_manifest_tag;
        strcpy(export_request.content.poison_export_manifest.export_id, export_id);
        strcpy(
            export_request.content.poison_export_manifest.schema, "poison.evidence-manifest/v1");
        export_request.content.poison_export_manifest.evidence_ids.funcs.encode =
            test_rpc_encode_strings;
        export_request.content.poison_export_manifest.evidence_ids.arg = &export_list;
        export_request.content.poison_export_manifest.finalize = true;
        PB_Main export_response;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &export_request, &export_response));
        mu_check(export_response.command_status == PB_CommandStatus_OK);
        mu_check(export_response.which_content == PB_Main_poison_export_manifest_tag);
        mu_check(export_response.content.poison_export_manifest.accepted_evidence_ids == 1u);
        mu_check(strlen(export_response.content.poison_export_manifest.manifest_sha256) == 64u);
        mu_check(export_response.content.poison_export_manifest.signature[0] == '\0');
        char manifest_sha256[65u];
        strcpy(manifest_sha256, export_response.content.poison_export_manifest.manifest_sha256);
        pb_release(&PB_Main_msg, &export_response);

        export_request.command_id = ++command_id;
        mu_check(test_rpc_secure_round_trip(
            &client_session, session_id, &export_request, &export_response));
        mu_check(export_response.command_status == PB_CommandStatus_OK);
        mu_check(
            strcmp(
                export_response.content.poison_export_manifest.manifest_sha256, manifest_sha256) ==
            0);
        pb_release(&PB_Main_msg, &export_response);

        char case_record_path[192u];
        char annotation_record_path[192u];
        char export_record_path[192u];
        snprintf(
            case_record_path, sizeof(case_record_path), "/ext/evidence/cases/%s.pcase", case_id);
        snprintf(
            annotation_record_path,
            sizeof(annotation_record_path),
            "/ext/evidence/annotations/%s.pann",
            annotation_id);
        snprintf(
            export_record_path,
            sizeof(export_record_path),
            "/ext/evidence/exports/%s.pmanifest",
            export_id);
        mu_check(storage_file_exists(transfer_storage, case_record_path));
        mu_check(storage_file_exists(transfer_storage, annotation_record_path));
        mu_check(storage_file_exists(transfer_storage, export_record_path));
        mu_check(storage_common_remove(transfer_storage, annotation_record_path) == FSE_OK);
        mu_check(storage_common_remove(transfer_storage, export_record_path) == FSE_OK);
        mu_check(storage_common_remove(transfer_storage, evidence_record_path) == FSE_OK);
        mu_check(storage_common_remove(transfer_storage, case_record_path) == FSE_OK);
        if(!evidence_object_preexisting)
            mu_check(storage_common_remove(transfer_storage, evidence_object_path) == FSE_OK);
    }
    mu_check(storage_common_remove(transfer_storage, transfer_path) == FSE_OK);
    mu_check(storage_common_remove(transfer_storage, transfer_version) == FSE_OK);
    mu_check(
        storage_common_remove(transfer_storage, "/int/config/.rpc-workload/versions") == FSE_OK);
    mu_check(storage_common_remove(transfer_storage, transfer_root) == FSE_OK);
    furi_record_close(RECORD_STORAGE);

    PB_Main file_list = PB_Main_init_zero;
    file_list.command_id = ++command_id;
    file_list.which_content = PB_Main_poison_file_list_request_tag;
    strcpy(file_list.content.poison_file_list_request.path, "/config");
    strcpy(file_list.content.poison_file_list_request.cursor, "9999");
    file_list.content.poison_file_list_request.page_size = 1u;
    mu_check(
        test_rpc_secure_round_trip(&client_session, session_id, &file_list, &transfer_response));
    mu_check(transfer_response.command_status == PB_CommandStatus_OK);
    mu_check(transfer_response.which_content == PB_Main_poison_file_list_response_tag);
    mu_check(!transfer_response.has_next);
    pb_release(&PB_Main_msg, &transfer_response);

    poison_diagnostics_init(poison_diagnostics_get());
    poison_diagnostics_increment(poison_diagnostics_get(), PoisonDiagnosticUpdateStage);
    PB_Main diagnostic_snapshot = PB_Main_init_zero;
    diagnostic_snapshot.command_id = ++command_id;
    diagnostic_snapshot.which_content = PB_Main_poison_diagnostic_snapshot_request_tag;
    diagnostic_snapshot.content.poison_diagnostic_snapshot_request.max_events = 0u;
    PB_Main diagnostic_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &diagnostic_snapshot, &diagnostic_response));
    mu_check(diagnostic_response.command_status == PB_CommandStatus_OK);
    mu_check(diagnostic_response.which_content == PB_Main_poison_diagnostic_counters_tag);
    mu_check(diagnostic_response.content.poison_diagnostic_counters.update_stages == 1u);
    mu_check(!diagnostic_response.has_next);

    PB_Main audit_snapshot = PB_Main_init_zero;
    audit_snapshot.command_id = ++command_id;
    audit_snapshot.which_content = PB_Main_poison_audit_snapshot_request_tag;
    audit_snapshot.content.poison_audit_snapshot_request.after_event_id = UINT64_MAX;
    audit_snapshot.content.poison_audit_snapshot_request.max_events = POISON_AUDIT_RING_SIZE;
    PB_Main audit_response;
    mu_check(
        test_rpc_secure_round_trip(&client_session, session_id, &audit_snapshot, &audit_response));
    mu_check(audit_response.command_status == PB_CommandStatus_OK);
    mu_check(audit_response.which_content == PB_Main_poison_audit_snapshot_end_tag);
    mu_check(audit_response.content.poison_audit_snapshot_end.next_event_id >= 1u);
    mu_check(
        audit_response.content.poison_audit_snapshot_end.last_digest.size ==
        POISON_AUDIT_DIGEST_BYTES);
    mu_check(!audit_response.has_next);

    PB_Main resume_issue = PB_Main_init_zero;
    resume_issue.command_id = ++command_id;
    resume_issue.which_content = PB_Main_poison_resume_request_tag;
    resume_issue.content.poison_resume_request.session_id = session_id;
    PB_Main resume_issue_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &resume_issue, &resume_issue_response));
    mu_check(resume_issue_response.command_status == PB_CommandStatus_OK);
    mu_check(resume_issue_response.which_content == PB_Main_poison_resume_response_tag);
    mu_check(resume_issue_response.content.poison_resume_response.accepted);
    mu_check(
        resume_issue_response.content.poison_resume_response.resume_token.size ==
        POISON_SESSION_RESUME_TOKEN_BYTES);
    uint8_t resume_token[POISON_SESSION_RESUME_TOKEN_BYTES];
    memcpy(
        resume_token,
        resume_issue_response.content.poison_resume_response.resume_token.bytes,
        sizeof(resume_token));
    const uint64_t last_received_sequence = client_session.next_rx_sequence - 1u;
    const uint64_t resumed_next_sequence = client_session.next_rx_sequence;
    pb_release(&PB_Main_msg, &resume_issue_response);

    test_rpc_teardown();
    test_rpc_setup();
    PB_Main resume_request = PB_Main_init_zero;
    resume_request.command_id = ++command_id;
    resume_request.which_content = PB_Main_poison_resume_request_tag;
    resume_request.content.poison_resume_request.session_id = session_id;
    resume_request.content.poison_resume_request.resume_token.size = sizeof(resume_token);
    memcpy(
        resume_request.content.poison_resume_request.resume_token.bytes,
        resume_token,
        sizeof(resume_token));
    resume_request.content.poison_resume_request.last_received_sequence = last_received_sequence;
    test_rpc_encode_and_feed_one(&resume_request, 0u);
    PB_Main resume_response = PB_Main_init_zero;
    mu_check(test_rpc_decode_one(&resume_response, 0u));
    mu_check(resume_response.command_id == resume_request.command_id);
    mu_check(resume_response.command_status == PB_CommandStatus_OK);
    mu_check(resume_response.which_content == PB_Main_poison_resume_response_tag);
    mu_check(resume_response.content.poison_resume_response.accepted);
    mu_check(
        resume_response.content.poison_resume_response.next_sequence == resumed_next_sequence);
    mu_check(
        resume_response.content.poison_resume_response.resume_token.size ==
        POISON_SESSION_RESUME_TOKEN_BYTES);
    uint8_t rotated_resume_token[POISON_SESSION_RESUME_TOKEN_BYTES];
    memcpy(
        rotated_resume_token,
        resume_response.content.poison_resume_response.resume_token.bytes,
        sizeof(rotated_resume_token));
    mu_check(
        memcmp(
            resume_response.content.poison_resume_response.resume_token.bytes,
            resume_token,
            sizeof(resume_token)) != 0);
    pb_release(&PB_Main_msg, &resume_response);

    PB_Main resumed_channel_open = PB_Main_init_zero;
    resumed_channel_open.command_id = ++command_id;
    resumed_channel_open.which_content = PB_Main_poison_channel_open_tag;
    strcpy(resumed_channel_open.content.poison_channel_open.channel, "rpc");
    resumed_channel_open.content.poison_channel_open.initial_credits = 4u;
    resumed_channel_open.content.poison_channel_open.resume_sequence = 1u;
    PB_Main resumed_channel_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &resumed_channel_open, &resumed_channel_response));
    mu_check(resumed_channel_response.command_status == PB_CommandStatus_OK);
    mu_check(resumed_channel_response.which_content == PB_Main_poison_channel_opened_tag);
    mu_check(resumed_channel_response.content.poison_channel_opened.next_sequence == 1u);
    pb_release(&PB_Main_msg, &resumed_channel_response);

    PB_Main resumed_ping = PB_Main_init_zero;
    resumed_ping.command_id = ++command_id;
    resumed_ping.which_content = PB_Main_system_ping_request_tag;
    resumed_ping.content.system_ping_request.data->size = 4u;
    memcpy(resumed_ping.content.system_ping_request.data->bytes, "back", 4u);
    PB_Main resumed_ping_response;
    mu_check(test_rpc_secure_round_trip(
        &client_session, session_id, &resumed_ping, &resumed_ping_response));
    mu_check(resumed_ping_response.command_status == PB_CommandStatus_OK);
    mu_check(resumed_ping_response.which_content == PB_Main_system_ping_response_tag);
    pb_release(&PB_Main_msg, &resumed_ping_response);

    PB_Main stop_session = PB_Main_init_zero;
    stop_session.command_id = ++command_id;
    stop_session.which_content = PB_Main_stop_session_tag;
    PB_Main stop_response;
    mu_check(
        test_rpc_secure_round_trip(&client_session, session_id, &stop_session, &stop_response));
    mu_check(stop_response.command_status == PB_CommandStatus_OK);
    mu_check(stop_response.which_content == PB_Main_empty_tag);
    pb_release(&PB_Main_msg, &stop_response);
    const uint64_t stopped_last_received_sequence = client_session.next_rx_sequence - 1u;

    test_rpc_teardown();
    test_rpc_setup();
    PB_Main revoked_resume = PB_Main_init_zero;
    revoked_resume.command_id = ++command_id;
    revoked_resume.which_content = PB_Main_poison_resume_request_tag;
    revoked_resume.content.poison_resume_request.session_id = session_id;
    revoked_resume.content.poison_resume_request.resume_token.size = sizeof(rotated_resume_token);
    memcpy(
        revoked_resume.content.poison_resume_request.resume_token.bytes,
        rotated_resume_token,
        sizeof(rotated_resume_token));
    revoked_resume.content.poison_resume_request.last_received_sequence =
        stopped_last_received_sequence;
    test_rpc_encode_and_feed_one(&revoked_resume, 0u);
    PB_Main revoked_resume_response = PB_Main_init_zero;
    mu_check(test_rpc_decode_one(&revoked_resume_response, 0u));
    mu_check(revoked_resume_response.command_id == revoked_resume.command_id);
    mu_check(revoked_resume_response.which_content == PB_Main_poison_resume_response_tag);
    mu_check(!revoked_resume_response.content.poison_resume_response.accepted);
    pb_release(&PB_Main_msg, &revoked_resume_response);

    memset(client_private, 0, sizeof(client_private));
    memset(shared_secret, 0, sizeof(shared_secret));
    memset(directional_keys, 0, sizeof(directional_keys));
    memset(resume_token, 0, sizeof(resume_token));
    memset(rotated_resume_token, 0, sizeof(rotated_resume_token));
    pb_release(&PB_Main_msg, &inner_response);
    pb_release(&PB_Main_msg, &app_response);
    pb_release(&PB_Main_msg, &update_response);
    pb_release(&PB_Main_msg, &package_response);
    pb_release(&PB_Main_msg, &diagnostic_response);
    pb_release(&PB_Main_msg, &audit_response);
    pb_release(&PB_Main_msg, &encrypted_response);
    test_rpc_teardown();
}

MU_TEST_SUITE(test_rpc_session) {
    MU_RUN_TEST(test_rpc_feed_rubbish);
    MU_RUN_TEST(test_rpc_multisession_ping);
    MU_RUN_TEST(test_rpc_poison_pairing_and_encrypted_ping_use_real_dispatcher);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_sd_status(storage) != FSE_OK) {
        FURI_LOG_E(TAG, "SD card not mounted - skip storage tests");
    } else {
        MU_RUN_TEST(test_rpc_multisession_storage);
    }
    furi_record_close(RECORD_STORAGE);
}

int run_minunit_test_rpc(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_sd_status(storage) != FSE_OK) {
        FURI_LOG_E(TAG, "SD card not mounted - skip storage tests");
    } else {
        MU_RUN_SUITE(test_rpc_storage);
    }
    furi_record_close(RECORD_STORAGE);
    MU_RUN_SUITE(test_rpc_system);
    MU_RUN_SUITE(test_rpc_app);
    MU_RUN_SUITE(test_rpc_session);
    poison_session_run_tests();
    poison_channel_run_tests();
    poison_crypto_run_tests();
    poison_session_state_run_tests();
    poison_policy_run_tests();
    poison_confirmation_run_tests();
    poison_diagnostics_run_tests();
    test_poison_pairing_ui();
    poison_file_contract_run_tests();
    poison_vfs_path_run_tests();
    poison_vfs_journal_run_tests();
    poison_migration_run_tests();
    poison_safe_sample_run_tests();
    poison_js_developer_policy_run_tests();
    poison_evidence_run_tests();
    poison_evidence_rpc_run_tests();
    poison_workspace_run_tests();
    poison_package_verify_run_tests();
    poison_package_transaction_run_tests();
    poison_package_catalog_run_tests();
    poison_app_protocol_run_tests();
    poison_profiles_run_tests();
    poison_tools_catalog_run_tests();
    poison_tool_nfc_run_tests();
    poison_tool_lfrfid_run_tests();
    poison_tool_ibutton_run_tests();
    poison_tool_infrared_run_tests();
    poison_tool_subghz_run_tests();
    poison_tool_gpio_run_tests();
    poison_rust_api_run_tests();
    poison_js_capabilities_run_tests();
    poison_js_limits_run_tests();
    poison_content_update_run_tests();
    poison_workload_run_tests();
    poison_lessons_run_tests();
    poison_assignments_run_tests();
    poison_wasm_run_tests();

    return MU_EXIT_CODE;
}

int32_t delay_test_app(void* p) {
    int timeout = atoi((const char*)p);

    if(timeout > 0) {
        furi_delay_ms(timeout);
    }

    return 0;
}

TEST_API_DEFINE(run_minunit_test_rpc)
