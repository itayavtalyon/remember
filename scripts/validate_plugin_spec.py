#!/usr/bin/env python3
"""Validate every JSON Schema and every example in PLUGIN-PROTOCOL.md (revision 6, 2026-10-22).

Schemas: ```json blocks that contain "$schema".
Examples: ```json blocks preceded by <!-- example: NAME --> (one document)
or <!-- example-lines: NAME --> (one document per line).

Extracted files (plan 24): each schema is schema/plugin/v1/<$id file name>.
Each example is a fixture in tests/fixtures/plugin/: at the path of its
`dir/file` caption (the section 2.4 plugins, with their ```python blocks),
else examples/NN-<schema>[-<type>].json, or .jsonl for example-lines. A file
holds the block body plus a newline. --write regenerates both folders.
"""
import argparse, difflib, json, os, re, sys, copy
from pathlib import Path
from jsonschema import Draft202012Validator
from referencing import Registry, Resource

BASE = "https://itayavtalyon.github.io/remember/schema/plugin/v1/"
ROOT = Path(__file__).resolve().parent.parent
SCHEMA_DIR, FIXTURE_DIR = ROOT / "schema/plugin/v1", ROOT / "tests/fixtures/plugin"
ap = argparse.ArgumentParser()
ap.add_argument("spec")
ap.add_argument("--write", action="store_true", help="regenerate the extracted schema and fixture files")
args = ap.parse_args()
path = args.spec
text = open(path, encoding="utf-8").read()

def caption(start):
    """The `dir/file` path that starts the last non-blank line before start, or None."""
    m = re.match(r"`([a-z][a-z0-9_]*/[A-Za-z0-9_][A-Za-z0-9_.]*)`", text[:start].rstrip().rsplit("\n", 1)[-1])
    return m and m.group(1)

blocks = []  # (line, marker, name, body, caption)
for m in re.finditer(r"(?:<!--\s*(example(?:-lines)?):\s*(\S+)\s*-->\s*\n)?```json\n(.*?)\n```", text, re.S):
    line = text[:m.start()].count("\n") + 1
    blocks.append((line, m.group(1), m.group(2), m.group(3), caption(m.start())))

fail = 0
schemas, examples = {}, []
for line, kind, name, body, _ in blocks:
    if '"$schema"' in body:
        try:
            s = json.loads(body)
        except json.JSONDecodeError as e:
            print(f"FAIL line {line}: schema is not valid JSON: {e}"); fail += 1; continue
        try:
            Draft202012Validator.check_schema(s)
        except Exception as e:
            print(f"FAIL line {line}: {s.get('$id')} is not a valid 2020-12 schema: {e.message}"); fail += 1; continue
        schemas[s["$id"]] = s
        print(f"ok   line {line}: schema {s['$id'].replace(BASE, '')}")
    elif kind is None:
        print(f"FAIL line {line}: json block has no example marker"); fail += 1
    else:
        docs = body.splitlines() if kind == "example-lines" else [body]
        for i, d in enumerate(docs):
            try:
                examples.append((line, i, name, json.loads(d)))
            except json.JSONDecodeError as e:
                print(f"FAIL line {line}.{i}: example is not valid JSON: {e}"); fail += 1

registry = Registry().with_resources([(k, Resource.from_contents(v)) for k, v in schemas.items()])

def validator(name):
    return Draft202012Validator(schemas[BASE + name], registry=registry)

# Every $ref must resolve: validate each schema's referenced targets by walking refs.
def refs(o):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == "$ref": yield v
            else: yield from refs(v)
    elif isinstance(o, list):
        for v in o: yield from refs(v)
for sid, s in schemas.items():
    res = registry.resolver(base_uri=sid)
    for r in set(refs(s)):
        try:
            res.lookup(r)
        except Exception as e:
            print(f"FAIL {sid.replace(BASE,'')}: $ref {r} does not resolve: {e}"); fail += 1

