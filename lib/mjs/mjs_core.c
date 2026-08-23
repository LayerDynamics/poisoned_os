/*
 * Copyright (c) 2017 Cesanta Software Limited
 * All rights reserved
 */

#include "common/cs_varint.h"
#include "common/str_util.h"

#include "mjs_bcode.h"
#include "mjs_builtin.h"
#include "mjs_core.h"
#include "mjs_exec.h"
#include "mjs_ffi.h"
#include "mjs_internal.h"
#include "mjs_object.h"
#include "mjs_primitive.h"
#include "mjs_string.h"
#include "mjs_util.h"

#include <limits.h>

#ifndef MJS_OBJECT_ARENA_SIZE
#define MJS_OBJECT_ARENA_SIZE 20
#endif
#ifndef MJS_PROPERTY_ARENA_SIZE
#define MJS_PROPERTY_ARENA_SIZE 20
#endif
#ifndef MJS_FUNC_FFI_ARENA_SIZE
#define MJS_FUNC_FFI_ARENA_SIZE 20
#endif
#ifndef MJS_CLOSURE_ARENA_SIZE
#define MJS_CLOSURE_ARENA_SIZE 20
#endif

#ifndef MJS_OBJECT_ARENA_INC_SIZE
#define MJS_OBJECT_ARENA_INC_SIZE 10
#endif
#ifndef MJS_PROPERTY_ARENA_INC_SIZE
#define MJS_PROPERTY_ARENA_INC_SIZE 10
#endif
#ifndef MJS_FUNC_FFI_ARENA_INC_SIZE
#define MJS_FUNC_FFI_ARENA_INC_SIZE 10
#endif
#ifndef MJS_CLOSURE_ARENA_INC_SIZE
#define MJS_CLOSURE_ARENA_INC_SIZE 10
#endif

void mjs_destroy(struct mjs* mjs) {
    {
        int parts_cnt = mjs_bcode_parts_cnt(mjs);
        int i;
        for(i = 0; i < parts_cnt; i++) {
            struct mjs_bcode_part* bp = mjs_bcode_part_get(mjs, i);
            if(!bp->in_rom) {
                free((void*)bp->data.p);
            }
        }
    }

    mbuf_free(&mjs->bcode_gen);
    mbuf_free(&mjs->bcode_parts);
    mbuf_free(&mjs->stack);
    mbuf_free(&mjs->call_stack);
    mbuf_free(&mjs->arg_stack);
    mbuf_free(&mjs->owned_strings);
    mbuf_free(&mjs->foreign_strings);
    mbuf_free(&mjs->owned_values);
    mbuf_free(&mjs->scopes);
    mbuf_free(&mjs->loop_addresses);
    mbuf_free(&mjs->json_visited_stack);
    mbuf_free(&mjs->array_buffers);
    free(mjs->error_msg);
    free(mjs->stack_trace);
    mjs_ffi_args_free_list(mjs);
    gc_arena_destroy(mjs, &mjs->object_arena);
    gc_arena_destroy(mjs, &mjs->property_arena);
    gc_arena_destroy(mjs, &mjs->ffi_sig_arena);
    gc_arena_destroy(mjs, &mjs->closure_arena);
    free(mjs);
}

struct mjs* mjs_create(void* context) {
    mjs_val_t global_object;
    struct mjs* mjs = calloc(1, sizeof(*mjs));
    mjs->context = context;
    mbuf_init(&mjs->stack, 0);
    mbuf_init(&mjs->call_stack, 0);
    mbuf_init(&mjs->arg_stack, 0);
    mbuf_init(&mjs->owned_strings, 0);
    mbuf_init(&mjs->foreign_strings, 0);
    mbuf_init(&mjs->bcode_gen, 0);
    mbuf_init(&mjs->bcode_parts, 0);
    mbuf_init(&mjs->owned_values, 0);
    mbuf_init(&mjs->scopes, 0);
    mbuf_init(&mjs->loop_addresses, 0);
    mbuf_init(&mjs->json_visited_stack, 0);
    mbuf_init(&mjs->array_buffers, 0);

    mjs->bcode_len = 0;
    mjs->debug_last_bcode_offset = SIZE_MAX;
    mjs->debug_last_bcode_start = SIZE_MAX;
    mjs->debug_last_line = -1;

