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

/* --json state for one remember_run, between diag_json_begin and diag_json_end.
   Deliberately mutable; one call at a time (see diag_json_begin). */
typedef struct {
    FILE *warnings; /* comma-joined warning objects */
    char *warnings_buf;
    size_t warnings_len;
    size_t nwarnings;
    FILE *error; /* the first error object */
    char *error_buf;
    size_t error_len;
    const char *command; /* names the usage fix; NULL before a command is known */
    bool on;
    bool has_error;
    /* NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers) */
    char pad_[6]; /* explicit tail padding (kept -Wpadded-clean) */
} JsonNotices;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static JsonNotices g_json;

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
    [DIAG_SCHEMA_TOO_NEW] = "schema_too_new",
};

/* error.fix per code (decision:plan21-error-fix-drafts-c42). The SQLite codes
   take their E4.8 fix in diag_error_sqlite; their rows here serve a non-SQLite
   failure under the same code. schema_too_new and the not_found example are
   locked text (C-01, JSON-STYLE); the rest are C-42 drafts. usage is built from
   the command in usage_fix. */
static const char *const k_fix[DIAG_CODE_COUNT] = {
    [DIAG_NOT_FOUND] = "Run 'remember list' to see keys.",
    [DIAG_DATABASE] = "Try again. If it keeps happening, report the details from --json.",
    [DIAG_OUT_OF_MEMORY] = "Close other apps to free memory, then try again.",
    [DIAG_INTERNAL] = "This is a bug in remember. Report it with the command you ran.",
    [DIAG_INVALID_QUERY] = "Check the search words, or put the query in double quotes.",
    [DIAG_KEY_CONFLICT] = "Pick another key, or rekey the other memory first.",
    [DIAG_BODY_CONFLICT] = "Change the text, or update the memory that already has it.",
    [DIAG_EXPIRED] = "Add --expired to use it.",
    [DIAG_NOT_EXPIRED] = "Run the command without --expired.",
    [DIAG_DELETED] = "Add --deleted to use it.",
    [DIAG_NOT_DELETED] = "Run 'remember delete' on it first.",
    [DIAG_NOT_SINGLE_DEVICE] = "Delete it instead; it stays in the Trash and syncs as deleted.",
    [DIAG_SELF_LINK] = "Link two different memories.",
    [DIAG_CYCLE] = "Remove the link that closes the loop, then try again.",
    [DIAG_SOURCE_TOO_OLD] = "Open the source brain once with this remember, then import it.",
    [DIAG_UNIQUE_TAKEN] = "Rekey or delete the memory that holds the key or text, then try again.",
    [DIAG_STALE] = "Read the memory again, then retry the change.",
    [DIAG_NEWER_DATABASE] = "Upgrade remember, then try again.",
    [DIAG_UNSUPPORTED_DATABASE] = "Open the brain with the remember that made it, and export it.",
    [DIAG_IMPORT_PARSE] = "Check the import source, then try again.",
    [DIAG_BUSY] = "Wait a moment and try again.",
    [DIAG_READ_ONLY] = "Give yourself write permission on the brain file and its folder.",
    [DIAG_DISK_FULL] = "Free some space and try again.",
    [DIAG_CANNOT_OPEN] = "Check that the folder exists and that you can read and write it.",
    [DIAG_NOT_A_DATABASE] = "Point --db at a remember brain.",
    [DIAG_CORRUPT] = "Copy the file somewhere safe, then restore a backup.",
    [DIAG_IO_ERROR] = "Check the disk or drive, then try again.",
    [DIAG_SCHEMA_TOO_NEW] = "Upgrade remember to make changes.",
};

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
const char *diag_code_fix(DiagCode code)
{
    size_t index = (size_t)code;

    if (index >= (size_t)DIAG_CODE_COUNT) {
        return NULL;
    }
    return k_fix[index];
}

/* decision:plan21-retryable-codes */
static bool code_retryable(DiagCode code)
{
    if (code == DIAG_BUSY || code == DIAG_IO_ERROR || code == DIAG_OUT_OF_MEMORY) {
        return true;
    }
    return false;
}

