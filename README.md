GhostLock54 for OPPO Reno7 5G

针对 OPPO Reno7 5G（型号 OP5227）的 GhostLock（CVE-2026-43499）移植版。

目标内核：5.4.254-qgki-gaa96eae94d32，Android 12。

本程序通过 futex PI 越界写获得内核任意写原语，把目标进程的 cred 指针整体替换为 init_cred，并清掉 seccomp，达到：

· 真实 uid=0 / gid=0 / euid=0 / egid=0
· 完整 capability 集合（CapPrm / CapEff / CapBnd = 0x0000003fffffffff，来自 init_cred）
· SELinux 切换为 Permissive（enforce 1 → 0）
· 清目标任务的 task_struct.seccomp，让子进程 fork 后不再被 Android seccomp filter 拦截（Seccomp: 0）
· 不切换 SELinux 域：子进程保持 init_cred 绑定的 u:r:kernel:s0 域，拿到的是 kernel 域的 root shell
· 提权成功后，直接在当前终端原地弹出一个拥有完整 Root 权限的 # Shell（该功能已停止更新，详见下方「停止更新说明」）

文件放置约定

与本 exploit 配套的两个文件统一放在 /data/local/tmp/ 根目录，不要再放子目录：

· /data/local/tmp/ghostlock54 —— 编译产物
· /data/local/tmp/kernelsu.ko —— KernelSU 模块
· /data/local/tmp/load_ksu_54.sh —— 加载脚本

代码里 execl("/system/bin/sh", "sh", "/data/local/tmp/load_ksu_54.sh", ...) 硬编码的就是这个路径；load_ksu_54.sh 里 insmod 的 kernelsu.ko 也应指向 /data/local/tmp/kernelsu.ko。旧文档里的 /data/local/tmp/kernelsu/... 子目录写法已废弃。

停止更新说明

「原地弹出 # shell」功能停止更新。

从当前版本起，exploit 不再继续维护「提权完成后直接在当前终端原地弹出 root shell」这一路径。原因：

· 原地弹 shell 依赖主进程原地挂起保活，退出 shell 后内核 PI 引用仍在，只能靠重启清理；
· KernelSU 路线（late-load kernelsu.ko）已经能提供常驻、可管理的 root，没必要在 exploit 里再养一个挂起主进程；
· 原地 shell 与 KernelSU 的 u:r:ksu:s0 域在同一台设备上并存时，SELinux 域切换和 su 分叉容易互相踩。

如果你确实需要「原地弹出 # shell」这个行为，请使用发行版 v5.0 及以下。 v5.0 及以下的 tag / release 里保留了完整实现（含 setcon("u:r:shell:s0") 与原地 # 提示符），当前版本不再保证该路径可用。v5.0 之后的版本以 KernelSU 集成为主路径，见下方「KernelSU 集成」。

防内核崩溃提示

由于漏洞利用后必须保持主进程存活以防内核悬挂指针引发崩溃（Kernel Panic），父进程会原地进入死循环挂起（v5.0 及以下含原地 shell 的版本；当前版本走 KernelSU 路径，主进程同样保活，但不再承载交互 shell）。此时如果你想彻底结束程序，直接按电源键重启手机即可。

编译

需要 libselinux（exploit 用 setcon 切换 SELinux 域，Android 系统核心库，bionic 环境自带头文件和动态库）：

```sh
clang -O2 -pthread ghostlock54.c -o ghostlock54 -lselinux
```

如果提示找不到 selinux/selinux.h，说明当前编译环境不是 Termux 或 NDK 内置路径，需要额外指定：

```sh
clang -O2 -pthread ghostlock54.c -o ghostlock54 \
  -I/data/data/com.termux/files/usr/include \
  -L/data/data/com.termux/files/usr/lib -lselinux
```

如果链接报 undefined symbol: setcon，就是漏了 -lselinux，加上即可。

验证动态库依赖：

```sh
nm -u ghostlock54 | grep -i selinux    # 能看到 setcon（来自 libselinux）
```

运行

编译完成后，把三个文件都推送到 /data/local/tmp/ 根目录，并给可执行文件赋权：

```sh
adb push ghostlock54          /data/local/tmp/ghostlock54
adb push kernelsu.ko          /data/local/tmp/kernelsu.ko
adb push load_ksu_54.sh       /data/local/tmp/load_ksu_54.sh
adb shell chmod 755 /data/local/tmp/ghostlock54
adb shell chmod 644 /data/local/tmp/kernelsu.ko
adb shell chmod 755 /data/local/tmp/load_ksu_54.sh
```