    /*
   * The compacting GC exploits the null terminator of the previous string as a
   * marker.
   */
    {
        char z = 0;
        mbuf_append(&mjs->owned_strings, &z, 1);
    }

    gc_arena_init(
        &mjs->object_arena,
        sizeof(struct mjs_object),
        MJS_OBJECT_ARENA_SIZE,
        MJS_OBJECT_ARENA_INC_SIZE);
    mjs->object_arena.destructor = mjs_obj_destructor;
    gc_arena_init(
        &mjs->property_arena,
        sizeof(struct mjs_property),
        MJS_PROPERTY_ARENA_SIZE,
        MJS_PROPERTY_ARENA_INC_SIZE);
    gc_arena_init(
        &mjs->ffi_sig_arena,
        sizeof(struct mjs_ffi_sig),
        MJS_FUNC_FFI_ARENA_SIZE,
        MJS_FUNC_FFI_ARENA_INC_SIZE);
    mjs->ffi_sig_arena.destructor = mjs_ffi_sig_destructor;
    gc_arena_init(
        &mjs->closure_arena,
        sizeof(struct mjs_closure),
        MJS_CLOSURE_ARENA_SIZE,
        MJS_CLOSURE_ARENA_INC_SIZE);
    mjs->closure_arena.destructor = mjs_closure_destructor;

    global_object = mjs_mk_object(mjs);
    mjs_init_builtin(mjs, global_object);
    mjs_set_ffi_resolver(mjs, NULL, NULL);
    push_mjs_val(&mjs->scopes, global_object);
    mjs->vals.this_obj = MJS_UNDEFINED;
    mjs->vals.dataview_proto = MJS_UNDEFINED;

    return mjs;
}

static void mjs_memory_usage_add(size_t* total, size_t value) {
    if(value > SIZE_MAX - *total) {
        *total = SIZE_MAX;
    } else {
        *total += value;
    }
}

static void mjs_memory_usage_arena(size_t* total, const struct gc_arena* arena) {
    for(const struct gc_block* block = arena->blocks; block; block = block->next) {
        mjs_memory_usage_add(total, sizeof(*block));
        if(arena->cell_size != 0 && block->size > SIZE_MAX / arena->cell_size) {
            *total = SIZE_MAX;
        } else {
            mjs_memory_usage_add(total, block->size * arena->cell_size);
        }
    }
}

size_t mjs_memory_usage(const struct mjs* mjs) {
    if(!mjs) return 0;
    size_t total = sizeof(*mjs);
    const struct mbuf* buffers[] = {
        &mjs->bcode_gen,
        &mjs->bcode_parts,
        &mjs->stack,
        &mjs->call_stack,
        &mjs->arg_stack,
        &mjs->scopes,
        &mjs->loop_addresses,
        &mjs->owned_strings,
        &mjs->foreign_strings,
        &mjs->owned_values,
        &mjs->json_visited_stack,
        &mjs->array_buffers,
    };
    for(size_t index = 0; index < sizeof(buffers) / sizeof(buffers[0]); ++index) {
        mjs_memory_usage_add(&total, buffers[index]->size);
    }
    for(int index = 0; index < mjs_bcode_parts_cnt((struct mjs*)mjs); ++index) {
        const struct mjs_bcode_part* part = mjs_bcode_part_get((struct mjs*)mjs, index);
        if(!part->in_rom) mjs_memory_usage_add(&total, part->data.len);
    }
    mjs_memory_usage_arena(&total, &mjs->object_arena);
    mjs_memory_usage_arena(&total, &mjs->property_arena);
    mjs_memory_usage_arena(&total, &mjs->ffi_sig_arena);
    mjs_memory_usage_arena(&total, &mjs->closure_arena);
    mjs_memory_usage_add(&total, mjs->closure_scope_bytes);
    for(const ffi_cb_args_t* callback = mjs->ffi_cb_args; callback; callback = callback->next) {
        mjs_memory_usage_add(&total, sizeof(*callback));
    }
    if(mjs->error_msg) mjs_memory_usage_add(&total, strlen(mjs->error_msg) + 1u);
    if(mjs->stack_trace) mjs_memory_usage_add(&total, strlen(mjs->stack_trace) + 1u);
    return total;
}

