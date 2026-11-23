#ifndef REMEMBER_PLUGIN_TRUST_H
#define REMEMBER_PLUGIN_TRUST_H

#include "plugin_manifest.h"
#include "remember_env.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * <data dir>/trust.json (specs/PLUGIN-TRUST.md, decision:2026-11-12-trust-file). Trust belongs to
 * the code hash: Remember records it only after the host confirm callback says yes, and a
 * mismatch of any stored hash means untrusted. This module reads and writes the file and hashes
 * the code; it prints nothing (diag.h prints the outcomes) and starts no process.
 */

enum {
    PLUGIN_TRUST_FILE_MAX = 1048576, /* the most bytes of trust.json a read takes */
    PLUGIN_TRUST_ENTRIES_MAX = 1024, /* entries in the file */
    PLUGIN_TRUST_FILES_MAX = 8,      /* argv files in one record (G-29) */
    PLUGIN_TRUST_NEEDS_MAX = 512,    /* needs in one record */
    PLUGIN_TRUST_FOLDER_FILES_MAX = 4096,
    PLUGIN_TRUST_FOLDER_DIRS_MAX = 4096,
    PLUGIN_TRUST_DEPTH_MAX = 16,
    PLUGIN_TRUST_HASHED_MAX = 536870912, /* 512 MiB read in all for one subject
                                            (decision:2026-11-23-plan33-total-hash-cap) */
    PLUGIN_TRUST_LOCK_TIMEOUT_MS = 2000,
    PLUGIN_TRUST_COMMANDS_MAX = 32,
    PLUGIN_TRUST_HEX = 64 /* hex digits of a SHA-256 */
};

typedef enum PluginTrustKind {
    PLUGIN_TRUST_KIND_PLUGIN = 0,
    PLUGIN_TRUST_KIND_HOOK
} PluginTrustKind;

typedef enum PluginTrustStatus {
    /* check: the record matches the code. allow: the record is stored. */
    PLUGIN_TRUST_OK = 0,
    /* check: no record, or a stored hash differs from the code. Nothing is deleted. */
    PLUGIN_TRUST_UNTRUSTED,
    /* The code cannot be hashed (decision:2026-11-22-plan33-folder-hash, -hash-caps,
       -many-entries): a symlink or special file in the folder, a cap passed, a file that exists
       and cannot be read, a path that is not UTF-8, more than 8 argv files, two interpreters. It
       is never trusted. */
    PLUGIN_TRUST_UNHASHABLE,
    /* trust.json cannot be used (decision:2026-11-22-plan33-trust-json-unreadable): every check is
       untrusted and nothing is written over the file. */
    PLUGIN_TRUST_UNREADABLE,
    /* The lock, the temp file, the rename or the data dir failed; the previous file stays. */
    PLUGIN_TRUST_IO,
    /* Another process held the lock past PLUGIN_TRUST_LOCK_TIMEOUT_MS: nothing was written. */
    PLUGIN_TRUST_BUSY,
    /* confirm: NULL callback (exit 5, plugin_approval_required or hook_approval_required). */
    PLUGIN_TRUST_NO_CALLBACK,
    /* confirm: the callback said no (exit 1, plugin_approval_denied). */
    PLUGIN_TRUST_DENIED,
    PLUGIN_TRUST_OOM
} PluginTrustStatus;

/* One run.command: its argv (R2.8: no shell) and the absolute folder it runs in, NULL for the
   plugin folder (decision:2026-11-23-plan33-argv-cwd). */
typedef struct PluginTrustCommand {
    const char *const *argv;
    size_t argc;
    const char *cwd;
} PluginTrustCommand;

/*
 * The code a trust record is about. The caller builds it from the parsed manifest: commands holds
 * the run.command of each entry in entry order, needs the distinct permissions.needs of the
 * entries. folder is the installed plugin folder (it holds plugin.json). A hook is stored under
 * its command path, which is argv[0] of its one command; a plugin under its folder.
 * search_path is the PATH that /usr/bin/env finds an interpreter in (NULL finds none).
 * manifest is the plugin.json bytes commands and needs were parsed from (required, at most
 * PLUGIN_JSON_MAX): manifest_sha256 is the hash of these bytes, never of a second read of the file,
 * so a plugin.json swapped after the parse cannot pass a check for the argv the caller will run.
 */
