#include "commands.h"
#include "commands_common.h"
#include "diag.h"

#include "appio.h"
#include "exit_codes.h"
#include "export_format.h"
#include "fileio.h"
#include "output.h"
#include "store.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { EXPORT_JSON = 0, EXPORT_MD, EXPORT_CSV } ExportFormat;

static const char *const k_formats[] = {
    [EXPORT_JSON] = "json", [EXPORT_MD] = "md", [EXPORT_CSV] = "csv"};

enum { EXPORT_ARGS_PAD = 7 };

/* The parsed export flags (decision:plan22-export-bin-flags). */
typedef struct {
    const char *format;
    const char *out;
    ExportFormat fmt;
    StoreExportBins bins;  /* each --include-* sets its bin once */
    bool exclude_archived; /* parsed; no effect until the archive bin exists */
    bool force;
    char pad_[EXPORT_ARGS_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} ExportArgs;

static const char k_format_missing[] = "export requires --format (json, md, or csv)";
static const char k_out_missing[] = "export requires --out PATH";

static int take_once(bool *seen, const char *flag)
{
    if (*seen) {
        err_fmt(DIAG_USAGE, "duplicate %s", flag);
        return -1;
    }
    *seen = true;
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) take_value's argv, then the slot
static int take_string(int *i, int argc, const char **argv, const char **slot)
{
    const char *flag = argv[*i];
    TakeValue taken =
        take_value(i, argc, argv, strcmp(flag, "--format") == 0 ? k_format_missing : k_out_missing);

    if (taken.rc != 0) {
        err_msg(DIAG_USAGE, taken.err);
        return -1;
    }
    if (*slot != NULL) {
        err_fmt(DIAG_USAGE, "duplicate %s", flag);
        return -1;
    }
    *slot = taken.value;
    return 0;
}

/* One flag at argv[*i]; 1 when it is not an export flag. */
static int parse_flag(int *i, int argc, const char **argv, ExportArgs *a)
{
    const char *arg = argv[*i];

    if (strcmp(arg, "--format") == 0) {
        return take_string(i, argc, argv, &a->format);
    }
    if (strcmp(arg, "--out") == 0) {
        return take_string(i, argc, argv, &a->out);
    }
    if (strcmp(arg, "--include-expired") == 0) {
        return take_once(&a->bins.in[STORE_BIN_EXPIRED], arg);
    }
    if (strcmp(arg, "--include-trash") == 0) {
        return take_once(&a->bins.in[STORE_BIN_DELETED], arg);
    }
    if (strcmp(arg, "--exclude-archived") == 0) {
        return take_once(&a->exclude_archived, arg);
    }
    if (strcmp(arg, "--force") == 0) {
        return take_once(&a->force, arg);
    }
    return 1;
}

static int parse_args(int argc, const char **argv, ExportArgs *a)
{
    int i = 0;

    memset(a, 0, sizeof(*a));
    for (i = 0; i < argc; i++) {
        int rc = parse_flag(&i, argc, argv, a);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            err_fmt(DIAG_USAGE, "unknown option '%s'", argv[i]);
            return -1;
        }
        err_msg(DIAG_USAGE, "export takes no positional arguments");
        return -1;
    }
    if (a->format == NULL) {
        err_msg(DIAG_USAGE, k_format_missing);
        return -1;
    }
    for (a->fmt = EXPORT_JSON; (size_t)a->fmt < sizeof k_formats / sizeof k_formats[0]; a->fmt++) {
        if (strcmp(a->format, k_formats[a->fmt]) == 0) {
            break;
        }
    }
    if ((size_t)a->fmt == sizeof k_formats / sizeof k_formats[0]) {
        err_msg(DIAG_USAGE, "invalid --format (use json, md, or csv)");
        return -1;
    }
    if (a->out == NULL) {
        err_msg(DIAG_USAGE, k_out_missing);
        return -1;
    }
    a->bins.in[STORE_BIN_LIVE] = true;
    return 0;
}

/* decision:plan22-export-file-error-copy. A message names the path between
   before and after; NULL after means it names no path. */
typedef struct {
    int sys_errno;
    DiagCode code;
    const char *before;
    const char *after;
    const char *fix;
    const char *reason;
} FileErrorText;

static const FileErrorText k_file_errors[] = {
    {ENOSPC, DIAG_DISK_FULL, "disk is full.", NULL, "Free some space and try again.", NULL},
    {EACCES, DIAG_READ_ONLY, "cannot write \"", "\".",
     "Give yourself write permission on that folder, or export to another folder.", NULL},
    {EROFS, DIAG_READ_ONLY, "cannot write \"", "\".",
     "Give yourself write permission on that folder, or export to another folder.", NULL},
    {ENOENT, DIAG_CANNOT_OPEN, "the folder for \"", "\" doesn't exist.",
     "Create the folder, or export to another path.", "missing"},
    /* decision:plan22-export-out-is-folder */
    {EISDIR, DIAG_CANNOT_OPEN, "\"", "\" is a folder.", "Pick a file path for --out.",
     "is_directory"},
};