size_t mjs_call_depth(const struct mjs* mjs) {
    if(!mjs) return 0u;
    const size_t frames = mjs->call_stack.len / (sizeof(mjs_val_t) * CALL_STACK_FRAME_ITEMS_CNT);
    return frames > 0u ? frames - 1u : 0u;
}

size_t mjs_parser_depth(const struct mjs* mjs) {
    return mjs ? mjs->parser_depth : 0u;
}

void mjs_set_parser_depth_limit(struct mjs* mjs, size_t maximum_depth) {
    if(mjs) mjs->max_parser_depth = maximum_depth;
}

mjs_err_t mjs_set_errorf(struct mjs* mjs, mjs_err_t err, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    free(mjs->error_msg);
    mjs->error_msg = NULL;
    mjs->error = err;
    if(fmt != NULL) {
        mg_avprintf(&mjs->error_msg, 0, fmt, ap);
    }
    va_end(ap);
    return err;
}

void mjs_exit(struct mjs* mjs) {
    free(mjs->error_msg);
    mjs->error_msg = NULL;
    mjs->error = MJS_NEED_EXIT;
}

void mjs_set_exec_flags_poller(struct mjs* mjs, mjs_flags_poller_t poller) {
    mjs->exec_flags_poller = poller;
}

void mjs_set_debug_hook(struct mjs* mjs, mjs_debug_hook_t hook, void* context) {
    if(!mjs) return;
    mjs->debug_hook = hook;
    mjs->debug_hook_context = context;
    mjs->debug_last_bcode_offset = SIZE_MAX;
    mjs->debug_last_bcode_start = SIZE_MAX;
    mjs->debug_last_line = -1;
}

mjs_val_t mjs_debug_get_scope(struct mjs* mjs, size_t depth) {
    if(!mjs || depth >= mjs_stack_size(&mjs->scopes)) return MJS_UNDEFINED;
    return *vptr(&mjs->scopes, -1 - (int)depth);
}

void* mjs_get_context(struct mjs* mjs) {
    return mjs->context;
}

mjs_err_t mjs_prepend_errorf(struct mjs* mjs, mjs_err_t err, const char* fmt, ...) {
    char* old_error_msg = mjs->error_msg;
    char* new_error_msg = NULL;
    va_list ap;
    va_start(ap, fmt);

    /* err should never be MJS_OK here */
    assert(err != MJS_OK);

    mjs->error_msg = NULL;
    /* set error if only it wasn't already set to some error */
    if(mjs->error == MJS_OK) {
        mjs->error = err;
    }
    mg_avprintf(&new_error_msg, 0, fmt, ap);
    va_end(ap);

    if(old_error_msg != NULL) {
        mg_asprintf(&mjs->error_msg, 0, "%s: %s", new_error_msg, old_error_msg);
        free(new_error_msg);
        free(old_error_msg);
    } else {
        mjs->error_msg = new_error_msg;
    }
    return err;
}

void mjs_print_error(struct mjs* mjs, FILE* fp, const char* msg, int print_stack_trace) {
    (void)fp;

    if(print_stack_trace && mjs->stack_trace != NULL) {
        // fprintf(fp, "%s", mjs->stack_trace);
    }

    if(msg == NULL) {
        msg = "MJS error";
    }

    // fprintf(fp, "%s: %s\n", msg, mjs_strerror(mjs, mjs->error));
}

MJS_PRIVATE void mjs_die(struct mjs* mjs) {
    mjs_val_t msg_v = MJS_UNDEFINED;
    const char* msg = NULL;
    size_t msg_len = 0;

    /* get idx from arg 0 */
    if(!mjs_check_arg(mjs, 0, "msg", MJS_TYPE_STRING, &msg_v)) {
        goto clean;
    }

    msg = mjs_get_string(mjs, &msg_v, &msg_len);

    /* TODO(dfrank): take error type as an argument */
    mjs_prepend_errorf(mjs, MJS_TYPE_ERROR, "%.*s", (int)msg_len, msg);

clean:
    mjs_return(mjs, MJS_UNDEFINED);
}