不要把 kernelsu.ko / load_ksu_54.sh 放进 /data/local/tmp/kernelsu/ 之类的子目录。ghostlock54 里 execl 的脚本路径是 /data/local/tmp/load_ksu_54.sh，脚本里 insmod 的模块路径是 /data/local/tmp/kernelsu.ko，两者都按根目录写死。

启动漏洞利用：

```sh
adb shell -t /data/local/tmp/ghostlock54
```

· v5.0 及以下：程序跑完后，终端会直接变成 # 提示符，此时你已获得 Root 权限，可以直接执行 id / getenforce / cat /proc/self/attr/current。
· 当前版本：不再原地弹 #。exploit 只负责拿到内核任意写、把 cred 换成 init_cred、切 permissive、清 seccomp，然后调用 /data/local/tmp/load_ksu_54.sh 加载 /data/local/tmp/kernelsu.ko；之后用 KernelSU Manager 授权 app，在 app 终端里 su 拿 u:r:ksu:s0 的 root shell。

程序输出示例（child 阶段）：

```
[+] [CHILD] uid=0 gid=0 euid=0 egid=0
[+] [CHILD] CapPrm:     0000003fffffffff
[+] [CHILD] CapEff:     0000003fffffffff
[+] [CHILD] CapBnd:     0000003fffffffff
[+] [CHILD] Running root action. Launching shell...
```

父进程侧会额外打印一行攻击完成后的 context（用于 debug，不影响利用）：

```
[*] parent post-exploit: u:r:untrusted_app:s0:c...
```

/proc/self/status 示例（v5.0 及以下 root shell 内）：

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

关于 SELinux 域（v5.0 及以下有效）

exploit 完成后，子进程 uid 是 0、CapEff 全满、Seccomp 清零，SELinux context 初始是 u:r:kernel:s0——因为 cred 被整体替换为 init_cred，而 init_cred->security 绑定的就是 kernel 域的 sid。

v5.0 及以下的版本会在提权完成后调用 setcon("u:r:shell:s0") 切到 shell 域，理由：

· KernelSU 模块默认开启 SELinux 强制模式（setenforce 1）。在强制模式下，u:r:kernel:s0 的 shell 无法执行 /system/bin 下的命令（会被 SELinux 拒绝并终止），这样 ghostlock 给出的 # shell 就没法正常用了。
· u:r:shell:s0 是 Android 的 shell 域，权限足够（能 exec /system/bin 命令、能加载内核模块、能读写 /data 等），强制模式下也能正常工作。
· 切换是单向放宽：kernel → shell 允许；反向（无 root 的 shell 域切回 kernel 域）不允许。只有拿全 cap 的 kernel 域 root 才能自由 setcon 到任意放宽的目标域。
· 切换依赖 libselinux（编译需 -lselinux）。

当前版本已停止更新「原地弹 shell + setcon 到 shell 域」这一整套路径，该逻辑保留在 v5.0 及以下。当前版本以 KernelSU late-load 为主路径，root shell 由 KernelSU 提供 u:r:ksu:s0，不再由 exploit 自己 setcon。

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

当前 exploit 只消耗 4 个（见下方写入顺序），剩余 6 个是预留窗口，供未来扩展使用。第 11 次写会 pr_error("Out of fake lock windows!") 直接退出。要加写操作必须同时扩 windows[]，并确认新偏移仍在 selinux_state 那一页内、不越过 empty_zero_page（0x1000）。

写入顺序（实际 4 次）

1. selinux_state ← k_write_value（enforce 1 → 0，切 Permissive）
2. child_task+0x808 ← init_cred（task->cred）
3. child_task+0x800 ← init_cred（task->real_cred）
4. init_cred+0x04 ← 0（init_cred 的 uid / gid 修复）

旧版本还有第 5 次写 child_task+0x8d0 ← 0（清 seccomp）。在 adb shell 场景下子进程 seccomp.mode 本来就是 0，这次写是空操作，已删除。若你在 App 进程内跑（父进程带 seccomp filter），需要把这步加回来，见「历史版本」说明。

顺序选择的原因：先切 SELinux permissive，确保后续写 task 字段不会被 SELinux 审计卡住（虽然本轮是内核任意写，不经 LSM 检查，但 permissive 能避免写完后系统行为异常）；然后写 cred、real_cred，最后修 init_cred 兜底。