static const FileErrorText k_io_error = {0,
                                         DIAG_IO_ERROR,
                                         "the disk reported a read or write error.",
                                         NULL,
                                         "Check the disk or drive, then try again.",
                                         NULL};

static void file_error(const char *path, int sys_errno)
{
    const FileErrorText *t = &k_io_error;
    size_t i = 0;
    size_t n = 0;
    char *message = NULL;

    if (sys_errno == ENOMEM) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return;
    }
    for (i = 0; i < sizeof k_file_errors / sizeof k_file_errors[0]; i++) {
        if (k_file_errors[i].sys_errno == sys_errno) {
            t = &k_file_errors[i];
        }
    }
    n = strlen(t->before) + strlen(path) + (t->after != NULL ? strlen(t->after) : 0U) + 1U;
    message = (char *)malloc(n);
    if (message == NULL) {
        err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
        return;
    }
    (void)snprintf(message, n, "%s%s%s", t->before, t->after != NULL ? path : "",
                   t->after != NULL ? t->after : "");
    (void)diag_error_file(
        t->code, (DiagSplit){.message = message, .fix = t->fix},
        (DiagFileDetail){.path = path, .reason = t->reason, .sys_errno = sys_errno});
    free(message);
}

static int json_writer(FILE *out, const void *ctx)
{
    return export_json_write(out, (const ExportDoc *)ctx);
}

static int csv_writer(FILE *out, const void *ctx)
{
    return export_csv_write(out, (const ExportDoc *)ctx);
}

static int manifest_writer(FILE *out, const void *ctx)
{
    return export_manifest_write(out, (const ExportDoc *)ctx);
}

/* One output file: its path, its writer, and its temp sibling while writing. */
typedef struct {
    const char *path;
    FileWriteFn fn;
    char *tmp;
} OutFile;

/* Write every file to its temp sibling, then rename each onto its path. On
   failure no temp stays (P5) and the failing file's error is reported.
   ponytail: a rename failing after an earlier one landed leaves that file new
   beside an old one; renames within one folder do not fail in practice. */
static int write_files(OutFile *f, size_t n, const ExportDoc *doc)
{
    int sys_errno = 0;
    size_t i = 0;
    size_t bad = 0;
    int rc = 0;

    for (i = 0; i < n && rc == 0; i++) {
        f[i].tmp = fileio_sibling_name(f[i].path, "tmp", &sys_errno);
        rc = (f[i].tmp == NULL) ? -1 : fileio_write_new(f[i].tmp, f[i].fn, doc, &sys_errno);
        bad = i;
    }
    for (i = 0; i < n && rc == 0; i++) {
        rc = fileio_rename(f[i].tmp, f[i].path, &sys_errno);
        bad = i;
    }
    for (i = 0; i < n; i++) {
        if (rc != 0 && f[i].tmp != NULL) {
            (void)remove(f[i].tmp);
        }
        free(f[i].tmp);
    }
    if (rc != 0) {
        file_error(f[bad].path, sys_errno);
    }
    return rc;
}

typedef struct {
    const ExportDoc *doc;
    const Entry *e;
} MdFile;

static int readme_writer(FILE *out, const void *ctx)
{
    return export_readme_write(out, (const ExportDoc *)ctx);
}

static int md_writer(FILE *out, const void *ctx)
{
    const MdFile *f = (const MdFile *)ctx;

    return export_md_write(out, f->doc, f->e);
}

/* fileio_write_new on "<dir>/<name>". */
static int write_in(const char *dir, const char *name, FileWriteFn fn, const void *ctx,
                    int *sys_errno)
{
    char *p = fileio_join(dir, name, sys_errno);
    int rc = (p == NULL) ? -1 : fileio_write_new(p, fn, ctx, sys_errno);

    free(p);
    return rc;
}