static const char *const k_warning_codes[DIAG_WARN_COUNT] = {
    [DIAG_WARN_RAW_QUERY_IGNORED] = "raw_query_ignored",
    [DIAG_WARN_DEPRECATED_FLAG] = "deprecated_flag",
    [DIAG_WARN_SYNCED_FOLDER] = "synced_folder",
    [DIAG_WARN_SETTING_INVALID] = "setting_invalid",
};

/* Plain line lead per warning. E2.3 gives raw_query_ignored its own words; the
   JSON message stays the bare text. */
static const char *const k_warning_plain_prefix[DIAG_WARN_COUNT] = {
    [DIAG_WARN_RAW_QUERY_IGNORED] = "warning: raw query ignored: ",
    [DIAG_WARN_DEPRECATED_FLAG] = "warning: ",
    [DIAG_WARN_SYNCED_FOLDER] = "warning: ",
    [DIAG_WARN_SETTING_INVALID] = "warning: ",
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

/* One plain or JSON notice. plain_prefix is "" for errors and starts "warning: " for warnings. */
typedef struct {
    const char *json_kind;
    const char *plain_prefix;
    const char *code;
    const char *message;
    const DiagDetailField *fields; /* JSON detail members; NULL when none */
    size_t nfields;
    DiagCode error_code; /* errors only: picks fix and retryable */
    char pad_[4];        /* explicit tail padding (kept -Wpadded-clean) */
} Notice;

/* {"code":C,"message":M; the caller adds the rest and closes the object. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int write_json_notice_head(FILE *err, const char *code, const char *message)
{
    if (fputs("{\"code\":", err) < 0) {
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

/* ,"detail":{"<name>":"<value>",...} in the given order; nothing when n is 0. */
static int write_json_detail_fields(FILE *err, const DiagDetailField *fields, size_t n)
{
    size_t i = 0;

    if (n == 0U) {
        return 0;
    }
    if (fputs(",\"detail\":{", err) < 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (i > 0U && fputc(',', err) == EOF) {
            return -1;
        }
        if (output_json_string(err, fields[i].name) != 0 || fputc(':', err) == EOF ||
            output_json_string(err, fields[i].value) != 0) {
            return -1;
        }
    }
    if (fputc('}', err) == EOF) {
        return -1;
    }
    return 0;
}

/* The usage fix names the command's help when a command is known. */
enum { USAGE_FIX_BUF = 96 };
static const char *usage_fix(char *buf, size_t cap)
{
    if (g_json.command == NULL) {
        return "Run 'remember --help' to see the commands.";
    }
    (void)snprintf(buf, cap, "Run 'remember %s --help' to see how to use it.", g_json.command);
    return buf;
}

/* ,"fix":F,"retryable":B */
static int write_fix_retryable(FILE *fp, const char *fix, DiagCode code)
{
    if (fputs(",\"fix\":", fp) < 0 || output_json_string(fp, fix) != 0) {
        return -1;
    }
    if (code_retryable(code)) {
        return fputs(",\"retryable\":true", fp) < 0 ? -1 : 0;
    }
    return fputs(",\"retryable\":false", fp) < 0 ? -1 : 0;
}

/* The stream for this notice: the error slot (NULL once taken: the first
   failure is the cause, later ones are dropped) or the next warnings item. */
static FILE *json_slot(bool is_error, bool *dropped)
{
    *dropped = false;
    if (is_error) {
        if (g_json.has_error) {
            *dropped = true;
            return NULL;
        }
        g_json.has_error = true;
        return g_json.error;
    }
    if (g_json.nwarnings > 0U && fputc(',', g_json.warnings) == EOF) {
        return NULL;
    }
    g_json.nwarnings++;
    return g_json.warnings;
}

static int write_json_notice(Notice notice, const char *message)
{
    char buf[USAGE_FIX_BUF];
    bool is_error = strcmp(notice.json_kind, "error") == 0;
    bool dropped = false;
    FILE *fp = json_slot(is_error, &dropped);

    if (dropped) {
        return 0;
    }
    if (fp == NULL || write_json_notice_head(fp, notice.code, message) != 0) {
        return -1;
    }
    if (is_error) {
        const char *fix = notice.error_code == DIAG_USAGE ? usage_fix(buf, sizeof(buf))
                                                          : k_fix[notice.error_code];
        if (write_fix_retryable(fp, fix, notice.error_code) != 0) {
            return -1;
        }
    }
    if (write_json_detail_fields(fp, notice.fields, notice.nfields) != 0) {
        return -1;
    }
    return fputc('}', fp) == EOF ? -1 : 0;
}

static int write_notice(Notice notice)
{
    FILE *err = app_err();
    const char *message = message_or_empty(notice.message);

    if (g_json.on) {
        return write_json_notice(notice, message);
    }
    /* decision:plan17a-plain-notice-one-line: an echoed token cannot split the
       line or drive the terminal. JSON escapes the same bytes above. */
    if (fprintf(err, "remember: %s", notice.plain_prefix) < 0) {
        return -1;
    }
    if (output_text_human(err, message, false) != 0) {
        return -1;
    }
    if (fputc('\n', err) == EOF) {
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
        .error_code = code,
    });
}

