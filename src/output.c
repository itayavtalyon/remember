#include "output.h"

#include "store.h"

#include <stdio.h>
#include <string.h>

static const char *empty_str(void)
{
    static const char e[] = "";
    return e;
}

static int write_json_escape(FILE *out, unsigned char c)
{
    switch (c) {
    case '"':
        return fputs("\\\"", out);
    case '\\':
        return fputs("\\\\", out);
    case '\b':
        return fputs("\\b", out);
    case '\f':
        return fputs("\\f", out);
    case '\n':
        return fputs("\\n", out);
    case '\r':
        return fputs("\\r", out);
    case '\t':
        return fputs("\\t", out);
    default:
        if (c < 0x20U) {
            return fprintf(out, "\\u%04x", (unsigned)c);
        }
        return fputc((int)c, out);
    }
}

int output_json_string(FILE *out, const char *s)
{
    const unsigned char *p;

    if (out == NULL) {
        return -1;
    }
    if (s == NULL) {
        s = empty_str();
    }
    if (fputc('"', out) == EOF) {
        return -1;
    }
    for (p = (const unsigned char *)s; *p != '\0'; p++) {
        if (write_json_escape(out, *p) < 0) {
            return -1;
        }
    }
    if (fputc('"', out) == EOF) {
        return -1;
    }
    return 0;
}

static int write_json_field_str(FILE *out, const char *name, const char *value)
{
    if (fprintf(out, ",\"%s\":", name) < 0) {
        return -1;
    }
    return output_json_string(out, value != NULL ? value : empty_str());
}

int output_entry_json(FILE *out, const Entry *e)
{
    size_t i;

    if (out == NULL || e == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"id\":%lld,\"key\":", e->id) < 0) {
        return -1;
    }
    if (e->key == NULL) {
        if (fputs("null", out) < 0) {
            return -1;
        }
    } else if (output_json_string(out, e->key) != 0) {
        return -1;
    }
    if (write_json_field_str(out, "body", e->body) != 0) {
        return -1;
    }
    if (fputs(",\"tags\":[", out) < 0) {
        return -1;
    }
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_json_string(out, e->tags[i]) != 0) {
            return -1;
        }
    }
    if (fputs("]", out) < 0) {
        return -1;
    }
    if (write_json_field_str(out, "source", e->source != NULL ? e->source : "unknown") != 0) {
        return -1;
    }
    if (write_json_field_str(out, "created_at", e->created_at) != 0) {
        return -1;
    }
    if (write_json_field_str(out, "updated_at", e->updated_at) != 0) {
        return -1;
    }
    if (fputc('}', out) == EOF) {
        return -1;
    }
    return 0;
}

int output_action_envelope(FILE *out, const char *action, const Entry *e)
{
    if (out == NULL || action == NULL || e == NULL) {
        return -1;
    }
    if (fprintf(out, "{\"version\":1,\"action\":") < 0) {
        return -1;
    }
    if (output_json_string(out, action) != 0) {
        return -1;
    }
    if (fputs(",\"count\":1,\"entries\":[", out) < 0) {
        return -1;
    }
    if (output_entry_json(out, e) != 0) {
        return -1;
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_get_envelope(FILE *out, const Entry *e)
{
    if (out == NULL || e == NULL) {
        return -1;
    }
    if (fputs("{\"version\":1,\"count\":1,\"entries\":[", out) < 0) {
        return -1;
    }
    if (output_entry_json(out, e) != 0) {
        return -1;
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_list_envelope(FILE *out, size_t offset, size_t limit, size_t count, size_t total,
                         const Entry *entries)
{
    size_t i;

    if (out == NULL) {
        return -1;
    }
    if (count > 0U && entries == NULL) {
        return -1;
    }
    if (fprintf(out,
                "{\"version\":1,\"offset\":%zu,\"limit\":%zu,\"count\":%zu,\"total\":%zu,"
                "\"entries\":[",
                offset, limit, count, total) < 0) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (i > 0U && fputc(',', out) == EOF) {
            return -1;
        }
        if (output_entry_json(out, &entries[i]) != 0) {
            return -1;
        }
    }
    if (fputs("]}\n", out) < 0) {
        return -1;
    }
    return 0;
}

int output_id_human(FILE *out, long long id)
{
    if (out == NULL) {
        return -1;
    }
    if (fprintf(out, "%lld\n", id) < 0) {
        return -1;
    }
    return 0;
}

/* Control byte that could drive the terminal (drop \n and \t, which are safe
   whitespace we want to keep). Shared intent with write_preview's '?' mapping. */
static int is_terminal_ctrl(unsigned char c)
{
    if (c == (unsigned char)'\n' || c == (unsigned char)'\t') {
        return 0;
    }
    return c < 0x20U || c == 0x7FU;
}

int output_body_human(FILE *out, const char *body)
{
    if (out == NULL) {
        return -1;
    }
    if (body != NULL) {
        const unsigned char *p;
        for (p = (const unsigned char *)body; *p != '\0'; p++) {
            int ch = is_terminal_ctrl(*p) ? '?' : (int)*p;
            if (fputc(ch, out) == EOF) {
                return -1;
            }
        }
    }
    if (fputc('\n', out) == EOF) {
        return -1;
    }
    return 0;
}

static size_t utf8_clen(unsigned char c)
{
    if ((c & 0x80U) == 0U) {
        return 1U;
    }
    if ((c & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((c & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((c & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

/* First line, max 80 codepoints, ellipsis if truncated. Control bytes → '?'. */
static void write_preview(FILE *out, const char *body)
{
    size_t i = 0U;
    size_t cps = 0U;
    const size_t max_cp = 80U;
    int truncated = 0;

    if (body == NULL) {
        return;
    }
    while (body[i] != '\0' && body[i] != '\n' && body[i] != '\r') {
        unsigned char c = (unsigned char)body[i];
        size_t clen = utf8_clen(c);

        if (body[i + clen - 1U] == '\0') {
            clen = 1U;
        }
        if (cps >= max_cp) {
            truncated = 1;
            break;
        }
        if (c < 0x20U || c == 0x7FU) {
            (void)fputc('?', out);
        } else {
            size_t k;
            for (k = 0; k < clen && body[i + k] != '\0'; k++) {
                (void)fputc((unsigned char)body[i + k], out);
            }
        }
        i += clen;
        cps++;
    }
    if (body[i] != '\0' && body[i] != '\n' && body[i] != '\r') {
        truncated = 1;
    }
    if (truncated) {
        (void)fputs("...", out);
    }
}

int output_entry_human_line(FILE *out, const Entry *e)
{
    size_t i;

    if (out == NULL || e == NULL) {
        return -1;
    }
    if (fprintf(out, "%lld | ", e->id) < 0) {
        return -1;
    }
    /* Keys/tags are normalize_token output: no ASCII controls. Raw fputs is safe. */
    if (e->key != NULL) {
        (void)fputs(e->key, out);
    }
    (void)fputs(" | ", out);
    for (i = 0; i < e->ntags; i++) {
        if (i > 0U) {
            (void)fputc(',', out);
        }
        if (e->tags[i] != NULL) {
            (void)fputs(e->tags[i], out);
        }
    }
    (void)fputs(" | ", out);
    write_preview(out, e->body);
    (void)fputs(" | ", out);
    if (e->updated_at != NULL) {
        (void)fputs(e->updated_at, out);
    }
    (void)fputc('\n', out);
    return 0;
}
