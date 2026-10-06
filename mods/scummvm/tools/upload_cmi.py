"""Upload Curse of Monkey Island data to the Zune, playable-first (resumable: rerun to continue). Usage: upload_cmi.py [ip]"""
import os, subprocess, sys
ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.10.178"
here = os.path.dirname(os.path.abspath(__file__))
G = "\\flash2\\scummvm\\games\\comi"
os.makedirs(os.path.join(here, "ckpt_cmi"), exist_ok=True)
subprocess.run([sys.executable, os.path.join(os.environ.get("LYRA_TOOLS", os.path.join(here, "..", "..", "..", "lyra-src", "tools")), "general", "lyra-mkdir.py"), ip, G + "\\RESOURCE"], capture_output=True)
res = sorted(os.listdir(os.path.join(here, "cmi_data", "RESOURCE")))
order = ["COMI.LA1", "COMI.LA2", "RESOURCE/LANGUAGE.TAB"] + ["RESOURCE/FONT%d.NUT" % i for i in range(5)] \
    + ["RESOURCE/" + n for n in ("VOXDISK1.BUN", "MUSDISK1.BUN", "VOXDISK2.BUN", "MUSDISK2.BUN")] \
    + ["RESOURCE/" + n for n in res if n.upper().endswith(".SAN")]
for rel in order:
    remote = G + "\\" + rel.replace("/", "\\")
    ck = os.path.join(here, "ckpt_cmi", rel.replace("/", "_") + ".json")
    print("==", remote, flush=True)
    subprocess.run([sys.executable, os.path.join(here, "upload_resume.py"), ip, os.path.join(here, "cmi_data", rel), remote, "--ckpt", ck])
print("ALL_DONE")
