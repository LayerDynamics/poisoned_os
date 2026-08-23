#include "js_poison_storage.h"

#include <path.h>

#define JS_POISON_STORAGE_IO_MAX      (64u * 1024u)
#define JS_POISON_STORAGE_DIR_ENTRIES (64u)

typedef struct {
    Storage* storage;
    JsModules* modules;
} JsPoisonStorage;

typedef struct {
    File* file;
} JsPoisonFile;

static const JsValueDeclaration js_poison_storage_path_args_list[] = {
    JS_VALUE_SIMPLE(JsValueTypeString),
};
static const JsValueArguments js_poison_storage_path_args =
    JS_VALUE_ARGS(js_poison_storage_path_args_list);

static const JsValueDeclaration js_poison_storage_two_path_args_list[] = {
    JS_VALUE_SIMPLE(JsValueTypeString),
    JS_VALUE_SIMPLE(JsValueTypeString),
};
static const JsValueArguments js_poison_storage_two_path_args =
    JS_VALUE_ARGS(js_poison_storage_two_path_args_list);

static bool js_poison_storage_resolve(
    struct mjs* mjs,
    JsPoisonStorage* storage,
    const char* path,
    char resolved[JS_PROJECT_PATH_MAX]) {
    if(storage && js_modules_resolve_project_path(storage->modules, path, resolved)) return true;
    mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "path escapes managed project");
    mjs_return(mjs, MJS_UNDEFINED);
    return false;
}

static void js_poison_file_destructor(struct mjs* mjs, mjs_val_t object) {
    UNUSED(mjs);
    JsPoisonFile* instance = JS_GET_INST(mjs, object);
    if(!instance) return;
    if(instance->file) {
        if(storage_file_is_open(instance->file)) storage_file_close(instance->file);
        storage_file_free(instance->file);
    }
    free(instance);
}

static void js_poison_file_close(struct mjs* mjs) {
    JsPoisonFile* instance = JS_GET_CONTEXT(mjs);
    mjs_return(
        mjs,
        mjs_mk_boolean(mjs, instance && instance->file && storage_file_close(instance->file)));
}

static void js_poison_file_is_open(struct mjs* mjs) {
    JsPoisonFile* instance = JS_GET_CONTEXT(mjs);
    mjs_return(
        mjs,
        mjs_mk_boolean(mjs, instance && instance->file && storage_file_is_open(instance->file)));
}

static void js_poison_file_size(struct mjs* mjs) {
    JsPoisonFile* instance = JS_GET_CONTEXT(mjs);
    mjs_return(
        mjs,
        mjs_mk_number(mjs, instance && instance->file ? storage_file_size(instance->file) : 0u));
}

static void js_poison_file_read(struct mjs* mjs) {
    typedef enum {
        JsPoisonReadAscii,
        JsPoisonReadBinary,
    } JsPoisonReadMode;
    static const JsValueEnumVariant variants[] = {
        {"ascii", JsPoisonReadAscii},
        {"binary", JsPoisonReadBinary},
    };
    static const JsValueDeclaration args_list[] = {
        JS_VALUE_ENUM(JsPoisonReadMode, variants),
        JS_VALUE_SIMPLE(JsValueTypeInt32),
    };
    static const JsValueArguments args = JS_VALUE_ARGS(args_list);
    JsPoisonReadMode mode;
    int32_t requested;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &args, &mode, &requested);
    if(requested < 0 || (uint32_t)requested > JS_POISON_STORAGE_IO_MAX) {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "read exceeds managed I/O limit");
    }
    JsPoisonFile* instance = JS_GET_CONTEXT(mjs);
    if(!instance || !instance->file) JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "file closed");
    char* buffer = requested ? malloc((size_t)requested) : NULL;
    if(requested && !buffer) JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "allocation failed");
    const size_t count = requested ? storage_file_read(instance->file, buffer, requested) : 0u;
    mjs_val_t result = mode == JsPoisonReadAscii ?
                           mjs_mk_string(mjs, (const char*)buffer, count, true) :
                           mjs_mk_array_buf(mjs, buffer, count);
    free(buffer);
    mjs_return(mjs, result);
}

