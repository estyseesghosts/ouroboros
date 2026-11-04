#!/usr/bin/env python3
"""A5b run driver.  run.py prep NN 'ENV_EXTRA;...' [args lines sep by ;]
                    run.py launch NN    (Sphaira cursor at entry 1)
                    run.py nav NN STEP  (plus|cont|cgear|ok)
                    run.py shot NN NAME
                    run.py collect NN   (after the game is closed)"""
import sys, os, time, subprocess
sys.path.insert(0, os.path.dirname(__file__)); import swmcp
W = "/Users/terezi/bw-switch/logs"; D = "sdmc:/switch/eidos/"
BASE = "EIDOS_FPS_LOG=1\nEIDOS_RENDERER_REPORT=1\nEIDOS_GPU3D_REPORT=1\nEIDOS_RENDERER=gl\n"
def up():
    if subprocess.call(["nc","-z","-w3","10.88.111.28","57331"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL): sys.exit("MCP DOWN")
def c(n,a): up(); r=swmcp.call(n,a); 
def press(b,ms=800): up(); swmcp.call("hid_press_buttons",{"buttons":[b],"duration_ms":ms})
def shot(nn,name):
    up(); r=swmcp.call("screenshot",{}); import base64
    for x in r["result"]["content"]:
        if x.get("type")=="image": open(f"{W}/{nn}/{name}.jpg","wb").write(base64.b64decode(x["data"]))
    print("shot",f"{W}/{nn}/{name}.jpg")
def readall(path):
    out=b"";off=0
    while True:
        up(); r=swmcp.call("fs_read_file",{"path":path,"offset":off,"length":65536})
        t="".join(x.get("text","") for x in r["result"]["content"])
        if r["result"].get("isError"): return out, t
        import re
        m=re.match(r"Read (\d+) bytes from offset \d+:\n",t)
        if m: n=int(m.group(1)); t=t[m.end():]
        else: n=len(t.encode())
        out+=t.encode()
        if n<65536: break
        off+=65536
    return out,None
cmd,nn=sys.argv[1],sys.argv[2]; os.makedirs(f"{W}/{nn}",exist_ok=True)
if cmd=="prep":
    env=BASE+"".join(l+"\n" for l in sys.argv[3].split(";") if l) if len(sys.argv)>3 else BASE
    if len(sys.argv)>3 and sys.argv[3].startswith("!"): env="".join(l+"\n" for l in sys.argv[3][1:].split(";") if l)
    args="".join(l+"\n" for l in sys.argv[4].split(";") if l) if len(sys.argv)>4 else ""
    for f,d in (("env.txt",env),("args.txt",args),("stderr.txt",""),("boot.txt","")):
        up(); swmcp.call("fs_write_file",{"path":D+f,"data":d})
    open(f"{W}/{nn}/env.txt","w").write(env); open(f"{W}/{nn}/args.txt","w").write(args)
    print(env); print("args:",repr(args))
elif cmd=="launch":
    press("Down",150); time.sleep(1); press("Down",150); time.sleep(1.5); shot(nn,"sel")
elif cmd=="go":
    press("A",150); time.sleep(20); b,_=readall(D+"boot.txt"); print(b.decode()[-600:]); shot(nn,"title")
elif cmd=="nav":
    st=sys.argv[3]
    if st=="plus": press("Plus"); time.sleep(6)
    elif st=="cont": press("B"); time.sleep(8)
    elif st=="cgear": press("Down"); time.sleep(1.5); press("B"); time.sleep(6)
    elif st=="ok": press("B"); time.sleep(18)
    shot(nn,"nav_"+st)
elif cmd=="shot": shot(nn,sys.argv[3])
elif cmd=="collect":
    for f in ("boot.txt","stderr.txt","stdout.txt"):
        b,e=readall(D+f)
        if e is None: open(f"{W}/{nn}/{f}","wb").write(b); print(f,len(b))
        else: print(f,"ERR",e[:120])
