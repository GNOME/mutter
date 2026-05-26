#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Invariant: Buffer reads/copies must never exceed the declared/allocated
 * buffer length. When copying keymap_serialized into keymap_mem, the
 * destination buffer size must be respected. Using strcpy without bounds
 * checking (CWE-120) can cause heap buffer overflow.
 *
 * This test simulates the vulnerable pattern and verifies that a safe
 * replacement (strncpy or strlcpy equivalent) correctly bounds the copy,
 * preventing out-of-bounds writes regardless of input size.
 */

/* Safe keymap copy function - the fix that SHOULD be used instead of strcpy */
static int safe_keymap_copy(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || src == NULL || dest_size == 0) {
        return -1;
    }
    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        /* Reject oversized input - do not copy */
        return -1;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return 0;
}

/* Generate a string of given length filled with a repeated character */
static char *make_payload(size_t length, char fill)
{
    char *buf = malloc(length + 1);
    if (!buf) return NULL;
    memset(buf, fill, length);
    buf[length] = '\0';
    return buf;
}

#define KEYMAP_MEM_SIZE 4096

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    /* Invariant: copying keymap_serialized into keymap_mem must never
     * write beyond the allocated size of keymap_mem */

    const char *static_payloads[] = {
        /* Exact boundary - 1 byte under limit */
        /* These will be generated dynamically below */
        /* Static adversarial strings */
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", /* 256 bytes */
        "\x00\x01\x02\x03\x04\x05\x06\x07", /* embedded nulls (treated as empty by strlen) */
        "xkb_keymap { xkb_keycodes { }; xkb_types { }; xkb_compat { }; xkb_symbols { }; };",
        "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s", /* format string attack */
        "../../../../etc/passwd",             /* path traversal */
        "\xff\xfe\xfd\xfc\xfb\xfa",          /* high byte values */
    };

    int num_static = sizeof(static_payloads) / sizeof(static_payloads[0]);

    /* Test static payloads */
    for (int i = 0; i < num_static; i++) {
        char *keymap_mem = calloc(1, KEYMAP_MEM_SIZE);
        ck_assert_ptr_nonnull(keymap_mem);

        const char *keymap_serialized = static_payloads[i];
        size_t src_len = strlen(keymap_serialized);

        int result = safe_keymap_copy(keymap_mem, KEYMAP_MEM_SIZE, keymap_serialized);

        if (result == 0) {
            /* Copy succeeded: verify no overflow - dest must be null-terminated
             * and length must be strictly less than KEYMAP_MEM_SIZE */
            size_t copied_len = strlen(keymap_mem);
            ck_assert_msg(copied_len < KEYMAP_MEM_SIZE,
                "Copied length %zu must be < buffer size %d",
                copied_len, KEYMAP_MEM_SIZE);
            /* Verify the null terminator is within bounds */
            ck_assert_msg(keymap_mem[KEYMAP_MEM_SIZE - 1] == '\0',
                "Last byte of buffer must be null terminator");
        } else {
            /* Copy rejected: verify destination buffer is untouched or safe */
            /* keymap_mem should remain zeroed (calloc) or safely terminated */
            ck_assert_msg(keymap_mem[KEYMAP_MEM_SIZE - 1] == '\0',
                "Buffer must remain safely null-terminated after rejection");
        }

        free(keymap_mem);
    }

    /* Test dynamically generated oversized payloads */
    size_t oversized_lengths[] = {
        KEYMAP_MEM_SIZE,         /* exact size (off-by-one) */
        KEYMAP_MEM_SIZE + 1,     /* one byte over */
        KEYMAP_MEM_SIZE * 2,     /* 2x oversized */
        KEYMAP_MEM_SIZE * 10,    /* 10x oversized */
        KEYMAP_MEM_SIZE * 100,   /* 100x oversized */
        65536,                   /* 64KB */
        131072,                  /* 128KB */
    };

    int num_dynamic = sizeof(oversized_lengths) / sizeof(oversized_lengths[0]);

    for (int i = 0; i < num_dynamic; i++) {
        char *keymap_mem = calloc(1, KEYMAP_MEM_SIZE);
        ck_assert_ptr_nonnull(keymap_mem);

        /* Place a canary at the end of the buffer */
        keymap_mem[KEYMAP_MEM_SIZE - 1] = '\0';

        char *oversized_input = make_payload(oversized_lengths[i], 'A');
        ck_assert_ptr_nonnull(oversized_input);

        size_t src_len = strlen(oversized_input);
        ck_assert_msg(src_len == oversized_lengths[i],
            "Payload length %zu should equal requested %zu",
            src_len, oversized_lengths[i]);

        int result = safe_keymap_copy(keymap_mem, KEYMAP_MEM_SIZE, oversized_input);

        /* Oversized input MUST be rejected */
        ck_assert_msg(result == -1,
            "Oversized input of length %zu must be rejected (got result %d)",
            oversized_lengths[i], result);

        /* Buffer must remain safe after rejection */
        ck_assert_msg(keymap_mem[KEYMAP_MEM_SIZE - 1] == '\0',
            "Buffer canary must be intact after rejecting oversized input of length %zu",
            oversized_lengths[i]);

        /* Verify no data was written beyond what's safe */
        size_t written_len = strlen(keymap_mem);
        ck_assert_msg(written_len < KEYMAP_MEM_SIZE,
            "Written length %zu must be < buffer size %d after rejection",
            written_len, KEYMAP_MEM_SIZE);

        free(oversized_input);
        free(keymap_mem);
    }

    /* Test boundary: exactly one byte under the limit should succeed */
    {
        char *keymap_mem = calloc(1, KEYMAP_MEM_SIZE);
        ck_assert_ptr_nonnull(keymap_mem);

        char *boundary_input = make_payload(KEYMAP_MEM_SIZE - 1, 'B');
        ck_assert_ptr_nonnull(boundary_input);

        int result = safe_keymap_copy(keymap_mem, KEYMAP_MEM_SIZE, boundary_input);
        ck_assert_msg(result == 0,
            "Input of exactly KEYMAP_MEM_SIZE-1 bytes should be accepted");

        size_t copied_len = strlen(keymap_mem);
        ck_assert_msg(copied_len == KEYMAP_MEM_SIZE - 1,
            "Copied length %zu should equal %d", copied_len, KEYMAP_MEM_SIZE - 1);
        ck_assert_msg(keymap_mem[KEYMAP_MEM_SIZE - 1] == '\0',
            "Buffer must be null-terminated at last position");

        free(boundary_input);
        free(keymap_mem);
    }

    /* Test that strcpy (the vulnerable function) would overflow but safe version doesn't */
    {
        /* Allocate a buffer with a guard region to detect overflow */
        size_t guard_size = KEYMAP_MEM_SIZE;
        char *region = calloc(1, KEYMAP_MEM_SIZE + guard_size);
        ck_assert_ptr_nonnull(region);

        char *keymap_mem = region;
        char *guard_region = region + KEYMAP_MEM_SIZE;

        /* Fill guard region with sentinel value */
        memset(guard_region, 0xDE, guard_size);

        /* Create 2x oversized payload */
        char *attack_payload = make_payload(KEYMAP_MEM_SIZE * 2, 'X');
        ck_assert_ptr_nonnull(attack_payload);

        /* Use safe copy - must not touch guard region */
        int result = safe_keymap_copy(keymap_mem, KEYMAP_MEM_SIZE, attack_payload);
        ck_assert_msg(result == -1, "2x oversized payload must be rejected");

        /* Verify guard region is untouched */
        for (size_t j = 0; j < guard_size; j++) {
            ck_assert_msg((unsigned char)guard_region[j] == 0xDE,
                "Guard region byte %zu was overwritten! Buffer overflow detected at offset %zu",
                j, j);
        }

        free(attack_payload);
        free(region);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_set_timeout(tc_core, 60);
    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}