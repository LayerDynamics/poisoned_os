#include "../test.h"
#include "../../../../services/poison_packages/poison_package_archive.h"
#include "../../../../services/poison_packages/poison_package_authority.h"
#include "../../../../services/poison_packages/poison_package_manager.h"
#include "../../../../services/poison_packages/poison_package_signature.h"
#include "../../../../services/poison_packages/poison_package_verify.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

MU_TEST(poison_package_verifier_rejects_tamper_revocation_and_downgrade) {
    const char* digest = "0000000000000000000000000000000000000000000000000000000000000000";
    mu_check(
        poison_package_verify_manifest(
            "org.example", "2.0.0", "app.fap", digest, "key", false, "1.0.0") ==
        PoisonPackageVerifyOk);
    mu_check(
        poison_package_verify_manifest(
            "org.example", "2.0.0", "app.fap", "bad", "key", false, "1.0.0") ==
        PoisonPackageVerifyInvalid);
    mu_check(
        poison_package_verify_manifest(
            "org.example", "2.0.0", "app.fap", digest, "key", true, "1.0.0") ==
        PoisonPackageVerifyRevoked);
    mu_check(
        poison_package_verify_manifest(
            "org.example", "1.0.0", "app.fap", digest, "key", false, "2.0.0") ==
        PoisonPackageVerifyDowngrade);
    mu_check(
        poison_package_verify_manifest(
            "org.example", "2.0.0", "../app.fap", digest, "key", false, "1.0.0") ==
        PoisonPackageVerifyInvalid);
    mu_check(
        poison_package_verify_manifest(
            "org.example", "10.0.0", "app.fap", digest, "key", false, "2.0.0") ==
        PoisonPackageVerifyOk);
}

MU_TEST(poison_package_verifier_accepts_p256_signature_and_rejects_tamper) {
    static const uint8_t public_key[65] = {
        0x04, 0xd0, 0xb1, 0x35, 0x77, 0x9f, 0xf2, 0x66, 0x93, 0xc4, 0xcd, 0x25, 0x7a,
        0x41, 0x2d, 0xbf, 0x45, 0xad, 0x3f, 0x2d, 0x48, 0xbe, 0x2f, 0x3d, 0x25, 0xa1,
        0x7f, 0xd5, 0xa6, 0x3b, 0xa0, 0xeb, 0x4d, 0xce, 0x42, 0xc8, 0x9e, 0xe5, 0x42,
        0x42, 0x98, 0x1b, 0xf5, 0xfc, 0x11, 0x0b, 0xe5, 0x05, 0x3f, 0x07, 0xc3, 0x1a,
        0x97, 0xe3, 0x2e, 0x9d, 0x9a, 0xb6, 0xf2, 0xb2, 0xf0, 0x66, 0x96, 0x66, 0x75,
    };
    static const uint8_t signature[] = {
        0x30, 0x46, 0x02, 0x21, 0x00, 0xbb, 0x76, 0xae, 0x39, 0x3c, 0xea, 0xc2, 0x6b, 0xa9, 0xf5,
        0xad, 0x56, 0xce, 0xec, 0x98, 0xb0, 0x38, 0x04, 0x12, 0x3a, 0x89, 0x2c, 0x6c, 0x4c, 0x50,
        0x37, 0x58, 0xbd, 0x9f, 0xe8, 0xfa, 0x6d, 0x02, 0x21, 0x00, 0x90, 0x7c, 0xcb, 0x99, 0xac,
        0x1e, 0x54, 0x0f, 0x6f, 0x3d, 0xe6, 0x38, 0x04, 0x1d, 0x5b, 0x69, 0x40, 0x04, 0xfd, 0x8c,
        0x55, 0x32, 0x71, 0xac, 0xb2, 0x00, 0x90, 0x73, 0x71, 0x2a, 0x53, 0x51,
    };
    uint8_t manifest[] = "{\"id\":\"org.poison.test\"}\n";
    mu_check(
        poison_package_verify_p256_signature(
            public_key, manifest, sizeof(manifest) - 1u, signature, sizeof(signature)) ==
        PoisonPackageSignatureOk);
    manifest[7] ^= 1u;
    mu_check(
        poison_package_verify_p256_signature(
            public_key, manifest, sizeof(manifest) - 1u, signature, sizeof(signature)) ==
        PoisonPackageSignatureInvalid);
}