const char* mjs_strerror(struct mjs* mjs, enum mjs_err err) {
    const char* err_names[] = {
        "NO_ERROR",
        "SYNTAX_ERROR",
        "REFERENCE_ERROR",
        "TYPE_ERROR",
        "OUT_OF_MEMORY",
        "INTERNAL_ERROR",
        "NOT_IMPLEMENTED",
        "FILE_OPEN_ERROR",
        "BAD_ARGUMENTS"};
    return mjs->error_msg == NULL || mjs->error_msg[0] == '\0' ? err_names[err] : mjs->error_msg;
}

const char* mjs_get_stack_trace(struct mjs* mjs) {
    return mjs->stack_trace;
}

MJS_PRIVATE size_t mjs_get_func_addr(mjs_val_t v) {
    const struct mjs_closure* closure = mjs_get_closure(v);
    return closure ? closure->address : v & ~MJS_TAG_MASK;
}

MJS_PRIVATE enum mjs_type mjs_get_type(mjs_val_t v) {
    int tag;
    if(mjs_is_number(v)) {
        return MJS_TYPE_NUMBER;
    }
    tag = (v & MJS_TAG_MASK) >> 48;
    switch(tag) {
    case MJS_TAG_FOREIGN >> 48:
        return MJS_TYPE_FOREIGN;
    case MJS_TAG_UNDEFINED >> 48:
        return MJS_TYPE_UNDEFINED;
    case MJS_TAG_OBJECT >> 48:
        return MJS_TYPE_OBJECT_GENERIC;
    case MJS_TAG_ARRAY >> 48:
        return MJS_TYPE_OBJECT_ARRAY;
    case MJS_TAG_FUNCTION >> 48:
    case MJS_TAG_FUNCTION_CLOSURE >> 48:
        return MJS_TYPE_OBJECT_FUNCTION;
    case MJS_TAG_STRING_I >> 48:
    case MJS_TAG_STRING_O >> 48:
    case MJS_TAG_STRING_F >> 48:
    case MJS_TAG_STRING_D >> 48:
    case MJS_TAG_STRING_5 >> 48:
        return MJS_TYPE_STRING;
    case MJS_TAG_BOOLEAN >> 48:
        return MJS_TYPE_BOOLEAN;
    case MJS_TAG_NULL >> 48:
        return MJS_TYPE_NULL;
    case MJS_TAG_ARRAY_BUF >> 48:
        return MJS_TYPE_ARRAY_BUF;
    case MJS_TAG_ARRAY_BUF_VIEW >> 48:
        return MJS_TYPE_ARRAY_BUF_VIEW;
    default:
        abort();
        return MJS_TYPE_UNDEFINED;
    }
}

mjs_val_t mjs_get_global(struct mjs* mjs) {
    return *vptr(&mjs->scopes, 0);
}

static void mjs_append_stack_trace_line(struct mjs* mjs, size_t offset) {
    if(offset != MJS_BCODE_OFFSET_EXIT) {
        const char* filename = mjs_get_bcode_filename_by_offset(mjs, offset);
        int line_no = mjs_get_lineno_by_offset(mjs, offset);
        char* new_line = NULL;
        const char* fmt = "\tat %s:%d\r\n";
        if(filename == NULL) {
            // fprintf(
            //     stderr,
            //     "ERROR during stack trace generation: wrong bcode offset %d\n",
            //     (int)offset);
            filename = "<unknown-filename>";
        }
        mg_asprintf(&new_line, 0, fmt, filename, line_no);

        if(mjs->stack_trace != NULL) {
            char* old = mjs->stack_trace;
            mg_asprintf(&mjs->stack_trace, 0, "%s%s", mjs->stack_trace, new_line);
            free(old);
            free(new_line);
        } else {
            mjs->stack_trace = new_line;
        }
    }
}

MJS_PRIVATE void mjs_gen_stack_trace(struct mjs* mjs, size_t offset) {
    mjs_append_stack_trace_line(mjs, offset);
    while(mjs->call_stack.len >= sizeof(mjs_val_t) * CALL_STACK_FRAME_ITEMS_CNT) {
        int i;

        /* set current offset to it to the offset stored in the frame */
        offset = mjs_get_int(mjs, *vptr(&mjs->call_stack, -1 - CALL_STACK_FRAME_ITEM_RETURN_ADDR));

        /* pop frame from the call stack */
        for(i = 0; i < CALL_STACK_FRAME_ITEMS_CNT; i++) {
            mjs_pop_val(&mjs->call_stack);
        }

        mjs_append_stack_trace_line(mjs, offset);
    }
}