static void js_poison_file_write(struct mjs* mjs) {
    static const JsValueDeclaration args_list[] = {JS_VALUE_SIMPLE(JsValueTypeAny)};
    static const JsValueArguments args = JS_VALUE_ARGS(args_list);
    mjs_val_t value;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &args, &value);
    const void* bytes = NULL;
    size_t length = 0u;
    if(mjs_is_string(value)) {
        bytes = mjs_get_string(mjs, &value, &length);
    } else if(mjs_is_array_buf(value)) {
        bytes = mjs_array_buf_get_ptr(mjs, value, &length);
    } else {
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "expected string or ArrayBuffer");
    }
    if(length > JS_POISON_STORAGE_IO_MAX)
        JS_ERROR_AND_RETURN(mjs, MJS_BAD_ARGS_ERROR, "write exceeds managed I/O limit");
    JsPoisonFile* instance = JS_GET_CONTEXT(mjs);
    if(!instance || !instance->file) JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "file closed");
    mjs_return(mjs, mjs_mk_number(mjs, storage_file_write(instance->file, bytes, length)));
}

static void js_poison_storage_open_file(struct mjs* mjs) {
    static const JsValueEnumVariant access_variants[] = {
        {"r", FSAM_READ},
        {"w", FSAM_WRITE},
        {"rw", FSAM_READ_WRITE},
    };
    static const JsValueEnumVariant open_variants[] = {
        {"open_existing", FSOM_OPEN_EXISTING},
        {"open_always", FSOM_OPEN_ALWAYS},
        {"open_append", FSOM_OPEN_APPEND},
        {"create_new", FSOM_CREATE_NEW},
        {"create_always", FSOM_CREATE_ALWAYS},
    };
    static const JsValueDeclaration args_list[] = {
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_ENUM(FS_AccessMode, access_variants),
        JS_VALUE_ENUM(FS_OpenMode, open_variants),
    };
    static const JsValueArguments args = JS_VALUE_ARGS(args_list);
    const char* path;
    FS_AccessMode access;
    FS_OpenMode open;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &args, &path, &access, &open);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    JsPoisonFile* instance = calloc(1u, sizeof(*instance));
    if(!instance) JS_ERROR_AND_RETURN(mjs, MJS_INTERNAL_ERROR, "allocation failed");
    instance->file = storage_file_alloc(storage->storage);
    if(!instance->file || !storage_file_open(instance->file, resolved, access, open)) {
        if(instance->file) storage_file_free(instance->file);
        free(instance);
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    mjs_val_t object = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, object) {
        JS_FIELD(INST_PROP_NAME, mjs_mk_foreign(mjs, instance));
        JS_FIELD(MJS_DESTRUCTOR_PROP_NAME, MJS_MK_FN(js_poison_file_destructor));
        JS_FIELD("close", MJS_MK_FN(js_poison_file_close));
        JS_FIELD("isOpen", MJS_MK_FN(js_poison_file_is_open));
        JS_FIELD("read", MJS_MK_FN(js_poison_file_read));
        JS_FIELD("write", MJS_MK_FN(js_poison_file_write));
        JS_FIELD("size", MJS_MK_FN(js_poison_file_size));
    }
    mjs_return(mjs, object);
}

static void js_poison_storage_file_exists(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, storage_file_exists(storage->storage, resolved)));
}

static void js_poison_storage_directory_exists(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, storage_dir_exists(storage->storage, resolved)));
}

static void js_poison_storage_common_exists(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, storage_common_exists(storage->storage, resolved)));
}

static void js_poison_storage_make_directory(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, storage_simply_mkdir(storage->storage, resolved)));
}

