#!/usr/bin/env python3
"""Generate an Xray-only Go 1.27 overlay without changing the Go installation."""

import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--go", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    env = json.loads(subprocess.check_output(
        [args.go, "env", "-json", "GOROOT", "GOVERSION"], universal_newlines=True))
    if not env["GOVERSION"].startswith("go1.27."):
        raise SystemExit("EIP93 overlay requires Go 1.27.x; review it before upgrading Go")
    source = Path(env["GOROOT"]).resolve() / "src/crypto/internal/fips140/aes"
    out = Path(args.out).resolve()
    # Validate every edit before writing the overlay manifest.
    files = {}

    def change(name, edits):
        text = (source / name).read_text()
        for old, new in edits:
            if text.count(old) != 1:
                raise SystemExit("EIP93 overlay: unsupported Go source in " + name)
            text = text.replace(old, new, 1)
        files[name] = text

    change("gcm/gcm.go", [("\tgcmPlatformData\n", "\tgcmPlatformData\n\teip93 *eip93State\n")])
    change("gcm/gcm_noasm.go", [
        ("func initGCM(g *GCM) {}", "func initGCM(g *GCM) { g.eip93 = newEIP93State(g) }"),
        ("\tsealGeneric(out, g, nonce, plaintext, data)",
         "\tif g.eip93 != nil {\n"
         "\t\tif handled, _ := g.eip93.crypt(g, out, nonce, plaintext, data, false); handled { return }\n"
         "\t}\n\tsealGeneric(out, g, nonce, plaintext, data)"),
        ("\treturn openGeneric(out, g, nonce, ciphertext, data)",
         "\tif g.eip93 != nil {\n"
         "\t\tif handled, valid := g.eip93.crypt(g, out, nonce, ciphertext, data, true); handled {\n"
         "\t\t\tif !valid { return errOpen }; return nil\n"
         "\t\t}\n\t}\n\treturn openGeneric(out, g, nonce, ciphertext, data)"),
    ])
    change("aes_noasm.go", [
        ("func checkGenericIsExpected() {}", "func checkGenericIsExpected() {}\n\n"
         "// EncryptionKeySchedule exposes the same internal helper as aes_asm.go.\n"
         "func EncryptionKeySchedule(c *Block) []uint32 { return c.enc[:c.roundKeysSize()] }"),
    ])
    change("cbc.go", [
        ("type CBCEncrypter struct {\n", "type CBCEncrypter struct {\n\teip93 *eip93CBCState\n"),
        ("type CBCDecrypter struct {\n", "type CBCDecrypter struct {\n\teip93 *eip93CBCState\n"),
        ("return &CBCEncrypter{b: *b, iv: iv}",
         "return &CBCEncrypter{b: *b, iv: iv, eip93: newEIP93CBCState(b)}"),
        ("return &CBCDecrypter{b: *b, iv: iv}",
         "return &CBCDecrypter{b: *b, iv: iv, eip93: newEIP93CBCState(b)}"),
        ("\tcryptBlocksEnc(&c.b, &c.iv, dst, src)",
         "\tif c.eip93 != nil && c.eip93.crypt(&c.b, &c.iv, dst, src, false) { return }\n"
         "\tcryptBlocksEnc(&c.b, &c.iv, dst, src)"),
        ("\tcryptBlocksDec(&c.b, &c.iv, dst, src)",
         "\tif c.eip93 != nil && c.eip93.crypt(&c.b, &c.iv, dst, src, true) { return }\n"
         "\tcryptBlocksDec(&c.b, &c.iv, dst, src)"),
    ])
    for name in ("eip93_linux.go", "eip93_stub.go", "eip93_linux_test.go"):
        files["gcm/" + name] = (Path(__file__).parent / (name + ".in")).read_text()
    for name in ("eip93_cbc_linux.go", "eip93_cbc_stub.go", "eip93_cbc_linux_test.go"):
        files[name] = (Path(__file__).parent / (name + ".in")).read_text()
    out.mkdir(parents=True, exist_ok=True)
    replacements = {}
    for name, text in files.items():
        dst = out / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text)
        replacements[str(source / name)] = str(dst)
    (out / "overlay.json").write_text(json.dumps({"Replace": replacements}, indent=2) + "\n")


if __name__ == "__main__":
    main()