typedef struct PluginTrustSubject {
    const char *name;
    const char *folder;
    const char *search_path;
    const char *manifest;
    size_t manifest_len;
    const PluginTrustCommand *commands;
    const char *const *needs;
    size_t ncommands;
    size_t nneeds;
    PluginTrustKind kind;
    bool dev;
    char pad_[3]; /* explicit tail padding (kept -Wpadded-clean) */
} PluginTrustSubject;

/* <data dir>/trust.json, heap; NULL when memory runs out. */
char *plugin_trust_file_path(const char *data_dir);

/*
 * Is the code of subject what the person allowed? PLUGIN_TRUST_OK, PLUGIN_TRUST_UNTRUSTED,
 * PLUGIN_TRUST_UNHASHABLE, PLUGIN_TRUST_UNREADABLE or PLUGIN_TRUST_OOM. Only OK is trusted. The
 * built-in plugin (PLUGIN_HYBRID_NAME) is trusted and has no record. The stored folder_sha256,
 * manifest_sha256, files (path and sha256) and interpreter are compared with the code now;
 * size, mtime, needs, dev and trusted_at are not.
 */
PluginTrustStatus plugin_trust_check(const char *data_dir, const PluginTrustSubject *subject);

/* Ask the host: env->confirm(callback_ctx, "plugin" or "hook", name, body). OK when it says yes.
   A NULL env, or a NULL callback, is PLUGIN_TRUST_NO_CALLBACK, never a yes (decision:g2-13); a
   false return is PLUGIN_TRUST_DENIED. */
PluginTrustStatus plugin_trust_confirm(const RememberEnv *env, PluginTrustKind kind,
                                       const char *name, const char *body);

/*
 * An installed plugin's code as a subject: <data dir>/plugins/<name>/plugin.json parsed, one
 * command for each entry (entry order), the distinct needs of the entries. A folder with no
 * plugin.json, a manifest with an error or another name, or a name that is not one path segment
 * is PLUGIN_TRUST_UNHASHABLE: the code of that name cannot be told, so it is not trusted.
 * out->subject points into memory the struct owns; free it with plugin_trust_installed_free
 * (safe on a zeroed struct).
 */
typedef struct PluginTrustInstalled {
    PluginTrustSubject subject;
    PluginManifest *manifest;
    PluginTrustCommand *commands;
    const char **argv_pool; /* PLUGIN_ARGS_MAX argv pointers for each command */
    const char **needs;
    char *folder;
    char *manifest_bytes; /* the plugin.json bytes subject.manifest points at */
} PluginTrustInstalled;

PluginTrustStatus plugin_trust_load_installed(const char *data_dir, const char *name,
                                              PluginTrustKind kind, const char *search_path,
                                              PluginTrustInstalled *out);
void plugin_trust_installed_free(PluginTrustInstalled *installed);

/* plugin_trust_check of the installed plugin called name, of the kind its record says (a hook's
   record is kind hook). No record is PLUGIN_TRUST_UNTRUSTED. The built-in is trusted. */
PluginTrustStatus plugin_trust_check_installed(const char *data_dir, const char *name,
                                               const char *search_path);

/*
 * Hash the code, ask the host (the body of H-9.4, H-9.6: for an update only the needs the old
 * record lacks), and only after a yes store the record, replacing the one of that name. This is
 * the one function that writes trust.json. now is trusted_at (RFC 3339 UTC). With no yes, or on
 * any failure, the file is as it was. The built-in plugin has no record: OK, nothing stored.
 */
PluginTrustStatus plugin_trust_allow(const char *data_dir, const RememberEnv *env,
                                     const PluginTrustSubject *subject, const char *now);

#endif /* REMEMBER_PLUGIN_TRUST_H */
