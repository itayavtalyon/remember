#ifndef REMEMBER_EXPORT_FORMAT_H
#define REMEMBER_EXPORT_FORMAT_H

#include "store.h"

#include <stdio.h>

/*
 * remember export documents (plan 22). Pure: writes to the stream it is given
 * and opens no file. Escaping is output.c's.
 */

enum { EXPORT_DOC_PAD = 5 };

/* One export's facts: the snapshot, the bins it holds, and the header values. */
typedef struct {
    const StoreExportSnapshot *snap;
    const char *exported_at; /* also the now that set each memory's bin */
    const char *engine;      /* REMEMBER_VERSION */
    StoreExportBins bins;
    char pad_[EXPORT_DOC_PAD]; /* explicit tail padding (kept -Wpadded-clean) */
} ExportDoc;

/*
 * The remember-export JSON document as one compact line, then a newline:
 * format, version, brain_id, exported_at, engine, schema, bins
 * (decision:plan22-export-bins-header), entries, links. An entry has no
 * local_id; its version_vector keys are sorted (D18).
 */
int export_json_write(FILE *out, const ExportDoc *doc);

/* The bin's folder in a Markdown export: memories, expired, deleted (D29). */
const char *export_md_folder(StoreBin bin);

/* The longest file name: a 48-character slug, "--", 12 hex, ".md", NUL. */
enum { EXPORT_MD_NAME_MAX = 48 + 2 + 12 + 3 + 1 };

/*
 * A memory's Markdown file name, "<slug>--<last 12 hex of id>.md" (E6.b2,
 * D12). The slug source is the key, else the "# " title, else the first 6
 * words of the plain text.
 */
void export_md_name(const Entry *e, char out[EXPORT_MD_NAME_MAX]);

/*
 * One Markdown memory file: "---", the front matter lines "<field>: <JSON>"
 * (remember_export, id, key, tags, created_by, updated_by, created_at,
 * updated_at, expires_at, deleted_at, version_vector, links), "---", the
 * body, one newline. links holds the snapshot's links whose from is e,
 * without from (E6.b4, D25).
 */
int export_md_write(FILE *out, const ExportDoc *doc, const Entry *e);

/* README.txt: the D30 sentence, a newline, then the bins line and a newline
   (decision:plan22-export-bins-header). */
int export_readme_write(FILE *out, const ExportDoc *doc);

/*
 * The CSV file (JE-8.3): UTF-8 BOM, the exact header, then one row per entry,
 * CRLF after each line. Every cell is quoted with '"' doubled; NULL is an
 * empty cell. Actor, tags, version_vector and links cells hold JSON; links has
 * the Markdown shape (D25). Never a #settings row.
 */
int export_csv_write(FILE *out, const ExportDoc *doc);

/* The CSV sibling manifest: the JSON header members (format through bins) as
   one object, then a newline (decision:plan22-export-bins-header). */
int export_manifest_write(FILE *out, const ExportDoc *doc);

#endif /* REMEMBER_EXPORT_FORMAT_H */