MU_TEST(poison_package_authority_store_rejects_duplicates_and_revoked_keys) {
    PoisonPackageAuthorityStore store;
    poison_package_authority_store_init(&store);
    const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES] = {
        0x04, 0xd0, 0xb1, 0x35, 0x77, 0x9f, 0xf2, 0x66, 0x93, 0xc4, 0xcd, 0x25, 0x7a,
        0x41, 0x2d, 0xbf, 0x45, 0xad, 0x3f, 0x2d, 0x48, 0xbe, 0x2f, 0x3d, 0x25, 0xa1,
        0x7f, 0xd5, 0xa6, 0x3b, 0xa0, 0xeb, 0x4d, 0xce, 0x42, 0xc8, 0x9e, 0xe5, 0x42,
        0x42, 0x98, 0x1b, 0xf5, 0xfc, 0x11, 0x0b, 0xe5, 0x05, 0x3f, 0x07, 0xc3, 0x1a,
        0x97, 0xe3, 0x2e, 0x9d, 0x9a, 0xb6, 0xf2, 0xb2, 0xf0, 0x66, 0x96, 0x66, 0x75,
    };
    mu_check(poison_package_authority_store_add(&store, "package-prod-1", public_key, false));
    mu_check(!poison_package_authority_store_add(&store, "package-prod-1", public_key, false));
    const PoisonPackageAuthority* authority =
        poison_package_authority_store_find(&store, "package-prod-1");
    mu_check(authority);
    mu_check(!authority->revoked);
    mu_check(poison_package_authority_store_revoke(&store, "package-prod-1"));
    mu_check(!poison_package_authority_store_find(&store, "package-prod-1"));
}

MU_TEST(poison_package_authority_store_round_trips_bounded_binary_format) {
    PoisonPackageAuthorityStore source;
    poison_package_authority_store_init(&source);
    const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES] = {
        0x04, 0xd0, 0xb1, 0x35, 0x77, 0x9f, 0xf2, 0x66, 0x93, 0xc4, 0xcd, 0x25, 0x7a,
        0x41, 0x2d, 0xbf, 0x45, 0xad, 0x3f, 0x2d, 0x48, 0xbe, 0x2f, 0x3d, 0x25, 0xa1,
        0x7f, 0xd5, 0xa6, 0x3b, 0xa0, 0xeb, 0x4d, 0xce, 0x42, 0xc8, 0x9e, 0xe5, 0x42,
        0x42, 0x98, 0x1b, 0xf5, 0xfc, 0x11, 0x0b, 0xe5, 0x05, 0x3f, 0x07, 0xc3, 0x1a,
        0x97, 0xe3, 0x2e, 0x9d, 0x9a, 0xb6, 0xf2, 0xb2, 0xf0, 0x66, 0x96, 0x66, 0x75,
    };
    mu_check(poison_package_authority_store_add(&source, "package-prod-1", public_key, false));
    uint8_t encoded[POISON_PACKAGE_AUTHORITY_FILE_MAX];
    const size_t encoded_size =
        poison_package_authority_store_encode(&source, encoded, sizeof(encoded));
    mu_check(encoded_size > 0u);

    PoisonPackageAuthorityStore decoded;
    poison_package_authority_store_init(&decoded);
    mu_check(poison_package_authority_store_decode(&decoded, encoded, encoded_size));
    mu_check(poison_package_authority_store_find(&decoded, "package-prod-1"));
    encoded[0] ^= 1u;
    mu_check(!poison_package_authority_store_decode(&decoded, encoded, encoded_size));
    mu_check(poison_package_authority_store_count(&decoded) == 0u);
}