历史版本曾逐字段写 cred 的 uid / gid / securebits / cap_inheritable / cap_permitted / cap_effective / cap_bset / cap_ambient，共 5 + 5 = 10 次写，且必须严格按 +0x38 → +0x30 → +0x40 → +0x28 → +0x48 的顺序避开 rb_erase 读取 parent 时的污染。本版本改为 cred 指针替换，已不需要这套严格顺序，仅保留 init_cred 修复作为兜底。

安全说明

· 仅针对上述设备/内核标定。换内核必须重新反汇编验证所有偏移和栈几何，否则极易 panic。
· 主进程保活机制：成功利用后，内核 PI 引用指向存活的工作线程内核栈。为了防止主进程退出导致 Y 线程栈释放而触发 Kernel Panic，主进程会进入 for(;;) pause(); 死循环。
· 不要试图 kill -9 挂起的漏洞主进程，否则悬挂的 fake waiter 被别的进程取到会瞬间 Panic 重启。
· 恢复系统到正常状态（SELinux 回 enforcing、清理内核内残留 PI 引用）只能靠重启（adb reboot）。
· 仅在你自己拥有或明确授权测试的设备上使用。

KernelSU 集成

本仓库附带 KernelSU 3.3.1 的 5.4.254 移植（kernelsu/ 目录），配合 GhostLock54 把手动 root 升级成完整的 root 管理方案。

文件放置（重要）：kernelsu.ko 和 load_ksu_54.sh 都放在 /data/local/tmp/ 根目录，不要放 kernelsu/ 子目录：

```
/data/local/tmp/ghostlock54
/data/local/tmp/kernelsu.ko
/data/local/tmp/load_ksu_54.sh
```

load_ksu_54.sh 里 insmod 的模块路径应为 /data/local/tmp/kernelsu.ko；ghostlock54 里 execl 的脚本路径为 /data/local/tmp/load_ksu_54.sh，两者都已按根目录写死。

流程：

1. 运行 ghostlock54，等它执行到调用 load_ksu_54.sh 的阶段（当前版本由 exploit 自动调用；v5.0 及以下需你手动在 # shell 里执行）。
2. 脚本会 insmod /data/local/tmp/kernelsu.ko 并自动重启 KernelSU Manager；在 Manager 里给需要的 app 授权。
3. 之后任意已授权 app 的终端里 su 即可得到 u:r:ksu:s0 的 root shell。

模块是 runtime 加载（late-load），不修改 boot.img / 任何分区；重启后重新跑一次脚本即可。

SELinux：模块默认 setenforce 1（强制模式）；若某状态（例如旧的 exploit 破坏过 SELinux）强制模式让系统异常，加载时加 permissive=1 参数保持当前模式。注意强制模式下 u:r:kernel:s0 的 shell 不能执行 /system/bin 命令——v5.0 及以下的 ghostlock 侧会 setcon 到 shell 域规避，当前版本则完全交给 KernelSU 的 u:r:ksu:s0 shell，不再由 exploit 自己切域。

详见 kernelsu/README.md 与 kernelsu/SUMMARY_5.4_KSU.md（移植全过程与踩坑记录，包括 ColorOS 隐藏内核反提权 oplus_root_check 的绕过、5.4 原生 sepolicy 注入、终端 su 修复、rmmod panic 教训等）。

kernelsu/ 目录下的源代码派生自 KernelSU，按 GPL-2.0 分发，与主仓库的 Apache-2.0 分开。

变更说明（相对上一版）

· 停止更新「原地弹出 # shell」功能：当前版本不再维护原地 # 提示符 + setcon("u:r:shell:s0") 这套路径；需要该功能的用户请使用 发行版 v5.0 及以下。当前版本以 KernelSU late-load 为主路径。
· 文件路径统一到 /data/local/tmp/ 根目录：ghostlock54、kernelsu.ko、load_ksu_54.sh 三个文件都放 /data/local/tmp/ 下，不再使用 /data/local/tmp/kernelsu/ 子目录。代码里 execl 的脚本路径、脚本里 insmod 的模块路径都按根目录写死。
· 删除 write_value(child_task + 0x8d0, 0)：adb shell 场景下子进程 seccomp.mode 本来就是 0，该写为空操作。写入次数由 5 次降到 4 次，减少一次 PI 竞态失败点，成功率由约 80% 提升。若在 App 进程内跑（父进程带 seccomp filter），需要把这步加回。
· log_context 保留：攻击前后打印进程上下文（纯文件 IO，读 /proc/self/attr/current）。
· 编译命令：clang -O2 -pthread ghostlock54.c -o ghostlock54 -lselinux

许可证

Apache License 2.0