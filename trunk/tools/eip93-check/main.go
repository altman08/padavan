// Standalone AF_ALG diagnostic. Uses unmodified Go AES as the reference.
package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/des"
	"crypto/hmac"
	"crypto/md5"
	"crypto/sha1"
	"crypto/sha256"
	"encoding/binary"
	"flag"
	"fmt"
	"hash"
	"os"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"unsafe"
)

const solALG = 279

func legacyAEADKernel() bool {
	var u syscall.Utsname
	if syscall.Uname(&u) != nil {
		return false
	}
	var release []byte
	for _, c := range u.Release {
		if c == 0 {
			break
		}
		release = append(release, byte(c))
	}
	return strings.HasPrefix(string(release), "4.4.")
}

var legacyAEAD = flag.Bool("legacy-aead", legacyAEADKernel(), "use Linux 4.4 AF_ALG AEAD buffer layout (auto-detected)")

func bindALG(kind, name string, key []byte, tagSize int) (int, error) {
	fd, err := syscall.Socket(38, syscall.SOCK_SEQPACKET|syscall.SOCK_CLOEXEC, 0)
	if err != nil {
		return -1, fmt.Errorf("AF_ALG socket: %w", err)
	}
	defer syscall.Close(fd)
	var sa [88]byte
	binary.LittleEndian.PutUint16(sa[:], 38)
	copy(sa[2:16], kind)
	copy(sa[24:], name)
	_, _, errno := syscall.Syscall(syscall.SYS_BIND, uintptr(fd), uintptr(unsafe.Pointer(&sa[0])), uintptr(len(sa)))
	runtime.KeepAlive(sa)
	if errno != 0 {
		return -1, fmt.Errorf("bind %s: %w", name, errno)
	}
	if key != nil {
		var keyPtr uintptr
		if len(key) > 0 {
			keyPtr = uintptr(unsafe.Pointer(&key[0]))
		}
		_, _, errno = syscall.Syscall6(syscall.SYS_SETSOCKOPT, uintptr(fd), solALG, 1, keyPtr, uintptr(len(key)), 0)
		runtime.KeepAlive(key)
		if errno != 0 {
			return -1, fmt.Errorf("set key: %w", errno)
		}
	}
	if tagSize > 0 {
		_, _, errno = syscall.Syscall6(syscall.SYS_SETSOCKOPT, uintptr(fd), solALG, 5, 0, uintptr(tagSize), 0)
		if errno != 0 {
			return -1, fmt.Errorf("set tag size: %w", errno)
		}
	}

	opFD, _, errno := syscall.Syscall6(syscall.SYS_ACCEPT4, uintptr(fd), 0, 0, syscall.SOCK_CLOEXEC, 0, 0)
	if errno != 0 {
		return -1, fmt.Errorf("accept: %w", errno)
	}
	op := int(opFD)
	for _, opt := range []int{syscall.SO_RCVTIMEO, syscall.SO_SNDTIMEO} {
		if err := syscall.SetsockoptTimeval(op, syscall.SOL_SOCKET, opt, &syscall.Timeval{Sec: 15}); err != nil {
			syscall.Close(op)
			return -1, fmt.Errorf("set timeout: %w", err)
		}
	}
	return op, nil
}

func u32(v uint32) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, v)
	return b
}

func control(kind int32, data []byte) []byte {
	b := make([]byte, syscall.CmsgSpace(len(data)))
	h := (*syscall.Cmsghdr)(unsafe.Pointer(&b[0]))
	h.Level, h.Type = solALG, kind
	h.SetLen(syscall.CmsgLen(len(data)))
	copy(b[syscall.CmsgLen(0):], data)
	return b
}