static uint8_t poison_package_test_hex_nibble(char value) {
    if(value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if(value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    return 0xffu;
}

static bool
    poison_package_test_sha256_hex(const uint8_t* input, size_t input_size, char output[65u]) {
    uint8_t digest[32u];
    if(mbedtls_sha256(input, input_size, digest, 0) != 0) return false;
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < sizeof(digest); ++index) {
        output[index * 2u] = hex[digest[index] >> 4u];
        output[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    output[64u] = '\0';
    memset(digest, 0, sizeof(digest));
    return true;
}

MU_TEST(poison_package_archive_verifies_signed_members_and_rejects_tamper) {
    static const char archive_hex[] =
        "504b03041400000000000000210051cc584127020000270200000d0000006d616e69666573742e6a736f6e7b226361706162696c6974696573223a5b2273746f726167652e70726f6a6563742e72656164225d2c22636f6e74656e74536861323536223a2261363961636434653035336338663165383037353833346133373362383432623135643733393230616466623034303032313038323630393239336561326136222c22636f6e74656e7454797065223a226170706c69636174696f6e222c22656e747279706f696e74223a226170702e666170222c226669726d77617265417069223a223e3d38382e302e30203c38392e302e30222c226964223a226f72672e706f69736f6e2e61726368697665222c227061636b616765466f726d6174223a312c227061796c6f616473223a5b7b2270617468223a226170702e666170222c22736861323536223a2233343966396364303535363861316466383737363030613233626237323963646263343836313661356137363033343565333363323332333462333939646262222c2273697a65223a387d5d2c2272656c6561736553657175656e6365223a312c227369676e6174757265223a224d455543495143556e6a696c7a65746d54356c65382b342b30496f4939693477777153725868596538686558725155634c5149674e6a2b6d4453506242436c42553441306a544e506777396876727a3975594e6f4941474948346479754c633d222c227369676e696e674b65794964223a227061636b6167652d746573742d31222c2276657273696f6e223a22312e302e30227d0a"
        "504b030414000000000000002100d21c5e730800000008000000070000006170702e6661707361666520617070"
        "504b010214031400000000000000210051cc584127020000270200000d0000000000000000000000a481000000006d616e69666573742e6a736f6e"
        "504b0102140314000000000000002100d21c5e730800000008000000070000000000000000000000a481520200006170702e666170"
        "504b05060000000002000200700000007f0200000000";
    static const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES] = {
        0x04, 0x4c, 0xea, 0x5a, 0xb8, 0x76, 0xfb, 0xc2, 0xe9, 0xfa, 0xce, 0x32, 0x1e,
        0xba, 0x37, 0x66, 0xa6, 0xf5, 0x52, 0xa2, 0xbf, 0x7c, 0x25, 0x76, 0xd7, 0x99,
        0x34, 0xe8, 0x36, 0xe3, 0xe3, 0x20, 0x4d, 0xa1, 0x92, 0x17, 0x4b, 0x2c, 0x95,
        0x22, 0x56, 0x72, 0xb8, 0x7c, 0xf0, 0x5f, 0x28, 0x9c, 0xd2, 0x85, 0x79, 0x5a,
        0xd5, 0x86, 0x86, 0x15, 0xde, 0xce, 0x34, 0x22, 0xc8, 0x0b, 0x86, 0xad, 0x4a,
    };
    const size_t archive_size = (sizeof(archive_hex) - 1u) / 2u;
    uint8_t* archive = malloc(archive_size);
    mu_check(archive);
    for(size_t index = 0; index < archive_size; ++index) {
        const uint8_t high = poison_package_test_hex_nibble(archive_hex[index * 2u]);
        const uint8_t low = poison_package_test_hex_nibble(archive_hex[index * 2u + 1u]);
        mu_check(high != 0xffu && low != 0xffu);
        archive[index] = (uint8_t)((high << 4u) | low);
    }

    const char* path = EXT_PATH(".tmp/poison-package-archive.poison");
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_mkdir(storage, EXT_PATH(".tmp"));
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    PoisonPackageAuthorityStore authorities;
    poison_package_authority_store_init(&authorities);
    PoisonPackageVerifiedArchive verified;
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveUnknownSigner);
    mu_check(poison_package_authority_store_add(&authorities, "package-test-1", public_key, true));
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveRevokedSigner);
    poison_package_authority_store_init(&authorities);
    mu_check(
        poison_package_authority_store_add(&authorities, "package-test-1", public_key, false));
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            87u,
            0u,
            NULL,
            &verified) == PoisonPackageArchiveIncompatible);
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            88u,
            46u,
            "2.0.0",
            &verified) == PoisonPackageArchiveDowngrade);
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveOk);
    mu_check(strcmp(verified.package_id, "org.poison.archive") == 0);
    mu_check(strcmp(verified.version, "1.0.0") == 0);
    mu_check(verified.payload_count == 1u);
    mu_check(verified.capability_mask == (1u << 3));
    mu_check(verified.capability_count == 1u);
    mu_check(strcmp(verified.capabilities[0], "storage.project.read") == 0);

    const char* extracted_root = EXT_PATH(".tmp/poison-package-extracted");
    mu_check(poison_package_extract_verified_archive(path, &verified, extracted_root));
    PoisonPackageVerifiedArchive installed;
    mu_check(
        poison_package_verify_installed_manifest(
            extracted_root, &authorities, 88u, 46u, &installed) == PoisonPackageArchiveOk);
    mu_check(strcmp(installed.package_id, verified.package_id) == 0);
    mu_check(strcmp(installed.content_sha256, verified.content_sha256) == 0);
    mu_check(installed.capability_count == 1u);
    mu_check(strcmp(installed.capabilities[0], "storage.project.read") == 0);
    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    mu_check(storage_file_open(
        file, EXT_PATH(".tmp/poison-package-extracted/app.fap"), FSAM_READ, FSOM_OPEN_EXISTING));
    uint8_t extracted[8u] = {0};
    mu_check(storage_file_read(file, extracted, sizeof(extracted)) == sizeof(extracted));
    mu_check(memcmp(extracted, "safe app", sizeof(extracted)) == 0);
    storage_file_free(file);
    mu_check(storage_simply_remove_recursive(storage, extracted_root));
    furi_record_close(RECORD_STORAGE);

    PoisonPackageStorageLayout layout;
    mu_check(poison_package_storage_layout_init(
        &layout, EXT_PATH(".tmp/poison-managed"), EXT_PATH(".tmp/poison-active")));
    storage = furi_record_open(RECORD_STORAGE);
    (void)storage_simply_remove_recursive(storage, layout.managed_root);
    (void)storage_simply_remove_recursive(storage, layout.active_root);
    furi_record_close(RECORD_STORAGE);
    mu_check(poison_package_storage_stage(&layout, path, &verified));
    mu_check(poison_package_storage_activate(&layout, verified.package_id));
    mu_check(poison_package_storage_report_health(&layout, verified.package_id, true));
    mu_check(poison_package_storage_stage(&layout, path, &verified));
    mu_check(poison_package_storage_activate(&layout, verified.package_id));
    mu_check(poison_package_storage_rollback(&layout, verified.package_id));
    mu_check(poison_package_storage_report_health(&layout, verified.package_id, false));
    storage = furi_record_open(RECORD_STORAGE);
    FileInfo installed_info;
    mu_check(
        storage_common_stat(
            storage, EXT_PATH(".tmp/poison-active/org.poison.archive/app.fap"), &installed_info) ==
        FSE_OK);
    mu_check(
        storage_common_stat(
            storage,
            EXT_PATH(".tmp/poison-managed/org.poison.archive/quarantine/app.fap"),
            &installed_info) == FSE_OK);
    mu_check(storage_simply_remove_recursive(storage, layout.managed_root));
    mu_check(storage_simply_remove_recursive(storage, layout.active_root));
    furi_record_close(RECORD_STORAGE);

    archive[8u] = 8u;
    char compressed_digest[65u];
    mu_check(poison_package_test_sha256_hex(archive, archive_size, compressed_digest));
    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    mu_check(
        poison_package_verify_archive(
            path, compressed_digest, &authorities, 88u, 46u, NULL, &verified) ==
        PoisonPackageArchiveInvalid);

    archive[8u] = 0u;
    archive[100u] ^= 0x80u;
    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    mu_check(
        poison_package_verify_archive(
            path,
            "a42b68ff04769ae93455775b8eab1a6162166341c8ace876be750091a33ef8dd",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveDigestMismatch);

    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_simply_remove(storage, path));
    furi_record_close(RECORD_STORAGE);
    memset(archive, 0, archive_size);
    free(archive);
}