int diag_warning(DiagWarning code, const char *message)
{
    return diag_warning_detail(code, message, NULL, 0U);
}

int diag_warning_detail(DiagWarning code, const char *message, const DiagDetailField *fields,
                        size_t nfields)
{
    const char *name = diag_warning_name(code);

    if (name == NULL || (fields == NULL && nfields > 0U)) {
        return -1;
    }
    return write_notice((Notice){
        .json_kind = "warning",
        .plain_prefix = k_warning_plain_prefix[code],
        .code = name,
        .message = message,
        .fields = fields,
        .nfields = nfields,
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
         "Copy the file somewhere safe before anything else, then restore a backup, "
         "or recover what can be read with `remember --db NEW.db import --from-db COPY`."},
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

/* The import --from-db source failed, not the brain (Itay 2026-10-15): the
   causes a read-only source can hit name the source. Others keep the base row. */
static const char k_source_fix[] = "Check the --from-db path.";
static const SqliteAdvice k_source_advice[DIAG_CODE_COUNT] = {
    [DIAG_BUSY] = {"the import source is locked by another process", k_source_fix},
    [DIAG_CANNOT_OPEN] = {"cannot open the import source", k_source_fix},
    [DIAG_NOT_A_DATABASE] = {"the import source is not a remember database", k_source_fix},
    [DIAG_CORRUPT] = {"the import source is damaged", k_source_fix},
};

/* database has no cause; its fix is the E4.8 "Try again" sentence. */
static const char k_database_fix[] =
    "Try again. If it keeps happening, report the details from --json.";

/* E4.8 cause/fix for code, or NULL for database. corrupt + fts_only is the repair row. */
static const SqliteAdvice *sqlite_advice(DiagCode code, const StoreError *detail)
{
    if (code == DIAG_DATABASE) {
        return NULL;
    }
    if (detail->import_source && k_source_advice[code].cause != NULL) {
        return &k_source_advice[code];
    }
    if (code == DIAG_CORRUPT && detail->fts_only) {
        return &k_sqlite_fts_advice;
    }
    return &k_sqlite_advice[code];
}

/* Every part is bounded (cause < 50, sqlite_name < STORE_SQLITE_NAME_LEN, fix
   < 140), so one stack line holds the longest E4.8 text. */
enum { SQLITE_LINE_BUF = 512 };

/* E4.9 detail members in E4.9 order: sqlite_code, sqlite_extended, sqlite_name,
   sqlite_message always; then the code-gated members. The fix is error.fix
   (plan 21), no longer a detail member. abs_path is NULL when omitted. */
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
    return output_json_string(err, detail->sqlite_message);
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
    const SqliteAdvice *advice = NULL;
    const char *fix = k_database_fix;
    char line[SQLITE_LINE_BUF];
    char absolute[REMEMBER_PATH_MAX];
    const char *abs_path = NULL;
    FILE *err = NULL;

    if (!sqlite_code_valid(code) || detail == NULL || detail->error_class == STORE_ERROR_NONE) {
        return -1;
    }
    name = diag_code_name(code);
    advice = sqlite_advice(code, detail);
    if (advice != NULL) {
        fix = advice->fix;
    }
    if (!g_json.on) {
        /* E4.8 plain line: cause, extended name, fix. */
        if (advice == NULL) {
            (void)snprintf(line, sizeof(line), "database error (%s). %s", detail->sqlite_name, fix);
        } else {
            (void)snprintf(line, sizeof(line), "database error: %s (%s). %s", advice->cause,
                           detail->sqlite_name, fix);
        }
        return write_notice((Notice){
            .json_kind = "error",
            .plain_prefix = "",
            .code = name,
            .message = line,
            .error_code = code,
        });
    }
    /* JSON (Itay 2026-10-15, amends E4.4 for SQLite codes): a short message;
       the E4.8 fix is error.fix (plan 21). */
    if (advice == NULL) {
        (void)snprintf(line, sizeof(line), "database error");
    } else {
        (void)snprintf(line, sizeof(line), "database error: %s", advice->cause);
    }
    /* D7: only an absolute path is emitted; a path that will not absolutize
       is omitted rather than emitted relative. */
    if (sqlite_code_has_path(code) && path != NULL &&
        util_absolute_path(path, absolute, sizeof(absolute)) == 0) {
        abs_path = absolute;
    }
    if (g_json.has_error) {
        return 0; /* the first failure is the cause */
    }
    g_json.has_error = true;
    err = g_json.error;
    if (write_json_notice_head(err, name, line) != 0 || write_fix_retryable(err, fix, code) != 0 ||
        write_sqlite_detail(err, code, detail, abs_path) != 0) {
        return -1;
    }
    return fputc('}', err) == EOF ? -1 : 0;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
int diag_json_begin(const char *command)
{
    diag_json_end();
    g_json.warnings = open_memstream(&g_json.warnings_buf, &g_json.warnings_len);
    g_json.error = open_memstream(&g_json.error_buf, &g_json.error_len);
    if (g_json.warnings == NULL || g_json.error == NULL) {
        /* still plain mode; the caller's diag_json_end frees what opened */
        return -1;
    }
    g_json.command = command;
    g_json.on = true;
    return 0;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
bool diag_json_has_error(void)
{
    return g_json.has_error;
}

/* Flushing a memstream makes its buffer and length current. */
static int flush_notices(FILE *fp)
{
    return fflush(fp) != 0 || ferror(fp) != 0 ? -1 : 0;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
int diag_json_write_error(FILE *out)
{
    if (flush_notices(g_json.error) != 0 || fputs("\"error\":", out) < 0 ||
        fwrite(g_json.error_buf, 1U, g_json.error_len, out) != g_json.error_len) {
        return -1;
    }
    return 0;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
int diag_json_write_warnings(FILE *out)
{
    if (g_json.nwarnings == 0U) {
        return 0;
    }
    if (flush_notices(g_json.warnings) != 0 || fputs(",\"warnings\":[", out) < 0 ||
        fwrite(g_json.warnings_buf, 1U, g_json.warnings_len, out) != g_json.warnings_len ||
        fputc(']', out) == EOF) {
        return -1;
    }
    return 0;
}

/* cppcheck-suppress staticFunction ; public API (diag.h); remember_run */
void diag_json_end(void)
{
    if (g_json.warnings != NULL) {
        (void)fclose(g_json.warnings);
    }
    if (g_json.error != NULL) {
        (void)fclose(g_json.error);
    }
    free(g_json.warnings_buf);
    free(g_json.error_buf);
    g_json = (JsonNotices){0};
}