/* The D29 folder at dir: README.txt, the three bin folders, one file per memory. */
static int write_md_tree(const char *dir, const ExportDoc *doc, int *sys_errno)
{
    size_t b = 0;
    size_t i = 0;

    if (fileio_mkdir(dir, sys_errno) != 0 ||
        write_in(dir, "README.txt", readme_writer, doc, sys_errno) != 0) {
        return -1;
    }
    for (b = 0; b <= (size_t)STORE_BIN_DELETED; b++) {
        char *sub = fileio_join(dir, export_md_folder((StoreBin)b), sys_errno);
        int rc = (sub == NULL) ? -1 : fileio_mkdir(sub, sys_errno);

        free(sub);
        if (rc != 0) {
            return -1;
        }
    }
    for (i = 0; i < doc->snap->nentries; i++) {
        const Entry *e = &doc->snap->entries[i];
        const MdFile f = {.doc = doc, .e = e};
        char name[EXPORT_MD_NAME_MAX];
        char rel[sizeof "memories/" + EXPORT_MD_NAME_MAX];

        export_md_name(e, name);
        (void)snprintf(
            rel, sizeof rel, "%s/%s",
            export_md_folder(store_bin_of(e->deleted_at, e->expires_at, doc->exported_at)), name);
        if (write_in(dir, rel, md_writer, &f, sys_errno) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Build the folder beside path, then swap it in (D17). Nothing stays on failure. */
static int write_md(const char *path, const ExportDoc *doc)
{
    int sys_errno = 0;
    char *tmp = fileio_sibling_name(path, "tmp", &sys_errno);
    int rc = -1;

    if (tmp != NULL) {
        rc = write_md_tree(tmp, doc, &sys_errno);
        if (rc == 0) {
            rc = fileio_replace_dir(tmp, path, &sys_errno);
        }
        if (rc != 0) {
            fileio_remove_tree(tmp);
        }
    }
    free(tmp);
    if (rc != 0) {
        file_error(path, sys_errno);
    }
    return rc;
}

/* E6.6: without --force an existing path is a usage error, and nothing is written.
   A folder where a file goes fails even with --force (decision:plan22-export-out-is-folder). */
static int refuse_existing(const OutFile *f, size_t n, const ExportArgs *a)
{
    size_t i = 0;

    for (i = 0; i < n; i++) {
        if (a->fmt != EXPORT_MD && fileio_is_dir(f[i].path)) {
            file_error(f[i].path, EISDIR);
            return -1;
        }
    }
    for (i = 0; i < n; i++) {
        if (!a->force && fileio_exists(f[i].path)) {
            err_fmt(DIAG_USAGE, "output already exists: %s", f[i].path);
            return -1;
        }
    }
    return 0;
}

/* <dir>/<CSV basename>.manifest.json; the basename drops the last extension
   (decision:plan22-export-bins-header). Heap. */
static char *manifest_path(const char *path)
{
    static const char tail[] = ".manifest.json";
    const char *slash = strrchr(path, '/');
    const char *base = (slash != NULL) ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t keep = (dot != NULL && dot > base) ? (size_t)(dot - path) : strlen(path);
    char *out = (char *)malloc(keep + sizeof tail);

    if (out != NULL) {
        (void)snprintf(out, keep + sizeof tail, "%.*s%s", (int)keep, path, tail);
    }
    return out;
}

static int export_reply(bool json, const ExportArgs *a, const char *path,
                        const StoreExportSnapshot *snap)
{
    FILE *out = app_out();

    if (json) {
        const OutputExport reply = {
            .format = a->format, .path = path, .count = snap->nentries, .links = snap->nlinks};
        return output_export_data(out, &reply);
    }
    /* decision:plan22-export-human-count-agrees */
    if (fprintf(out, "exported %zu %s, %zu %s to ", snap->nentries,
                snap->nentries == 1U ? "memory" : "memories", snap->nlinks,
                snap->nlinks == 1U ? "link" : "links") < 0 ||
        output_text_human(out, path, false) != 0 || fputc('\n', out) == EOF) {
        return -1;
    }
    return 0;
}

int cmd_export_reject_args(int rest_argc, const char **rest_argv)
{
    ExportArgs a;

    return parse_args(rest_argc, rest_argv, &a);
}

int cmd_export(Store *s, bool json, int rest_argc, const char **rest_argv)
{
    ExportArgs a;
    char now[ISO_TS_BUFSIZE];
    char path[REMEMBER_PATH_MAX];
    char *manifest = NULL;
    OutFile files[2];
    size_t nfiles = 1;
    StoreExportSnapshot snap;
    StoreStatus st = STORE_OK;
    int rc = REMEMBER_ERR;

    memset(&snap, 0, sizeof(snap));
    memset(files, 0, sizeof(files));
    if (parse_args(rest_argc, rest_argv, &a) != 0) {
        return REMEMBER_ERR;
    }
    if (util_absolute_path(a.out, path, sizeof path) != 0 || utc_now(now, sizeof now) != 0) {
        err_msg(DIAG_INTERNAL, "internal error");
        return REMEMBER_ERR;
    }
    files[0] = (OutFile){.path = path, .fn = (a.fmt == EXPORT_CSV) ? csv_writer : json_writer};
    if (a.fmt == EXPORT_CSV) {
        manifest = manifest_path(path);
        if (manifest == NULL) {
            err_msg(DIAG_OUT_OF_MEMORY, "out of memory");
            return REMEMBER_ERR;
        }
        files[nfiles++] = (OutFile){.path = manifest, .fn = manifest_writer};
    }
    if (refuse_existing(files, nfiles, &a) != 0) {
        goto cleanup;
    }
    st = store_export_snapshot(s, a.bins, now, &snap);
    if (st != STORE_OK) {
        rc = store_status_to_exit(s, st);
        goto cleanup;
    }
    {
        const ExportDoc doc = {
            .snap = &snap, .exported_at = now, .engine = REMEMBER_VERSION, .bins = a.bins};
        int wrote = (a.fmt == EXPORT_MD) ? write_md(path, &doc) : write_files(files, nfiles, &doc);

        if (wrote != 0) {
            goto cleanup;
        }
    }
    if (export_reply(json, &a, path, &snap) != 0) {
        err_msg(DIAG_INTERNAL, "failed to write output");
        goto cleanup;
    }
    rc = REMEMBER_OK;

cleanup:
    store_export_snapshot_free(&snap);
    free(manifest);
    return rc;
}
