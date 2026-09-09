import sys, re, glob, os
cands = [
 r"C:\Users\Administrator\Desktop\SK32F077x资料(20260409)\应用文档\SK32F077x_Datasheet_CN_V1.00(20251225).pdf",
 r"C:\Users\Administrator\Desktop\qmk\SK32F0xx_Firmware Package(25_10_24)\SK32F077xx_Datasheet_CN_Rev1.00.pdf",
]
fn = None
for c in cands:
    if os.path.exists(c):
        fn = c
        break
if fn is None:
    print("NO PDF FOUND")
    sys.exit(1)
print("PDF:", fn)
try:
    import pypdf
except Exception as e:
    print("ERR import pypdf:", repr(e))
    sys.exit(2)
r = pypdf.PdfReader(fn)
print("pages:", len(r.pages))
kw = re.compile(r"USART|AF10|USART1|TX|RX|PA9|PA10|PB5|PB6|复用|PA0")
for i, p in enumerate(r.pages):
    try:
        t = p.extract_text() or ""
    except Exception:
        continue
    if "USART1" in t or ("复用" in t and "USART" in t):
        print("=== PAGE", i, "===")
        lines = t.splitlines()
        for j, ln in enumerate(lines):
            if re.search(r"USART1|AF10", ln):
                lo = max(0, j - 1)
                hi = min(len(lines), j + 3)
                for k in range(lo, hi):
                    print("  ", lines[k])
                print("  ---")
