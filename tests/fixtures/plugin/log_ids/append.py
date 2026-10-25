#!/usr/bin/env python3
import json, os, sys
msg = json.loads(sys.stdin.readline())
d = msg["data"]
with open(os.path.join(d["data_dir"], "ids.txt"), "a") as f:
    f.write(d["memory"]["id"] + "\n")
sys.exit(0)