ENV_ORDER = ["format", "version", "type", "request_id", "data", "error", "next_cursor", "has_more", "warnings"]
OLD_FORMATS = ('"remember-plugin-message"', '"remember-cli-message"', '"remember-api-message"')  # rev 6 (S-07)
for line, i, name, doc in examples:
    errs = sorted(validator(name).iter_errors(doc), key=lambda e: list(e.path))
    tag = f"line {line}" + (f".{i}" if i else "")
    if errs:
        fail += 1
        for e in errs:
            print(f"FAIL {tag} vs {name}: {'/'.join(map(str, e.path))}: {e.message[:200]}")
    else:
        print(f"ok   {tag}: example ({doc.get('type', doc.get('format'))}) vs {name}")
    keys = list(doc)
    if name in ("envelope.json", "search-response.json") and keys != [k for k in ENV_ORDER if k in doc]:
        print(f"FAIL {tag}: envelope key order {keys}"); fail += 1
    if name != "plugin-status.json" and keys[:2] != ["format", "version"]:
        print(f"FAIL {tag}: format/version are not the first keys"); fail += 1
    if name in ("envelope.json", "search-response.json") and "id" in doc:
        print(f"FAIL {tag}: envelope has an id field (E1)"); fail += 1
    flat = json.dumps(doc)
    for old in ('"sync_id"', '"body": {', '"pong"', '"event_ack"', '"backfill_ack"', '"remember-event"', '"type": "error"',
                '"backfill_done"', '"backfill_pause"', '"backfill_resume"', '"backfill_done.result"', '"applied_through":', '"since":', '"result": "ok"', '"result": "failed"'):
        if old in flat:
            print(f"FAIL {tag}: example still uses {old}"); fail += 1
    # Rev 6 (S-07): one format name.
    for old in OLD_FORMATS:
        if old in flat:
            print(f"FAIL {tag}: example still uses the format {old} (S-07)"); fail += 1
    # Rev 6 (M-17): no hyphen in a plugin or entry name.
    if '-' in str(doc.get("name", "")) or (name == "envelope.json" and '-' in str((doc.get("data") or {}).get("entry", ""))):
        print(f"FAIL {tag}: hyphen in a plugin or entry name (M-17)"); fail += 1
    # Rev 5 (#599): no version vector anywhere in a plugin message.
    if name == "envelope.json" and '"version_vector"' in flat:
        print(f"FAIL {tag}: plugin message carries a version_vector (#599)"); fail += 1
    # Rev 5 (PP-14): the routing key is data.entry, never data.filter.
    if name == "envelope.json" and isinstance(doc.get("data"), dict) and "filter" in doc["data"]:
        print(f"FAIL {tag}: data.filter instead of data.entry (PP-14)"); fail += 1

# Negative tests: each document must FAIL its schema.
def ex(t):
    return copy.deepcopy(next(d for _, _, n, d in examples if n == "envelope.json" and d.get("type") == t))
