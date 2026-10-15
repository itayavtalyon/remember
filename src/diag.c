#include "diag.h"

#include "appio.h"
#include "output.h"
#include "store.h"
#include "util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

/* JSON envelope through the message value; the caller closes the object. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int write_json_notice_head(FILE *err, const char *kind, const char *code,
                                  const char *message)
{
    if (fprintf(err, "{\"version\":1,\"%s\":{\"code\":", kind) < 0) {
        return -1;
    }
    if (output_json_string(err, code) != 0) {
        return -1;
    }
    if (fputs(",\"message\":", err) < 0) {
        return -1;
    }
    if (output_json_string(err, message) != 0) {
        return -1;
    }
    return 0;
}

static int write_notice(Notice notice)
{
    FILE *err = app_err();
    const char *message = message_or_empty(notice.message);

    if (notice.json_errors) {
        if (write_json_notice_head(err, notice.json_kind, notice.code, message) != 0) {
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

/* E4.8 cause + fix per SQLite code. corrupt with fts_only uses the repair
   line below instead of the base row. database has no cause (own text). */
typedef struct {
    const char *cause;
    const char *fix;
} SqliteAdvice;

static const SqliteAdvice k_sqlite_advice[DIAG_CODE_COUNT] = {
    [DIAG_BUSY] =
        {"the brain is locked by another process",
         "Wait a moment and try again. Another app or a terminal may be using this brain."},
    [DIAG_READ_ONLY] =
        {"the brain is read-only",
         "Give yourself write permission on the brain file and its folder, or move the brain to "
         "a writable folder."},
    [DIAG_DISK_FULL] = {"disk is full", "Free some space and try again."},
    [DIAG_CANNOT_OPEN] = {"cannot open the brain file",
                          "Check that the folder exists and that you can read and write it."},
    [DIAG_NOT_A_DATABASE] = {"the file is not a remember database",
                             "Point --db at a remember brain, or move this file away."},
    [DIAG_CORRUPT] =
        {"the brain file is damaged",
         "Copy the file somewhere safe before anything else, then run `remember export`, or "
         "restore a backup."},
    [DIAG_IO_ERROR] = {"the disk reported a read or write error",
                       "Check the disk or drive, then try again."},
};

/* E4.8 corrupt + fts_only row (stage 6 sets fts_only). */
static const SqliteAdvice k_sqlite_fts_advice = {
    "the search index is damaged",
    "Run `remember repair search-index`. Your memories are not affected.",
};

static bool sqlite_code_valid(DiagCode code)
{
    if (code == DIAG_BUSY || code == DIAG_READ_ONLY || code == DIAG_DISK_FULL ||
        code == DIAG_CANNOT_OPEN || code == DIAG_NOT_A_DATABASE || code == DIAG_CORRUPT ||
        code == DIAG_IO_ERROR || code == DIAG_DATABASE) {
        return true;
    }
    return false;
}

/* E4.9: only these six codes carry the absolute brain path. */
static bool sqlite_code_has_path(DiagCode code)
{
    if (code == DIAG_READ_ONLY || code == DIAG_DISK_FULL || code == DIAG_CANNOT_OPEN ||
        code == DIAG_NOT_A_DATABASE || code == DIAG_CORRUPT || code == DIAG_IO_ERROR) {
        return true;
    }
    return false;
}

/* E4.7 reason names; NONE is the specified catch-all ("else other"). */
static const char *const k_open_reasons[] = {
    [STORE_REASON_NONE] = "other",      [STORE_REASON_PERMISSION] = "permission",
    [STORE_REASON_MISSING] = "missing", [STORE_REASON_IS_DIRECTORY] = "is_directory",
    [STORE_REASON_OTHER] = "other",
};

/* Heap-owned E4.8 message (E4.4: the JSON message is this same text). */
static char *sqlite_plain_message(DiagCode code, const StoreError *detail)
{
    const SqliteAdvice *advice = (code == DIAG_DATABASE) ? NULL : &k_sqlite_advice[code];
    int wrote = 0;
    char *buf = NULL;

    if (code == DIAG_CORRUPT && detail->fts_only) {
        advice = &k_sqlite_fts_advice;
    }
    if (advice == NULL) {
        wrote = snprintf(NULL, 0U,
                         "database error (%s). Try again. If it keeps happening, report the "
                         "details from --json-errors.",
                         detail->sqlite_name);
    } else {
        wrote = snprintf(NULL, 0U, "database error: %s (%s). %s", advice->cause,
                         detail->sqlite_name, advice->fix);
    }
    if (wrote < 0) {
        return NULL;
    }
    buf = (char *)malloc((size_t)wrote + 1U);
    if (buf == NULL) {
        return NULL;
    }
    if (advice == NULL) {
        (void)snprintf(buf, (size_t)wrote + 1U,
                       "database error (%s). Try again. If it keeps happening, report the details "
                       "from --json-errors.",
                       detail->sqlite_name);
    } else {
        (void)snprintf(buf, (size_t)wrote + 1U, "database error: %s (%s). %s", advice->cause,
                       detail->sqlite_name, advice->fix);
    }
    return buf;
}

