"""Multiplayer end to end on a computer: two NumBlocks games (build/mp), each with its own NumSocial
gateway (numsocial's gateway/numtube_gateway.py --tcp), through the real public broker, under a test
topic prefix (never in the real list of games).

Usage: python3 tests/mp_test.py PATH_TO_NUMSOCIAL   (its .venv has paho-mqtt)"""
import os
import subprocess
import sys
import tempfile
import time
import uuid

numsocial = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "../../../numtube-github")
python = os.path.join(numsocial, ".venv", "bin", "python3")
gw = os.path.join(numsocial, "gateway", "numtube_gateway.py")
env = dict(os.environ, NUMBLOCKS_TOPICS=f"numsocial/test/{uuid.uuid4().hex[:8]}")
work = tempfile.mkdtemp(prefix="nbmp-")
gateways = [subprocess.Popen([python, gw, "--tcp", str(p)], env=env, stdout=open(f"{work}/gw{p}.log", "w"),
                             stderr=subprocess.STDOUT) for p in (9751, 9752)]
time.sleep(2)
ok = False
try:
    host = subprocess.Popen(["build/mp", "host", "Alice", f"{work}/a", f"{work}/host.ppm"], env=dict(env, NB_NET_PORT="9751"),
                            stdout=subprocess.PIPE, text=True)
    at = None
    for line in host.stdout:
        print("[hote]  " + line, end="")
        if line.startswith("CODE"):
            at = line.split()[3:6]
            break
    assert at, "l'hote n'a pas ouvert la partie"
    guest = subprocess.run(["build/mp", "join", "Bob", f"{work}/b", *at],
                           env=dict(env, NB_NET_PORT="9752", NB_SHOT_LIST=f"{work}/list.ppm"),
                           capture_output=True, text=True, timeout=120)
    for line in guest.stdout.splitlines():
        print("[invite] " + line)
    for line in host.stdout:
        print("[hote]  " + line, end="")
    host.wait(timeout=60)
    ok = guest.returncode == 0 and host.returncode == 0
finally:
    for g in gateways:
        g.terminate()
    if not ok:
        for p in (9751, 9752):
            print(f"--- passerelle {p}\n" + open(f"{work}/gw{p}.log").read()[-1500:])
print("captures :", f"{work}/host.ppm", f"{work}/list.ppm")
print("OK" if ok else "ECHEC")
sys.exit(0 if ok else 1)
