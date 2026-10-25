#!/usr/bin/env python3
import json, os, socket
path = os.environ["REMEMBER_PLUGIN_SOCKET"]
srv = socket.socket(socket.AF_UNIX); srv.bind(path); srv.listen()
msg = lambda **kw: {"format": "remember-message", "version": 1, **kw}
iv = {"data_version": 1, "model": None}
print(json.dumps(msg(type="ready", data={"protocol": 1, "pid": os.getpid(), "index_version": iv})), flush=True)
while True:
    conn, _ = srv.accept(); f = conn.makefile("rw")
    req = json.loads(f.readline()); t, d = req["type"], req.get("data", {})
    if t == "shutdown": os.unlink(path); break
    out = msg(type=t + ".result", request_id=req["request_id"])
    if t == "ping": out["data"] = {"pid": os.getpid(), "index_version": iv}
    elif t == "filter.search.results":
        words = d["query"]["text"].lower().split()
        hits = lambda c: sum(w in c.get("body", "").lower() for w in words)
        ranked = sorted(d["candidates"], key=hits, reverse=True)
        out["data"] = {"results": [{"memory_id": c["id"], "score": hits(c)} for c in ranked]}
    else: out["error"] = {"code": "unknown_message_type", "message": f"hello can't handle {t}.",
                          "fix": "Upgrade the hello plugin.", "retryable": False}
    f.write(json.dumps(out) + "\n"); f.flush(); conn.close()