/* E4.9 detail members in E4.9 order: sqlite_code, sqlite_extended, sqlite_name
   and sqlite_message always; then the code-gated members. abs_path is NULL
   when omitted. */
static int write_sqlite_required(FILE *err, const StoreError *detail)
{
    if (fprintf(err, ",\"detail\":{\"sqlite_code\":%d,\"sqlite_extended\":%d", detail->sqlite_code,
                detail->sqlite_extended) < 0) {
        return -1;
    }
    if (fputs(",\"sqlite_name\":", err) < 0) {
        return -1;
    }
    if (output_json_string(err, detail->sqlite_name) != 0) {
        return -1;
    }
    if (fputs(",\"sqlite_message\":", err) < 0) {
        return -1;
    }
    if (output_json_string(err, detail->sqlite_message) != 0) {
        return -1;
    }
    return 0;
}

/* E4.7 reason names; an out-of-range reason can never come from the store and
   is the specified catch-all ("else other"). */
static const char *open_reason_name(StoreOpenReason reason)
{
    size_t index = (size_t)reason;

    if (index >= sizeof(k_open_reasons) / sizeof(k_open_reasons[0])) {
        return "other";
    }
    return k_open_reasons[index];
}

static int write_cannot_open_reason(FILE *err, const StoreError *detail)
{
    if (fputs(",\"reason\":", err) < 0) {
        return -1;
    }
    if (output_json_string(err, open_reason_name(detail->reason)) != 0) {
        return -1;
    }
    return 0;
}

static int write_corrupt_fts_only(FILE *err, const StoreError *detail)
{
    if (detail->fts_only) {
        if (fputs(",\"fts_only\":true", err) < 0) {
            return -1;
        }
    } else {
        if (fputs(",\"fts_only\":false", err) < 0) {
            return -1;
        }
    }
    return 0;
}

static int write_sqlite_optional(FILE *err, DiagCode code, const StoreError *detail,
                                 const char *abs_path)
{
    if (detail->sys_errno != 0) {
        if (fprintf(err, ",\"errno\":%d", detail->sys_errno) < 0) {
            return -1;
        }
    }
    if (abs_path != NULL) {
        if (fputs(",\"path\":", err) < 0) {
            return -1;
        }
        if (output_json_string(err, abs_path) != 0) {
            return -1;
        }
    }
    if (code == DIAG_CANNOT_OPEN) {
        if (write_cannot_open_reason(err, detail) != 0) {
            return -1;
        }
    }
    if (code == DIAG_CORRUPT) {
        if (write_corrupt_fts_only(err, detail) != 0) {
            return -1;
        }
    }
    if (fputs("}", err) < 0) {
        return -1;
    }
    return 0;
}

static int write_sqlite_detail(FILE *err, DiagCode code, const StoreError *detail,
                               const char *abs_path)
{
    if (write_sqlite_required(err, detail) != 0) {
        return -1;
    }
    return write_sqlite_optional(err, code, detail, abs_path);
}

/* cppcheck-suppress staticFunction ; public API (diag.h); used by tests */
int diag_error_sqlite(DiagCode code, const StoreError *detail, const char *path)
{
    const char *name = NULL;
    char *message = NULL;
    char absolute[REMEMBER_PATH_MAX];
    const char *abs_path = NULL;
    FILE *err = NULL;

    if (!sqlite_code_valid(code) || detail == NULL || detail->error_class == STORE_ERROR_NONE) {
        return -1;
    }
    name = diag_code_name(code);
    if (name == NULL) {
        return -1;
    }
    /* D7: only an absolute path is emitted; a path that will not absolutize
       is omitted rather than emitted relative. */
    if (sqlite_code_has_path(code) && path != NULL) {
        if (util_absolute_path(path, absolute, sizeof(absolute)) == 0) {
            abs_path = absolute;
        }
    }
    message = sqlite_plain_message(code, detail);
    if (message == NULL) {
        return -1;
    }
    err = app_err();
    if (!g_json_errors) {
        int rc = write_notice((Notice){
            .json_kind = "error",
            .plain_prefix = "",
            .code = name,
            .message = message,
            .json_errors = false,
        });
        free(message);
        return rc;
    }
    if (write_json_notice_head(err, "error", name, message) != 0) {
        free(message);
        return -1;
    }
    if (write_sqlite_detail(err, code, detail, abs_path) != 0) {
        free(message);
        return -1;
    }
    if (fputs("}}\n", err) < 0) {
        free(message);
        return -1;
    }
    free(message);
    return 0;
}