manifest = copy.deepcopy(next(d for _, _, n, d in examples if n == "manifest.json"))
cli = copy.deepcopy(next(d for _, _, n, d in examples if n == "search-response.json" and "request_id" not in d))
api = copy.deepcopy(next(d for _, _, n, d in examples if n == "search-response.json" and "request_id" in d))
neg = []
d = ex("ping.result"); del d["request_id"]; neg.append(("reply without request_id", "envelope.json", d))
d = ex("ping"); del d["request_id"]; neg.append(("request without request_id", "envelope.json", d))
d = ex("ready"); d["request_id"] = "01a14524-0000-7000-8000-000000000399"; neg.append(("notification with request_id", "envelope.json", d))
d = ex("ping"); del d["type"]; neg.append(("message without type", "envelope.json", d))
d = ex("ping"); del d["format"]; neg.append(("message without format", "envelope.json", d))
d = ex("ping"); d["body"] = d.pop("data"); neg.append(("old body field instead of data", "envelope.json", d))
d = ex("ping.result"); d["error"] = copy.deepcopy(next(x for _, _, n, x in examples if n == "envelope.json" and "error" in x)["error"]); neg.append(("reply with data and error", "envelope.json", d))
ERR = copy.deepcopy(next(x for _, _, n, x in examples if n == "envelope.json" and "error" in x)["error"])
d = ex("ping"); d["error"] = copy.deepcopy(ERR); del d["data"]; neg.append(("request carrying error", "envelope.json", d))
d = ex("ping.result"); del d["data"]; d["type"] = "error"; d["error"] = copy.deepcopy(ERR); neg.append(("type error (Q-A removed it)", "envelope.json", d))
d = ex("ping.result"); del d["data"]; d["error"] = copy.deepcopy(ERR); del d["request_id"]; neg.append(("failed reply without request_id", "envelope.json", d))
d = ex("ping.result"); d["type"] = "foo.bar.result"; neg.append(("unknown .result type carrying data", "envelope.json", d))
d = ex("ping.result"); del d["data"]; d["type"] = "foo.bar.result"; d["error"] = copy.deepcopy(ERR); neg.append(("unknown .result type with a code other than unknown_message_type", "envelope.json", d))
d = ex("ping"); d["type"] = "foo.bar"; neg.append(("unknown request type", "envelope.json", d))
d = ex("ping.result"); d["next_cursor"] = None; d["has_more"] = False; neg.append(("paging on a plugin message", "envelope.json", d))
d = ex("ping.result"); d["warnings"] = []; neg.append(("empty warnings on a plugin message", "envelope.json", d))
d = ex("event"); d["data"]["memory"]["local_id"] = 7; neg.append(("event memory with local_id", "envelope.json", d))
d = ex("event"); m = d["data"]["memory"]; m["sync_id"] = m.pop("id"); neg.append(("event memory with sync_id instead of id", "envelope.json", d))
d = ex("event"); d["data"]["entries"] = [d["data"].pop("memory")]; neg.append(("event with entries[0] instead of memory", "envelope.json", d))
d = ex("event"); d["data"]["event"] = {"id": d["data"].pop("event_id"), "type": d["data"].pop("event_type")}; neg.append(("event with old event{} object", "envelope.json", d))
d = ex("event"); del d["data"]["plugin"]; neg.append(("event data missing plugin", "envelope.json", d))
d = ex("event"); d["data"]["settings"] = {"plugin.url_expand.enabled": True}; neg.append(("settings as a keyed object", "envelope.json", d))
d = ex("event"); d["data"]["settings"] = [{"key": "plugin.url-expand.enabled", "value": True}]; neg.append(("hyphenated setting key (M-17, ST-1.1)", "envelope.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["permissions"]["needs"] = ["remember.search.max-results"]; neg.append(("hyphenated global setting key in needs (M-17, ST-1.1)", "manifest.json", d))
d = ex("filter.search.results.result"); d["data"]["results"][0]["sync_id"] = d["data"]["results"][0].pop("memory_id"); neg.append(("result item with sync_id", "envelope.json", d))
d = ex("filter.search.query.result"); d["data"]["query"]["text"] = "   "; neg.append(("blank rewritten query", "envelope.json", d))
d = ex("filter.search.query.result"); d["data"]["query"]["time_range"]["field"] = "expires_at"; neg.append(("time_range on an unsupported field", "envelope.json", d))
d = ex("filter.search.query.result"); del d["data"]["query"]["time_range"]["field"]; neg.append(("time_range without field", "envelope.json", d))
d = ex("ready"); d["format"] = "remember-plugin"; neg.append(("envelope with manifest format", "envelope.json", d))
d = ex("ping"); d["request_id"] = "req_01a14524"; neg.append(("prefixed request_id", "envelope.json", d))
d = copy.deepcopy(next(x for _, _, n, x in examples if n == "envelope.json" and "error" in x)); del d["error"]["fix"]; neg.append(("error without fix", "envelope.json", d))
d = ex("backfill"); d["data"]["entries"] *= 101; neg.append(("backfill batch over 100", "envelope.json", d))
d = ex("backfill"); del d["data"]["entries"][0]["change_seq"]; neg.append(("backfill entry without change_seq (#599)", "envelope.json", d))
d = ex("backfill"); del d["data"]["since_seq"]; neg.append(("catch_up backfill without since_seq (#599)", "envelope.json", d))
d = ex("backfill"); d["data"]["since"] = {"01a13651-e244-717d-bd40-7850d5e2f27d": 1}; neg.append(("catch_up backfill with an old since vector (#599)", "envelope.json", d))
d = ex("backfill"); d["data"]["entries"][0]["version_vector"] = {"01a13651-e244-717d-bd40-7850d5e2f27d": 2}; neg.append(("backfill entry with a version_vector (#599)", "envelope.json", d))
d = ex("event"); d["data"]["memory"]["version_vector"] = {}; neg.append(("event memory with a version_vector (#599)", "envelope.json", d))
d = copy.deepcopy(next(x for _, _, n, x in examples if n == "envelope.json" and x.get("type") == "ping.result" and "backfill_needed" in x["data"]))
d["data"]["backfill_needed"] = {"reason": "data_version", "last_applied_version": 2}; neg.append(("old backfill_needed shape", "envelope.json", d))
d = copy.deepcopy(manifest); d["actions"][0]["run"]["shell"] = True; neg.append(("unknown field in run", "manifest.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["permissions"]["env"] = {}; neg.append(("unknown field in permissions", "manifest.json", d))
d = copy.deepcopy(manifest); del d["filters"]; d["actions"][0]["run"]["process"] = "once"; neg.append(("backfill without persistent entry", "manifest.json", d))
d = copy.deepcopy(manifest); d["backfill"]["max_batch_entries"] = 500; neg.append(("backfill batch bigger than 100", "manifest.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["permissions"]["needs"] = ["plugin.other.secret"]; neg.append(("needs another plugin's setting", "manifest.json", d))
d = copy.deepcopy(manifest); d["settings"][0]["key"] = "enabled"; neg.append(("generated setting key enabled (S-06)", "manifest.json", d))
d = copy.deepcopy(cli); d["warnings"] = []; neg.append(("empty warnings", "search-response.json", d))
d = copy.deepcopy(cli); d["error"] = {"code": "internal", "message": "x", "retryable": False}; neg.append(("data and error together", "search-response.json", d))
d = copy.deepcopy(cli); d["data"]["filters"][1].pop("reason"); neg.append(("timeout filter without reason", "search-response.json", d))
d = copy.deepcopy(cli); d["next_cursor"] = None; d["has_more"] = False; neg.append(("search with paging fields (S-08)", "search-response.json", d))
d = copy.deepcopy(cli); del d["format"]; neg.append(("response without format (D28)", "search-response.json", d))
d = copy.deepcopy(cli); del d["type"]; neg.append(("response without type (D28)", "search-response.json", d))
d = copy.deepcopy(cli); d["version"] = 2; neg.append(("response with version 2 (S-07)", "search-response.json", d))
d = copy.deepcopy(cli); d["request_id"] = "01a14530-0000-7000-8000-0000000000ab"; neg.append(("response with request_id and local_id (J6)", "search-response.json", d))
d = copy.deepcopy(api); d["format"] = "remember-api-message"; neg.append(("old API format name (S-07)", "search-response.json", d))
d = copy.deepcopy(api); d["data"]["entries"][0]["local_id"] = 41; neg.append(("API response with local_id", "search-response.json", d))
d = copy.deepcopy(cli); d["next_cursor"] = "abc"; neg.append(("search with a cursor", "search-response.json", d))
d = ex("ready"); del d["data"]["index_version"]; neg.append(("ready without index_version (515)", "envelope.json", d))
d = ex("ping.result"); del d["data"]["index_version"]; neg.append(("ping.result without index_version (515)", "envelope.json", d))
d = ex("ready"); d["data"]["index_version"] = {"data_version": 3}; neg.append(("index_version without model (515)", "envelope.json", d))
d = copy.deepcopy(next(x for _, _, n, x in examples if n == "envelope.json" and x.get("type") == "ping.result" and "backfill_needed" in x["data"]))
d["data"]["backfill_needed"] = {"index_version": d["data"]["index_version"]}; neg.append(("backfill_needed without applied_through_seq (515, #599)", "envelope.json", d))
d = ex("ready"); d["data"]["backfill_needed"] = {"applied_through": {}}; neg.append(("old applied_through vector (rev 4, #599)", "envelope.json", d))
d = ex("ready"); d["data"]["backfill_needed"] = {"applied_through_seq": -1}; neg.append(("negative applied_through_seq", "envelope.json", d))
d = ex("event.result"); d["data"]["result"] = "ok"; neg.append(("event.result with a result field (PP-08)", "envelope.json", d))
d = ex("ping"); d["type"] = "backfill_done"; neg.append(("old type backfill_done (PP-07)", "envelope.json", d))
d = ex("ping"); d["type"] = "backfill_pause"; del d["request_id"]; neg.append(("old type backfill_pause (PP-07)", "envelope.json", d))
d = ex("filter.search.results"); d["data"]["filter"] = d["data"].pop("entry"); neg.append(("filter request with data.filter instead of data.entry (PP-14)", "envelope.json", d))
d = ex("filter.search.query"); del d["data"]["entry"]; neg.append(("filter request without data.entry (PP-14)", "envelope.json", d))
d = ex("event"); del d["data"]["entry"]; neg.append(("event without data.entry (PP-14)", "envelope.json", d))
d = ex("event"); d["data"]["plugin"]["action"] = "fetch"; neg.append(("event with data.plugin.action (PP-14)", "envelope.json", d))
d = ex("event"); del d["data"]["cause"]["origin"]; neg.append(("event cause without origin (#577)", "envelope.json", d))
d = ex("event"); d["data"]["cause"]["origin"] = "share"; neg.append(("event cause with an unknown origin (#577)", "envelope.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["permissions"]["needs"] = ["fortkeeps.library.link_enrichment"]; neg.append(("needs a fortkeeps.* key (#571)", "manifest.json", d))
d = copy.deepcopy(manifest); d["actions"][0]["permissions"]["needs"].append("version_vector"); neg.append(("needs version_vector (#599)", "manifest.json", d))
st = copy.deepcopy(next(x for _, _, n, x in examples if n == "plugin-status.json"))
d = copy.deepcopy(st); del d["last_problems"]; neg.append(("plugin status without last_problems (PP-05)", "plugin-status.json", d))
d = copy.deepcopy(st); d["disabled_reason"] = "broken"; neg.append(("plugin status with an unknown disabled_reason (PP-09)", "plugin-status.json", d))
d = copy.deepcopy(manifest); d["name"] = "url-expand"; neg.append(("hyphen in a plugin name (M-17)", "manifest.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["name"] = "fetch-title"; neg.append(("hyphen in an entry name (M-17)", "manifest.json", d))
d = copy.deepcopy(manifest); d["actions"][0]["events"] = ["memory.linked"]; neg.append(("link event type (S-10)", "manifest.json", d))
d = copy.deepcopy(manifest); d["actions"][0]["filter"] = {"source": ["agent"]}; neg.append(("source in an action filter (G2-10, R9.19)", "manifest.json", d))
d = ex("event"); d["data"]["plugin"] = None; neg.append(("event with plugin null (S-01)", "envelope.json", d))
d = ex("event"); d["data"]["cause"]["plugin"] = "url_expand"; neg.append(("event cause.plugin (R3.5a)", "envelope.json", d))
d = ex("ping"); d["format"] = "remember-plugin-message"; neg.append(("old plugin format name (S-07)", "envelope.json", d))
cold = copy.deepcopy(next(d for _, _, n, d in examples if n == "search-response.json" and any(f.get("reason") == "cold_grace" for f in d.get("data", {}).get("filters", []))))
d = copy.deepcopy(cold); del d["data"]["filters"][0]["reason"]; neg.append(("cold fallback filter without reason (SET-2)", "search-response.json", d))
for label, name, doc in neg:
    if validator(name).is_valid(doc):
        print(f"FAIL negative: '{label}' passed {name}"); fail += 1
    else:
        print(f"ok   negative: '{label}' is rejected by {name}")

# Code checks Remember runs on plugin.json that a schema cannot express.
# Rev 6 (S-06): the CF-7 entry-name collision rule is deleted, so the only code check left here is R6.13 below.
code_neg = []
# R6.13: the reported index_version.data_version equals the manifest data_version (default 1).
mdv = manifest.get("data_version", 1)
for line, i, name, doc in examples:
    if name == "envelope.json" and doc.get("type") in ("ready", "ping.result") and "data" in doc:
        if doc["data"].get("index_version", {}).get("data_version") != mdv:
            print(f"FAIL line {line}: {doc['type']} reports data_version {doc['data'].get('index_version')} but the manifest has {mdv}"); fail += 1
# Positive edge cases.
pos = []
d = copy.deepcopy(manifest); d["homepage"] = "https://example.org"; pos.append(("unknown top-level manifest field (warning, not error)", "manifest.json", d))
d = ex("ping.result"); d["data"]["uptime_ms"] = 5; pos.append(("unknown data field (warning, not error)", "envelope.json", d))
d = ex("filter.search.results.result"); d["data"]["note"] = "x" * 300; pos.append(("long note is not malformed (R9.17)", "envelope.json", d))
d = ex("ping.result"); del d["data"]; d["error"] = copy.deepcopy(ERR); pos.append(("failed ping.result keeps its type (Q-A)", "envelope.json", d))
d = ex("ping.result"); del d["data"]; d["type"] = "memory.vacuum.result"; d["error"] = {"code": "unknown_message_type", "message": "This plugin does not know memory.vacuum.", "fix": "Update the plugin.", "retryable": False}; pos.append(("failed reply to an unknown request type (R9.22)", "envelope.json", d))
d = copy.deepcopy(cli); del d["data"]; d["error"] = {"code": "busy", "message": "The brain is locked.", "fix": "Try again.", "retryable": True}; pos.append(("failed CLI search keeps memory.search.result", "search-response.json", d))
d = ex("ping.result"); d["warnings"] = [{"code": "model_warming", "message": "The model is still loading."}]; pos.append(("plugin reply with warnings (logged only, NQ-17)", "envelope.json", d))
d = copy.deepcopy(manifest); d["filters"][0]["name"] = "budget"; pos.append(("filter entry named budget or enabled (no collision rule, S-06)", "manifest.json", d))
d = copy.deepcopy(manifest); d["actions"][0]["filter"] = {"origins": ["local", "sync"], "created_by": ["agent"], "not_caused_by": ["plugin/hybrid/index"]}; pos.append(("action filter with origins and actor kinds (R2.21)", "manifest.json", d))
d = copy.deepcopy(st); d["disabled_reason"] = "untrusted"; d["enabled"] = True; pos.append(("plugin status disabled_reason untrusted (S-05)", "plugin-status.json", d))
d = ex("ready"); d["data"]["index_version"]["model"] = None; pos.append(("index_version with model null (515)", "envelope.json", d))
d = ex("ready"); d["data"]["backfill_needed"] = {"applied_through_seq": 0}; pos.append(("backfill_needed with applied_through_seq 0 asks for every memory (R6.14, #599)", "envelope.json", d))
d = copy.deepcopy(manifest); d["idle_timeout"] = "60m"; pos.append(("manifest idle_timeout (R2.20, #600)", "manifest.json", d))
d = copy.deepcopy(st); d["disabled_reason"] = "protocol_mismatch"; d["enabled"] = False; pos.append(("plugin status disabled_reason protocol_mismatch (R6.2a)", "plugin-status.json", d))
for label, name, doc in pos:
    ok = validator(name).is_valid(doc)
    if not ok: fail += 1
    print(("ok   positive: " if ok else "FAIL positive: ") + label)

# Extracted files: each must hold the same bytes as its spec block.
files = {}  # path -> (spec line, bytes)
def put(p, line, want):
    global fail
    if p in files:
        print(f"FAIL line {line}: {p.relative_to(ROOT)} is also extracted from line {files[p][0]}"); fail += 1
    files[p] = (line, want)
for line, kind, name, body, cap in blocks:
    if '"$schema"' in body:
        try:
            put(SCHEMA_DIR / json.loads(body)["$id"].rsplit("/", 1)[-1], line, body + "\n")
        except (json.JSONDecodeError, KeyError):
            pass  # reported above
for m in re.finditer(r"```python\n(.*?)\n```", text, re.S):
    line, cap = text[:m.start()].count("\n") + 1, caption(m.start())
    if cap:
        put(FIXTURE_DIR / cap, line, m.group(1) + "\n")
    else:
        print(f"FAIL line {line}: python block has no `dir/file` caption"); fail += 1
n = 0
for line, kind, name, body, cap in blocks:
    if kind is None:
        continue
    if cap:
        put(FIXTURE_DIR / cap, line, body + "\n")
        continue
    n += 1
    try:
        t = json.loads(body.splitlines()[0] if kind == "example-lines" else body).get("type")
    except json.JSONDecodeError:
        t = None  # reported above
    stem = f"{n:02d}-{name.removesuffix('.json')}" + (f"-{t}" if t else "")
    put(FIXTURE_DIR / "examples" / (stem + (".jsonl" if kind == "example-lines" else ".json")), line, body + "\n")
on_disk = {p for d in (SCHEMA_DIR, FIXTURE_DIR) if d.is_dir() for p in d.rglob("*") if p.is_file() and not p.name.startswith(".")}
if args.write:
    for p in on_disk - files.keys():
        p.unlink()
    for p, (_, want) in files.items():
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(want, encoding="utf-8", newline="")
        if want.startswith("#!"):
            p.chmod(0o755)
    on_disk = set(files)
for p in sorted(on_disk - files.keys()):
    print(f"FAIL file {p.relative_to(ROOT)}: not extracted from the spec (run with --write)"); fail += 1
for p, (line, want) in sorted(files.items()):
    rel = p.relative_to(ROOT)
    if p not in on_disk:
        print(f"FAIL file {rel}: missing, spec line {line} (run with --write)"); fail += 1; continue
    have = p.read_bytes().decode("utf-8", "replace")
    if have != want:
        print(f"FAIL file {rel}: differs from spec line {line} (run with --write)"); fail += 1
        if have.rstrip("\n") == want.rstrip("\n"):
            print("  only the final newline differs")
        else:
            sys.stdout.writelines(list(difflib.unified_diff(want.splitlines(True), have.splitlines(True), "spec", str(rel)))[:40])
    elif want.startswith("#!") and not os.access(p, os.X_OK):
        print(f"FAIL file {rel}: is not executable"); fail += 1
    else:
        print(f"ok   file {rel}: spec line {line}")
print(f"\n{len(files)} extracted files: {sum(p.parent == SCHEMA_DIR for p in files)} schemas, {sum(p.parent != SCHEMA_DIR for p in files)} fixtures")
print(f"{len(schemas)} schemas, {len(examples)} examples, {len(neg)} negative tests, {len(code_neg)} code-check negative tests, {len(pos)} positive edge tests, {fail} failures")
sys.exit(1 if fail else 0)
