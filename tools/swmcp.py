#!/usr/bin/env python3
"""Minimal JSON-RPC client for the console MCP server.
usage: swmcp.py TOOL '{json args}' [--out FILE]   (screenshot -> writes JPEG to --out)
       swmcp.py --list
"""
import json, sys, base64, urllib.request
URL = "http://10.88.111.28:57331/mcp"
H = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}

def post(body, sid=None, timeout=60):
    h = dict(H)
    if sid: h["Mcp-Session-Id"] = sid
    req = urllib.request.Request(URL, json.dumps(body).encode(), h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        sid = r.headers.get("Mcp-Session-Id", sid)
        raw = r.read().decode()
    msgs = []
    if raw.lstrip().startswith("{"):
        msgs.append(json.loads(raw))
    else:
        for ln in raw.splitlines():
            if ln.startswith("data:") and ln[5:].strip():
                msgs.append(json.loads(ln[5:]))
    return sid, msgs

def session():
    sid, _ = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2025-06-18", "capabilities": {},
        "clientInfo": {"name": "swmcp", "version": "1"}}})
    post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
    return sid

def call(name, args, sid=None, timeout=120):
    sid = sid or session()
    _, m = post({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                 "params": {"name": name, "arguments": args}}, sid, timeout)
    for x in m:
        if "result" in x or "error" in x: return x
    return m

if __name__ == "__main__":
    if sys.argv[1] == "--list":
        sid = session(); _, m = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list"}, sid)
        for t in m[-1]["result"]["tools"]: print(t["name"], json.dumps(t.get("inputSchema", {}).get("properties", {})))
        sys.exit()
    name = sys.argv[1]; args = json.loads(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else {}
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    r = call(name, args)
    if out:
        for c in r["result"]["content"]:
            if c.get("type") == "image":
                open(out, "wb").write(base64.b64decode(c["data"])); print("wrote", out); break
            if c.get("type") == "text" and len(c["text"]) > 1000:
                open(out, "wb").write(base64.b64decode(c["text"])); print("wrote", out); break
        else: print(json.dumps(r)[:2000])
    else:
        res = r.get("result", r)
        for c in res.get("content", []) if isinstance(res, dict) else []:
            print(c.get("text", c))
        if isinstance(res, dict) and res.get("isError") or "error" in r: print(json.dumps(r)[:2000]); sys.exit(1)