static void js_poison_storage_read_directory(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    File* directory = storage_file_alloc(storage->storage);
    if(!directory || !storage_dir_open(directory, resolved)) {
        if(directory) storage_file_free(directory);
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    mjs_val_t result = mjs_mk_array(mjs);
    FileInfo info;
    char name[128];
    size_t count = 0u;
    while(count < JS_POISON_STORAGE_DIR_ENTRIES &&
          storage_dir_read(directory, &info, name, sizeof(name))) {
        mjs_val_t entry = mjs_mk_object(mjs);
        JS_ASSIGN_MULTI(mjs, entry) {
            JS_FIELD("path", mjs_mk_string(mjs, name, ~0, true));
            JS_FIELD("isDirectory", mjs_mk_boolean(mjs, file_info_is_dir(&info)));
            JS_FIELD("size", mjs_mk_number(mjs, info.size));
        }
        mjs_array_push(mjs, result, entry);
        ++count;
    }
    storage_file_free(directory);
    mjs_return(mjs, result);
}

static void js_poison_storage_stat(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    FileInfo info;
    uint32_t timestamp = 0u;
    if(storage_common_stat(storage->storage, resolved, &info) != FSE_OK ||
       storage_common_timestamp(storage->storage, resolved, &timestamp) != FSE_OK) {
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    mjs_val_t result = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, result) {
        JS_FIELD("path", mjs_mk_string(mjs, path, ~0, true));
        JS_FIELD("isDirectory", mjs_mk_boolean(mjs, file_info_is_dir(&info)));
        JS_FIELD("size", mjs_mk_number(mjs, info.size));
        JS_FIELD("accessTime", mjs_mk_number(mjs, timestamp));
    }
    mjs_return(mjs, result);
}

static void js_poison_storage_remove(struct mjs* mjs) {
    const char* path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_path_args, &path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, path, resolved)) return;
    mjs_return(mjs, mjs_mk_boolean(mjs, storage_simply_remove(storage->storage, resolved)));
}

static void js_poison_storage_rename(struct mjs* mjs) {
    const char *old_path, *new_path;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_poison_storage_two_path_args, &old_path, &new_path);
    JsPoisonStorage* storage = JS_GET_CONTEXT(mjs);
    char old_resolved[JS_PROJECT_PATH_MAX];
    char new_resolved[JS_PROJECT_PATH_MAX];
    if(!js_poison_storage_resolve(mjs, storage, old_path, old_resolved) ||
       !js_poison_storage_resolve(mjs, storage, new_path, new_resolved)) {
        return;
    }
    mjs_return(
        mjs,
        mjs_mk_boolean(
            mjs, storage_common_rename(storage->storage, old_resolved, new_resolved) == FSE_OK));
}

void* js_poison_storage_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules) {
    if(!js_modules_is_managed(modules)) return NULL;
    JsPoisonStorage* storage = calloc(1u, sizeof(*storage));
    if(!storage) return NULL;
    storage->storage = furi_record_open(RECORD_STORAGE);
    storage->modules = modules;
    *object = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, *object) {
        JS_FIELD(INST_PROP_NAME, mjs_mk_foreign(mjs, storage));
        JS_FIELD("openFile", MJS_MK_FN(js_poison_storage_open_file));
        JS_FIELD("fileExists", MJS_MK_FN(js_poison_storage_file_exists));
        JS_FIELD("directoryExists", MJS_MK_FN(js_poison_storage_directory_exists));
        JS_FIELD("fileOrDirExists", MJS_MK_FN(js_poison_storage_common_exists));
        JS_FIELD("makeDirectory", MJS_MK_FN(js_poison_storage_make_directory));
        JS_FIELD("readDirectory", MJS_MK_FN(js_poison_storage_read_directory));
        JS_FIELD("stat", MJS_MK_FN(js_poison_storage_stat));
        JS_FIELD("remove", MJS_MK_FN(js_poison_storage_remove));
        JS_FIELD("rename", MJS_MK_FN(js_poison_storage_rename));
    }
    return storage;
}

void js_poison_storage_destroy(void* data) {
    JsPoisonStorage* storage = data;
    if(!storage) return;
    furi_record_close(RECORD_STORAGE);
    free(storage);
}
