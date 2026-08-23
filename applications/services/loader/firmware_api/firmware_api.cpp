#include "firmware_api.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/crc32_calc.h>

#include <cstring>

#include <firmware_api_table.h>

#define TAG "FirmwareApi"

#define FIRMWARE_API_TABLE_PATH            EXT_PATH("firmware_api.bin")
#define FIRMWARE_API_TABLE_FORMAT          1U
#define FIRMWARE_API_TABLE_MAX_ENTRIES     8192U
#define FIRMWARE_API_TABLE_CRC_BUFFER_SIZE 256U

struct __attribute__((packed)) FirmwareApiTableHeader {
    uint8_t magic[4];
    uint16_t format;
    uint16_t header_size;
    uint32_t api_version;
    uint32_t entry_count;
    uint32_t entries_crc32;
};

struct __attribute__((packed)) FirmwareApiTableEntry {
    uint32_t hash;
    uint32_t address;
};

struct FirmwareApiTableContext {
    FuriMutex* mutex;
    Storage* storage;
    File* file;
    uint32_t entry_count;
    bool valid;
    bool failure_reported;
};

static FirmwareApiTableContext firmware_api_context{};

static void firmware_api_close_table() {
    if(storage_file_is_open(firmware_api_context.file)) {
        storage_file_close(firmware_api_context.file);
    }
    firmware_api_context.entry_count = 0;
    firmware_api_context.valid = false;
}

static bool
    firmware_api_validate_entries(const FirmwareApiTableHeader& header, uint32_t* calculated_crc) {
    uint8_t buffer[FIRMWARE_API_TABLE_CRC_BUFFER_SIZE];
    uint32_t remaining = header.entry_count * sizeof(FirmwareApiTableEntry);
    uint32_t crc = 0;

    if(!storage_file_seek(firmware_api_context.file, header.header_size, true)) {
        return false;
    }

    while(remaining) {
        const size_t chunk_size = MIN(remaining, sizeof(buffer));
        if(storage_file_read(firmware_api_context.file, buffer, chunk_size) != chunk_size) {
            return false;
        }
        crc = crc32_calc_buffer(crc, buffer, chunk_size);
        remaining -= chunk_size;
    }

    *calculated_crc = crc;
    return true;
}

static bool firmware_api_open_table() {
    firmware_api_close_table();

    if(!storage_file_open(
           firmware_api_context.file, FIRMWARE_API_TABLE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        return false;
    }

    FirmwareApiTableHeader header;
    uint32_t calculated_crc;
    const uint64_t file_size = storage_file_size(firmware_api_context.file);

    const bool header_read =
        storage_file_read(firmware_api_context.file, &header, sizeof(header)) == sizeof(header);
    const bool header_valid =
        header_read && std::memcmp(header.magic, "FAPI", sizeof(header.magic)) == 0 &&
        header.format == FIRMWARE_API_TABLE_FORMAT && header.header_size == sizeof(header) &&
        header.api_version == static_cast<uint32_t>(elf_api_version) && header.entry_count > 0 &&
        header.entry_count <= FIRMWARE_API_TABLE_MAX_ENTRIES;

    if(!header_valid) {
        firmware_api_close_table();
        return false;
    }

    const uint64_t validated_expected_size =
        sizeof(header) + static_cast<uint64_t>(header.entry_count) * sizeof(FirmwareApiTableEntry);
    if(file_size != validated_expected_size ||
       !firmware_api_validate_entries(header, &calculated_crc) ||
       calculated_crc != header.entries_crc32) {
        firmware_api_close_table();
        return false;
    }

    firmware_api_context.entry_count = header.entry_count;
    firmware_api_context.valid = true;
    firmware_api_context.failure_reported = false;
    return true;
}

static bool
    firmware_api_resolve(const ElfApiInterface* interface, uint32_t hash, Elf32_Addr* address) {
    furi_check(interface);
    furi_check(address);

    furi_mutex_acquire(firmware_api_context.mutex, FuriWaitForever);

    bool result = false;
    if(!firmware_api_context.valid && !firmware_api_open_table()) {
        if(!firmware_api_context.failure_reported) {
            FURI_LOG_E(TAG, "Cannot validate %s", FIRMWARE_API_TABLE_PATH);
            firmware_api_context.failure_reported = true;
        }
        furi_mutex_release(firmware_api_context.mutex);
        return false;
    }

    uint32_t left = 0;
    uint32_t right = firmware_api_context.entry_count;
    while(left < right) {
        const uint32_t middle = left + (right - left) / 2;
        const uint32_t offset =
            sizeof(FirmwareApiTableHeader) + middle * sizeof(FirmwareApiTableEntry);
        FirmwareApiTableEntry entry;
        if(!storage_file_seek(firmware_api_context.file, offset, true) ||
           storage_file_read(firmware_api_context.file, &entry, sizeof(entry)) != sizeof(entry)) {
            firmware_api_close_table();
            break;
        }

        if(entry.hash < hash) {
            left = middle + 1;
        } else if(entry.hash > hash) {
            right = middle;
        } else {
            *address = entry.address;
            result = true;
            break;
        }
    }

    furi_mutex_release(firmware_api_context.mutex);
    return result;
}

extern "C" void firmware_api_init(void) {
    furi_check(!firmware_api_context.mutex);
    firmware_api_context.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    firmware_api_context.storage = static_cast<Storage*>(furi_record_open(RECORD_STORAGE));
    firmware_api_context.file = storage_file_alloc(firmware_api_context.storage);
}

constexpr ElfApiInterface elf_api_interface{
    .api_version_major = (elf_api_version >> 16),
    .api_version_minor = (elf_api_version & 0xFFFF),
    .resolver_callback = &firmware_api_resolve,
};
const ElfApiInterface* const firmware_api_interface = &elf_api_interface;

extern "C" void furi_hal_info_get_api_version(uint16_t* major, uint16_t* minor) {
    *major = firmware_api_interface->api_version_major;
    *minor = firmware_api_interface->api_version_minor;
}
