#include "diag.h"

#include "appio.h"
#include "output.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* See diag_set_json_errors. Deliberately mutable; one call at a time. */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_json_errors = false;

/* E4.3 order. One token per code; diag_code_name / diag_code_from_name share it. */
static const char *const k_error_codes[DIAG_CODE_COUNT] = {
    [DIAG_USAGE] = "usage",
    [DIAG_NOT_FOUND] = "not_found",
    [DIAG_DATABASE] = "database",
    [DIAG_OUT_OF_MEMORY] = "out_of_memory",
    [DIAG_INTERNAL] = "internal",
    [DIAG_INVALID_QUERY] = "invalid_query",
    [DIAG_KEY_CONFLICT] = "key_conflict",
    [DIAG_BODY_CONFLICT] = "body_conflict",
    [DIAG_EXPIRED] = "expired",
    [DIAG_NOT_EXPIRED] = "not_expired",
    [DIAG_DELETED] = "deleted",
    [DIAG_NOT_DELETED] = "not_deleted",
    [DIAG_NOT_SINGLE_DEVICE] = "not_single_device",
    [DIAG_SELF_LINK] = "self_link",
    [DIAG_CYCLE] = "cycle",
    [DIAG_SOURCE_TOO_OLD] = "source_too_old",
    [DIAG_UNIQUE_TAKEN] = "unique_taken",
    [DIAG_STALE] = "stale",
    [DIAG_NEWER_DATABASE] = "newer_database",
    [DIAG_UNSUPPORTED_DATABASE] = "unsupported_database",
    [DIAG_IMPORT_PARSE] = "import_parse",
    [DIAG_BUSY] = "busy",
    [DIAG_READ_ONLY] = "read_only",
    [DIAG_DISK_FULL] = "disk_full",
    [DIAG_CANNOT_OPEN] = "cannot_open",
    [DIAG_NOT_A_DATABASE] = "not_a_database",
    [DIAG_CORRUPT] = "corrupt",
    [DIAG_IO_ERROR] = "io_error",
};

static const char *const k_warning_codes[DIAG_WARN_COUNT] = {
    [DIAG_WARN_RAW_QUERY_IGNORED] = "raw_query_ignored",
    [DIAG_WARN_DEPRECATED_FLAG] = "deprecated_flag",
    [DIAG_WARN_SYNCED_FOLDER] = "synced_folder",
};

static const char *catalog_name(const char *const *names, size_t count, size_t index)
{
    if (index >= count) {
        return NULL;
    }
    return names[index];
}

static bool catalog_from_name(const char *const *names, size_t count, const char *name, size_t *out)
{
    size_t i = 0;

    if (name == NULL || out == NULL) {
        return false;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); used by tests */
const char *diag_code_name(DiagCode code)
{
    return catalog_name(k_error_codes, (size_t)DIAG_CODE_COUNT, (size_t)code);
}

bool diag_code_from_name(const char *name, DiagCode *out)
{
    size_t index = 0;

    if (out == NULL) {
        return false;
    }
    if (!catalog_from_name(k_error_codes, (size_t)DIAG_CODE_COUNT, name, &index)) {
        return false;
    }
    *out = (DiagCode)index;
    return true;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); used by tests */
const char *diag_warning_name(DiagWarning code)
{
    return catalog_name(k_warning_codes, (size_t)DIAG_WARN_COUNT, (size_t)code);
}

bool diag_warning_from_name(const char *name, DiagWarning *out)
{
    size_t index = 0;

    if (out == NULL) {
        return false;
    }
    if (!catalog_from_name(k_warning_codes, (size_t)DIAG_WARN_COUNT, name, &index)) {
        return false;
    }
    *out = (DiagWarning)index;
    return true;
}

static const char *message_or_empty(const char *message)
{
    return message != NULL ? message : "";
}

/* One plain or JSON notice. plain_prefix is "" for errors and "warning: " for warnings. */
typedef struct {
    const char *json_kind;
    const char *plain_prefix;
    const char *code;
    const char *message;
    bool json_errors;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[7]; /* explicit tail padding (kept -Wpadded-clean) */
} Notice;

static int write_notice(Notice notice)
{
    FILE *err = app_err();
    const char *message = message_or_empty(notice.message);

    if (notice.json_errors) {
        if (fprintf(err, "{\"version\":1,\"%s\":{\"code\":", notice.json_kind) < 0) {
            return -1;
        }
        if (output_json_string(err, notice.code) != 0) {
            return -1;
        }
        if (fputs(",\"message\":", err) < 0) {
            return -1;
        }
        if (output_json_string(err, message) != 0) {
            return -1;
        }
        if (fputs("}}\n", err) < 0) {
            return -1;
        }
        return 0;
    }
    if (fprintf(err, "remember: %s%s\n", notice.plain_prefix, message) < 0) {
        return -1;
    }
    return 0;
}

int diag_error(DiagCode code, const char *message)
{
    const char *name = diag_code_name(code);

    if (name == NULL) {
        return -1;
    }
    return write_notice((Notice){
        .json_kind = "error",
        .plain_prefix = "",
        .code = name,
        .message = message,
        .json_errors = g_json_errors,
    });
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
void diag_set_json_errors(bool enabled)
{
    g_json_errors = enabled;
}

int diag_warning(DiagWarning code, const char *message)
{
    const char *name = diag_warning_name(code);

    if (name == NULL) {
        return -1;
    }
    return write_notice((Notice){
        .json_kind = "warning",
        .plain_prefix = "warning: ",
        .code = name,
        .message = message,
        .json_errors = g_json_errors,
    });
}
