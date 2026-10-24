# EIP93 真机诊断

程序直接绑定内核中的 `*-eip93` 驱动名称，使用未修改的 Go 标准库计算参考结果。
需要内核 AF_ALG hash、skcipher、AEAD 接口和本目录对应的 EIP93 移植驱动。
不需要修改 Xray，也不依赖第三方 Go 模块。

在仓库根目录交叉编译：

```sh
GOOS=linux GOARCH=mipsle GOMIPS=softfloat CGO_ENABLED=0 \
  go build -trimpath -ldflags='-s -w' -o eip93-check \
  trunk/tools/eip93-check/main.go
```

将程序复制到运行新内核的 MT7621 路由器，在路由器上执行：

```sh
dmesg | grep -i -E 'eip93|alg:|self-test'
grep -A12 -B2 eip93 /proc/crypto
cat /proc/interrupts
./eip93-check
cat /proc/interrupts
```

只有结果正确、内核自检通过，并且 EIP93 中断增加，才说明硬件路径实际工作。
AEAD 的部分边界请求按驱动设计走软件回退；正常 AES、hash 和适合硬件的
authenc 请求应增加硬件中断计数。程序遇到算法缺失、错误结果或认证错误处理异常
会退出并返回非零值。

覆盖 AES 三种密钥长度和 ECB/CBC/CTR、DES/3DES、RFC3686、空/流式/分块边界 hash、
空密钥和长密钥 HMAC、16 种 authenc、变化的 AAD/标签长度、空 AEAD 载荷、
坏标签拒绝、CTR 低 32 位计数器溢出和并发请求。RFC3686 单独加密与 authenc
加解密均有覆盖。大缓冲区、scatterlist 和 hash export/import 还应结合内核自检。

程序自动识别 Linux 4.4 的旧 AEAD 缓冲区布局；可用 `-legacy-aead=true/false`
覆盖识别结果。`-software` 改绑通用算法名称，可在 Linux 主机检查诊断逻辑；
该选项不代表硬件验证，普通主机也不能运行默认的 EIP93 测试。