func exchange(fd int, iv, src, aad []byte, decrypt bool, tagSize int) ([]byte, error) {
	op := uint32(1)
	if decrypt {
		op = 0
	}
	ctl := control(3, u32(op))
	if len(iv) > 0 {
		ctl = append(ctl, control(2, append(u32(uint32(len(iv))), iv...))...)
	}
	input := append(append([]byte{}, aad...), src...)
	outLen := len(input)
	if tagSize > 0 {
		ctl = append(ctl, control(4, u32(uint32(len(aad))))...)
		if decrypt {
			outLen -= tagSize
		} else {
			outLen += tagSize
		}
	}
	if tagSize > 0 && *legacyAEAD {
		if !decrypt {
			input = append(input, make([]byte, tagSize)...)
		}
		outLen = len(input)
	}
	n, err := syscall.SendmsgN(fd, input, ctl, nil, 0)
	if err != nil {
		return nil, fmt.Errorf("sendmsg: %w", err)
	}
	if n != len(input) {
		return nil, fmt.Errorf("short send: %d/%d", n, len(input))
	}
	readLen := outLen
	if readLen == 0 {
		readLen = 1
	} // A zero-sized Go Read skips the kernel operation.
	output := make([]byte, readLen)
	n, err = syscall.Read(fd, output)
	if err != nil {
		return nil, err
	}
	if n != outLen {
		return nil, fmt.Errorf("short read: %d/%d", n, outLen)
	}
	output = output[:outLen]
	if tagSize > 0 && *legacyAEAD && decrypt {
		output = output[:len(output)-tagSize]
	}
	return output[len(aad):], nil
}

func pattern(n, seed int) []byte {
	b := make([]byte, n)
	for i := range b {
		b[i] = byte(i*7 + seed)
	}
	return b
}

// Only -software changes the binding names. The reference always stays in Go.
var software = flag.Bool("software", false, "exercise the diagnostic using generic AF_ALG algorithms")

func hwName(name string) string {
	if *software {
		return name
	}
	switch name {
	case "md5", "sha1", "sha224", "sha256":
		return name + "-eip93"
	}
	for _, c := range []string{"aes", "des3_ede", "des"} {
		name = strings.ReplaceAll(name, "("+c+")", "("+c+"-eip93)")
	}
	for _, h := range []string{"md5", "sha1", "sha224", "sha256"} {
		name = strings.ReplaceAll(name, "("+h+")", "("+h+"-eip93)")
	}
	return name
}
func openALG(kind, name string, key []byte, tag int) (int, error) {
	return bindALG(kind, hwName(name), key, tag)
}
func blockFor(c string, key []byte) (cipher.Block, error) {
	switch c {
	case "aes":
		return aes.NewCipher(key)
	case "des":
		return des.NewCipher(key)
	case "des3_ede":
		return des.NewTripleDESCipher(key)
	}
	return nil, fmt.Errorf("unknown cipher %s", c)
}
func encrypt(block cipher.Block, mode string, iv, plain []byte) []byte {
	out := make([]byte, len(plain))
	switch mode {
	case "ctr":
		cipher.NewCTR(block, iv).XORKeyStream(out, plain)
	case "cbc":
		cipher.NewCBCEncrypter(block, iv).CryptBlocks(out, plain)
	case "ecb":
		for n := 0; n < len(plain); n += block.BlockSize() {
			block.Encrypt(out[n:], plain[n:])
		}
	}
	return out
}
func checkCipher(c string, keyLen int, mode string) error {
	key := pattern(keyLen, 1)
	block, err := blockFor(c, key)
	if err != nil {
		return err
	}
	name := mode + "(" + c + ")"
	fd, err := openALG("skcipher", name, key, 0)
	if err != nil {
		return err
	}
	defer syscall.Close(fd)
	sizes := []int{block.BlockSize(), 256, 4096, 16384, 65536}
	if mode == "ctr" {
		sizes = append(sizes, 1, 15, 17, 257)
	}
	for _, size := range sizes {
		iv := pattern(block.BlockSize(), 2)
		// Cross the hardware's 32-bit CTR counter boundary.
		if mode == "ctr" {
			copy(iv[len(iv)-4:], []byte{255, 255, 255, 254})
		}
		plain := pattern(size, 3)
		want := encrypt(block, mode, iv, plain)
		reqIV := iv
		if mode == "ecb" {
			reqIV = nil
		}
		got, e := exchange(fd, reqIV, plain, nil, false, 0)
		if e != nil {
			return fmt.Errorf("%s/%d encrypt: %w", name, size, e)
		}
		if !bytes.Equal(got, want) {
			return fmt.Errorf("%s/%d encryption differs from Go", name, size)
		}
		got, e = exchange(fd, reqIV, want, nil, true, 0)
		if e != nil {
			return e
		}
		if !bytes.Equal(got, plain) {
			return fmt.Errorf("%s/%d decryption differs from Go", name, size)
		}
	}
	fmt.Printf("PASS %s key=%d, encrypt/decrypt and length boundaries\n", hwName(name), keyLen*8)
	return nil
}