void mjs_own(struct mjs* mjs, mjs_val_t* v) {
    mbuf_append(&mjs->owned_values, &v, sizeof(v));
}

int mjs_disown(struct mjs* mjs, mjs_val_t* v) {
    mjs_val_t** vp = (mjs_val_t**)(mjs->owned_values.buf + mjs->owned_values.len - sizeof(v));

    for(; (char*)vp >= mjs->owned_values.buf; vp--) {
        if(*vp == v) {
            *vp = *(mjs_val_t**)(mjs->owned_values.buf + mjs->owned_values.len - sizeof(v));
            mjs->owned_values.len -= sizeof(v);
            return 1;
        }
    }

    return 0;
}

/*
 * Returns position in the data stack at which the called function is located,
 * and which should be later replaced with the returned value.
 */
MJS_PRIVATE int mjs_getretvalpos(struct mjs* mjs) {
    int pos;
    mjs_val_t* ppos = vptr(&mjs->call_stack, -1);
    // LOG(LL_INFO, ("ppos: %p %d", ppos, mjs_stack_size(&mjs->call_stack)));
    assert(ppos != NULL && mjs_is_number(*ppos));
    pos = mjs_get_int(mjs, *ppos) - 1;
    assert(pos < (int)mjs_stack_size(&mjs->stack));
    return pos;
}

int mjs_nargs(struct mjs* mjs) {
    int top = mjs_stack_size(&mjs->stack);
    int pos = mjs_getretvalpos(mjs) + 1;
    // LOG(LL_INFO, ("top: %d pos: %d", top, pos));
    return pos > 0 && pos < top ? top - pos : 0;
}

mjs_val_t mjs_arg(struct mjs* mjs, int arg_index) {
    mjs_val_t res = MJS_UNDEFINED;
    int top = mjs_stack_size(&mjs->stack);
    int pos = mjs_getretvalpos(mjs) + 1;
    // LOG(LL_INFO, ("idx %d pos: %d", arg_index, pos));
    if(pos > 0 && pos + arg_index < top) {
        res = *vptr(&mjs->stack, pos + arg_index);
    }
    return res;
}

void mjs_return(struct mjs* mjs, mjs_val_t v) {
    int pos = mjs_getretvalpos(mjs);
    // LOG(LL_INFO, ("pos: %d", pos));
    mjs->stack.len = sizeof(mjs_val_t) * pos;
    mjs_push(mjs, v);
}

MJS_PRIVATE mjs_val_t vtop(struct mbuf* m) {
    size_t size = mjs_stack_size(m);
    return size > 0 ? *vptr(m, size - 1) : MJS_UNDEFINED;
}

MJS_PRIVATE size_t mjs_stack_size(const struct mbuf* m) {
    return m->len / sizeof(mjs_val_t);
}

MJS_PRIVATE mjs_val_t* vptr(struct mbuf* m, int idx) {
    int size = mjs_stack_size(m);
    if(idx < 0) idx = size + idx;
    return idx >= 0 && idx < size ? &((mjs_val_t*)m->buf)[idx] : NULL;
}

MJS_PRIVATE mjs_val_t mjs_pop(struct mjs* mjs) {
    if(mjs->stack.len == 0) {
        mjs_set_errorf(mjs, MJS_INTERNAL_ERROR, "stack underflow");
        return MJS_UNDEFINED;
    } else {
        return mjs_pop_val(&mjs->stack);
    }
}

MJS_PRIVATE void push_mjs_val(struct mbuf* m, mjs_val_t v) {
    mbuf_append(m, &v, sizeof(v));
}

MJS_PRIVATE mjs_val_t mjs_pop_val(struct mbuf* m) {
    mjs_val_t v = MJS_UNDEFINED;
    assert(m->len >= sizeof(v));
    if(m->len >= sizeof(v)) {
        memcpy(&v, m->buf + m->len - sizeof(v), sizeof(v));
        m->len -= sizeof(v);
    }
    return v;
}

MJS_PRIVATE void mjs_push(struct mjs* mjs, mjs_val_t v) {
    push_mjs_val(&mjs->stack, v);
}

void mjs_set_generate_jsc(struct mjs* mjs, int generate_jsc) {
    mjs->generate_jsc = generate_jsc;
}