MU_TEST(poison_package_archive_rejects_duplicate_zip_members) {
    static const char archive_hex[] =
        "504b03041400000000000000210006b0a1dd03000000030000000d0000006d616e69666573742e6a736f6e7b7d0a"
        "504b03041400000000000000210006b0a1dd03000000030000000d0000006d616e69666573742e6a736f6e7b7d0a"
        "504b010214031400000000000000210006b0a1dd03000000030000000d0000000000000000000000a4812e0000006d616e69666573742e6a736f6e"
        "504b010214031400000000000000210006b0a1dd03000000030000000d0000000000000000000000a4812e0000006d616e69666573742e6a736f6e"
        "504b05060000000002000200760000005c0000000000";
    const size_t archive_size = (sizeof(archive_hex) - 1u) / 2u;
    uint8_t* archive = malloc(archive_size);
    mu_check(archive);
    for(size_t index = 0u; index < archive_size; ++index) {
        const uint8_t high = poison_package_test_hex_nibble(archive_hex[index * 2u]);
        const uint8_t low = poison_package_test_hex_nibble(archive_hex[index * 2u + 1u]);
        mu_check(high != 0xffu && low != 0xffu);
        archive[index] = (uint8_t)((high << 4u) | low);
    }
    const char* path = EXT_PATH(".tmp/poison-package-duplicate.poison");
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_mkdir(storage, EXT_PATH(".tmp"));
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    PoisonPackageAuthorityStore authorities;
    poison_package_authority_store_init(&authorities);
    PoisonPackageVerifiedArchive verified;
    mu_check(
        poison_package_verify_archive(
            path,
            "f8f8350c19914e6577726f7293cd8adb361edc9e853024fe111a0799b2af558f",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveInvalid);

    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_simply_remove(storage, path));
    furi_record_close(RECORD_STORAGE);
    memset(archive, 0, archive_size);
    free(archive);
}