var hashes = []struct {
	name string
	new  func() hash.Hash
}{
	{"md5", md5.New}, {"sha1", sha1.New}, {"sha224", sha256.New224}, {"sha256", sha256.New},
}

func checkHashes() error {
	for _, h := range hashes {
		for _, keyLen := range []int{-1, 0, 17, 131} {
			name := h.name
			var key []byte
			if keyLen >= 0 {
				name = "hmac(" + name + ")"
				key = pattern(keyLen, 8)
			}
			for _, n := range []int{0, 1, 63, 64, 65, 257, 4096, 32768} {
				fd, err := openALG("hash", name, key, 0)
				if err != nil {
					return err
				}
				data := pattern(n, 5)
				var ref hash.Hash
				if keyLen >= 0 {
					ref = hmac.New(h.new, key)
				} else {
					ref = h.new()
				}
				ref.Write(data)
				want := ref.Sum(nil)
				// Exercise streaming update as well as finalization through AF_ALG.
				split := n / 2
				_, err = syscall.SendmsgN(fd, data[:split], nil, nil, syscall.MSG_MORE)
				if err == nil {
					_, err = syscall.SendmsgN(fd, data[split:], nil, nil, 0)
				}
				got := make([]byte, len(want))
				var read int
				if err == nil {
					read, err = syscall.Read(fd, got)
				}
				syscall.Close(fd)
				if err != nil {
					return fmt.Errorf("%s len=%d: %w", name, n, err)
				}
				if read != len(want) || !bytes.Equal(got, want) {
					return fmt.Errorf("%s len=%d differs from Go", name, n)
				}
			}
			fmt.Printf("PASS %s key length=%d, empty/streaming/boundary hashes\n", hwName(name), keyLen)
		}
	}
	return nil
}
func authencKey(auth, key []byte) []byte {
	out := make([]byte, 8+len(auth)+len(key))
	binary.LittleEndian.PutUint16(out, 8)
	binary.LittleEndian.PutUint16(out[2:], 1)
	binary.BigEndian.PutUint32(out[4:], uint32(len(key)))
	copy(out[8:], auth)
	copy(out[8+len(auth):], key)
	return out
}
func authencExchange(name string, key, iv, data, aad []byte, decrypt bool, tag int) ([]byte, error) {
	// Fresh sockets also exercise setkey and avoid old AF_ALG zero-input reuse bugs.
	fd, err := openALG("aead", name, key, tag)
	if err != nil {
		return nil, err
	}
	defer syscall.Close(fd)
	return exchange(fd, iv, data, aad, decrypt, tag)
}
func checkAuthenc() error {
	for _, h := range hashes {
		for _, c := range []string{"aes", "des", "des3_ede", "rfc3686"} {
			kl := map[string]int{"aes": 16, "des": 8, "des3_ede": 24, "rfc3686": 20}[c]
			key, ak := pattern(kl, 1), pattern(131, 6)
			bc, bk := c, key
			if c == "rfc3686" {
				bc, bk = "aes", key[:16]
			}
			block, err := blockFor(bc, bk)
			if err != nil {
				return err
			}
			name := "authenc(hmac(" + h.name + "),cbc(" + c + "))"
			if c == "rfc3686" {
				name = "authenc(hmac(" + h.name + "),rfc3686(ctr(aes)))"
			}
			packedKey := authencKey(ak, key)
			for _, tag := range []int{1, 12, h.new().Size()} {
				for _, aadLen := range []int{0, 13, 16, 256, 2049} {
					for _, plainLen := range []int{0, 256} {
						iv, plain, aad := pattern(block.BlockSize(), 2), pattern(plainLen, 3), pattern(aadLen, 4)
						var want []byte
						if c == "rfc3686" {
							iv = iv[:8]
							ctr := make([]byte, 16)
							copy(ctr, key[16:])
							copy(ctr[4:], iv)
							ctr[15] = 1
							want = encrypt(block, "ctr", ctr, plain)
						} else {
							want = encrypt(block, "cbc", iv, plain)
						}
						mac := hmac.New(h.new, ak)
						mac.Write(aad)
						mac.Write(want)
						want = append(want, mac.Sum(nil)[:tag]...)
						got, e := authencExchange(name, packedKey, iv, plain, aad, false, tag)
						if e != nil {
							return fmt.Errorf("%s tag=%d AAD=%d plain=%d enc: %w", name, tag, aadLen, plainLen, e)
						}
						if !bytes.Equal(got, want) {
							return fmt.Errorf("%s ciphertext/tag differs from Go", name)
						}
						got, e = authencExchange(name, packedKey, iv, want, aad, true, tag)
						if e != nil || !bytes.Equal(got, plain) {
							return fmt.Errorf("%s tag=%d AAD=%d plain=%d decrypt failed: %v", name, tag, aadLen, plainLen, e)
						}
						bad := append([]byte{}, want...)
						bad[len(bad)-1] ^= 1
						_, e = authencExchange(name, packedKey, iv, bad, aad, true, tag)
						if e != syscall.EBADMSG {
							return fmt.Errorf("%s corrupted tag accepted or unexpected error: %v", name, e)
						}
					}
				}
			}
			fmt.Println("PASS", hwName(name), "variable AAD, empty payload, long HMAC key, tags and corrupted-tag rejection")
		}
	}
	return nil
}
func checkRFC3686() error {
	key, iv, plain := pattern(20, 1), pattern(8, 2), pattern(257, 3)
	fd, err := openALG("skcipher", "rfc3686(ctr(aes))", key, 0)
	if err != nil {
		return err
	}
	defer syscall.Close(fd)
	block, _ := aes.NewCipher(key[:16])
	ctr := make([]byte, 16)
	copy(ctr, key[16:])
	copy(ctr[4:], iv)
	ctr[15] = 1
	want := encrypt(block, "ctr", ctr, plain)
	got, err := exchange(fd, iv, plain, nil, false, 0)
	if err != nil {
		return err
	}
	if !bytes.Equal(got, want) {
		return fmt.Errorf("RFC3686 differs from Go")
	}
	fmt.Println("PASS", hwName("rfc3686(ctr(aes))"))
	return nil
}
func main() {
	flag.Parse()
	// Go standard library results are independent of the AF_ALG driver.
	tests := []func() error{checkHashes, checkAuthenc, checkRFC3686}
	for _, c := range []string{"aes", "des", "des3_ede"} {
		for _, mode := range []string{"ecb", "cbc", "ctr"} {
			if c != "aes" && mode == "ctr" {
				continue
			}
			for _, kl := range []int{16, 24, 32} {
				if c == "des" {
					kl = 8
				}
				if c == "des3_ede" {
					kl = 24
				}
				cc, mm, kk := c, mode, kl
				tests = append(tests, func() error { return checkCipher(cc, kk, mm) })
				if c != "aes" {
					break
				}
			}
		}
	}
	for _, test := range tests {
		if err := test(); err != nil {
			fmt.Fprintln(os.Stderr, "FAIL:", err)
			os.Exit(1)
		}
	}
	var wg sync.WaitGroup
	fail := make(chan error, 4)
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); fail <- checkCipher("aes", 32, "ctr") }()
	}
	wg.Wait()
	close(fail)
	for err := range fail {
		if err != nil {
			fmt.Fprintln(os.Stderr, "FAIL concurrent:", err)
			os.Exit(1)
		}
	}
	fmt.Println("PASS all checks; confirm that the EIP93 interrupt count increased")
}