MU_TEST(poison_package_archive_verifies_signed_firmware_contract) {
    static const char archive_hex[] =
        "504b03041400000000000000210014f77b941c0200001c0200000d0000006d616e69666573742e6a736f6e7b226361706162696c6974696573223a5b5d2c22636f6e74656e74536861323536223a2239303661636330633039343461646437376235666464613562363166393361643466333538666331626233663663393035393130663963353664303135323234222c22636f6e74656e7454797065223a226669726d77617265222c22656e747279706f696e74223a227570646174652e667566222c226669726d77617265417069223a223e3d38382e302e30203c38392e302e30222c226964223a226f72672e706f69736f6e2e6669726d776172652d74657374222c227061636b616765466f726d6174223a312c227061796c6f616473223a5b7b2270617468223a227570646174652e667566222c22736861323536223a2239333364373062316231643031613138623932386139373633393663643165306639623533316136623563393632343665326632633763623664346338343731222c2273697a65223a31367d5d2c2272656c6561736553657175656e6365223a322c227369676e6174757265223a224d455543495143335562464b34692b2f6d704b394262675041524835556d4973627a36685658684b446e52543439484a34414967654246496e67497a577447566b4169346372756c416a34683731792f7053684d635a4148565538334259733d222c227369676e696e674b65794964223a226669726d776172652d746573742d31222c2276657273696f6e223a22322e302e30227d0a"
        "504b03041400000000000000210004e8ff6410000000100000000a0000007570646174652e6675667369676e6564206669726d776172650a"
        "504b010214031400000000000000210014f77b941c0200001c0200000d0000000000000000000000a481000000006d616e69666573742e6a736f6e"
        "504b010214031400000000000000210004e8ff6410000000100000000a0000000000000000000000a481470200007570646174652e667566"
        "504b05060000000002000200730000007f0200000000";
    static const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES] = {
        0x04, 0x70, 0x26, 0x75, 0x2d, 0x46, 0x20, 0xfe, 0xa4, 0x02, 0x64, 0xe1, 0x3a,
        0x0a, 0x8c, 0x22, 0xcc, 0x29, 0xfb, 0x5c, 0xdf, 0xd7, 0xb6, 0x8f, 0xa8, 0xf9,
        0x96, 0xb7, 0xb3, 0x54, 0x25, 0xd5, 0x12, 0xb5, 0xe6, 0xfb, 0xe1, 0xbb, 0x06,
        0x5a, 0x5c, 0xca, 0x14, 0xa9, 0xd4, 0xe0, 0x9c, 0x81, 0x5e, 0x0c, 0xc7, 0x93,
        0x86, 0x91, 0x22, 0x0e, 0xd5, 0xe1, 0x9e, 0x0b, 0xa4, 0x1b, 0x95, 0x50, 0xe9,
    };
    const size_t archive_size = (sizeof(archive_hex) - 1u) / 2u;
    uint8_t* archive = malloc(archive_size);
    mu_check(archive);
    for(size_t index = 0; index < archive_size; ++index) {
        const uint8_t high = poison_package_test_hex_nibble(archive_hex[index * 2u]);
        const uint8_t low = poison_package_test_hex_nibble(archive_hex[index * 2u + 1u]);
        mu_check(high != 0xffu && low != 0xffu);
        archive[index] = (uint8_t)((high << 4u) | low);
    }

    const char* path = EXT_PATH(".tmp/poison-firmware-archive.poison");
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_mkdir(storage, EXT_PATH(".tmp"));
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    PoisonPackageAuthorityStore authorities;
    poison_package_authority_store_init(&authorities);
    mu_check(
        poison_package_authority_store_add(&authorities, "firmware-test-1", public_key, false));
    PoisonPackageVerifiedArchive verified;
    mu_check(
        poison_package_verify_archive(
            path,
            "da6029e32e0a492b27097fa25d0b78d47f5b0a89e727db5a8b2526e268d55fe2",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveOk);
    mu_check(strcmp(verified.content_type, "firmware") == 0);
    mu_check(strcmp(verified.package_id, "org.poison.firmware-test") == 0);
    mu_check(strcmp(verified.entrypoint, "update.fuf") == 0);
    mu_check(verified.release_sequence == 2u);
    mu_check(verified.payload_count == 1u);
    mu_check(verified.payloads[0].size == 16u);

    archive[archive_size - 23u] ^= 1u;
    storage = furi_record_open(RECORD_STORAGE);
    file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, archive, archive_size) == archive_size);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    mu_check(
        poison_package_verify_archive(
            path,
            "da6029e32e0a492b27097fa25d0b78d47f5b0a89e727db5a8b2526e268d55fe2",
            &authorities,
            88u,
            46u,
            NULL,
            &verified) == PoisonPackageArchiveDigestMismatch);

    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_simply_remove(storage, path));
    furi_record_close(RECORD_STORAGE);
    memset(archive, 0, archive_size);
    free(archive);
}

MU_TEST_SUITE(poison_package_verify_suite) {
    MU_RUN_TEST(poison_package_verifier_rejects_tamper_revocation_and_downgrade);
    MU_RUN_TEST(poison_package_verifier_accepts_p256_signature_and_rejects_tamper);
    MU_RUN_TEST(poison_package_authority_store_rejects_duplicates_and_revoked_keys);
    MU_RUN_TEST(poison_package_authority_store_round_trips_bounded_binary_format);
    MU_RUN_TEST(poison_package_archive_verifies_signed_members_and_rejects_tamper);
    MU_RUN_TEST(poison_package_archive_rejects_duplicate_zip_members);
    MU_RUN_TEST(poison_package_archive_verifies_signed_firmware_contract);
}
void poison_package_verify_run_tests(void) {
    MU_RUN_SUITE(poison_package_verify_suite);
}
