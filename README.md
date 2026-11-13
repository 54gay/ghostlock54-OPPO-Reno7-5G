GhostLock54 for OPPO Reno7 5G

针对 OPPO Reno7 5G（型号 OP5227）的 GhostLock（CVE-2026-43499）移植版。

目标内核：5.4.254-qgki-gaa96eae94d32，Android 12。

本程序通过 futex PI 越界写获得内核任意写原语，把目标进程的 cred 指针整体替换为 init_cred，并清掉 seccomp，达到：

· 真实 uid=0 / gid=0 / euid=0 / egid=0
· 完整 capability 集合（CapPrm / CapEff / CapBnd = 0x0000003fffffffff，来自 init_cred）
· SELinux 切换为 Permissive（enforce 1 → 0）
· 清目标任务的 task_struct.seccomp，让子进程 fork 后不再被 Android seccomp filter 拦截（Seccomp: 0）
· SELinux 域：**不做 setcon**，子进程保持 init_cred 绑定的 `u:r:kernel:s0`（exploit 已先把 SELinux 切到 permissive，kernel 域可直接执行命令）
· `-s/--shell` 模式：提权成功后直接在当前终端弹出拥有完整 Root 权限的 # Shell（不加载 KernelSU）

文件放置约定

与本 exploit 配套的文件统一放在 /data/local/tmp/ 根目录，不要再放子目录：

· /data/local/tmp/ghostlock54 —— 编译产物
· /data/local/tmp/kernelsu.ko —— KernelSU 模块，**内嵌在 ghostlock54 里，运行时会自动释放（覆盖）**（可用 -m/--module 指定其它路径）
· /data/local/tmp/kernelsu.sh —— **由 ghostlock54 每次启动时释放（覆盖）**的加载脚本

KernelSU 加载逻辑放在 ghostlock54 **每次启动时释放**的 `/data/local/tmp/kernelsu.sh` 里：
默认模式提权成功后直接执行该脚本（解析 kallsyms_lookup_name + insmod），完成后提示手动
打开 KernelSU Manager；`-s` 模式下可在 root shell 里手动执行同一个脚本。

两种使用模式：

· **默认**：加载 KernelSU（late-load kernelsu.ko），完成后提示手动打开 Manager，root shell 由 KernelSU 提供（u:r:ksu:s0）；
· **`-s/--shell`**：只拿 root shell，**不加载 KernelSU**，适合临时执行一次 root 命令。

说明：`-s` 模式下终端里会直接出现 `#` 提示符（context `u:r:kernel:s0`，SELinux permissive）；退出 shell 后 root 即消失（无持久 root）。

在该 root shell 里想加载 KernelSU 时，直接运行 ghostlock54 释放的脚本：

```sh
sh /data/local/tmp/kernelsu.sh            # 默认加载 /data/local/tmp/kernelsu.ko
sh /data/local/tmp/kernelsu.sh /path/to/kernelsu.ko
```

脚本每次启动 ghostlock54 时都会重新写入 /data/local/tmp/kernelsu.sh（存在则覆盖）。

防内核崩溃提示

由于漏洞利用后必须保持主进程存活以防内核悬挂指针引发崩溃（Kernel Panic），默认模式下父进程会 daemonize 并原地挂起保活（不承载交互 shell）；`-s` 模式下父进程等待 root shell 退出后正常结束。如果想彻底清理状态，直接按电源键重启手机即可。

编译

一条命令搞定（make 会先编模块，再把它链接进 ghostlock54）。所有构建产物都在 `build/`：

```
build/kernelsu.full.ko   # 符号链接 -> kernelsu/src/kernel/kernelsu.ko（不复制，省空间）
build/kernelsu.ko        # 内嵌用：STRIP=1 时是 strip 后的副本（约 325KB）；STRIP=0 时是符号链接
build/kernelsu_ko.o      # objcopy 对象文件
ghostlock54              # 最终单文件
```

```sh
make            # 默认 STRIP=1：内嵌前去掉 ko 的调试段（ko 约 325KB，二进制约 360KB）
make STRIP=0    # 保留完整 ko（约 5.7MB，二进制约 5.8MB）
make module     # 只编模块 -> build/kernelsu.ko
make clean
```



验证动态库依赖：

```sh
nm -u ghostlock54 | grep -i selinux    # 应无输出（已不再链接 libselinux）
```

运行

ghostlock54 是**单文件**：kernelsu.ko 已内嵌，运行时自动释放到 /data/local/tmp/kernelsu.ko（存在则覆盖）。

```sh
adb push ghostlock54 /data/local/tmp/ghostlock54
adb shell chmod 755 /data/local/tmp/ghostlock54
```

只允许 uid 0（root）或 uid 2000（shell，adb shell）运行，其它 uid 直接输出 `Permission denied`（无颜色）。
`-s/--shell` 只开一个 root shell，**不加载 KernelSU**（适合临时执行一次 root 命令）：

```sh
adb shell -t /data/local/tmp/ghostlock54 -s
```

启动漏洞利用：

```sh
adb shell -t /data/local/tmp/ghostlock54
# 指定其它模块路径：
adb shell -t /data/local/tmp/ghostlock54 -m /path/to/kernelsu.ko
# 查看帮助：
adb shell -t /data/local/tmp/ghostlock54 -h
```

· `-s/--shell`：程序跑完后，终端会直接变成 # 提示符（uid=0，全 cap，context u:r:shell:s0），可以直接执行 id / getenforce / cat /proc/self/attr/current；**不加载 KernelSU**，exit 后 root 消失。
· 默认：不弹 #。exploit 拿到内核任意写、把 cred 换成 init_cred、切 permissive，然后执行释放出来的 /data/local/tmp/kernelsu.sh 加载模块（默认 /data/local/tmp/kernelsu.ko），完成后提示手动打开 KernelSU Manager；之后用 Manager 授权 app，在 app 终端里 su 拿 u:r:ksu:s0 的 root shell。

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

/proc/self/status 示例（`-s` root shell 内）：

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

关于 SELinux 域

提权后子进程 uid=0、CapEff 全满，SELinux context 是 `u:r:kernel:s0`——因为 cred 被整体替换为 init_cred，而 init_cred->security 绑定的就是 kernel 域的 sid。

该版本**不调用 setcon**，直接用回原来的 kernel 域：

· exploit 在提权过程中已把 SELinux 切为 permissive（enforce 1 → 0），因此 kernel 域的 root shell 可以直接执行 /system/bin 命令、加载模块；
· 默认模式加载 KernelSU 后由模块自行决定 enforcing，root shell 由 KernelSU 提供 `u:r:ksu:s0`；
· `-s/--shell` 模式不加载模块，SELinux 保持 permissive，root shell 的 context 就是 `u:r:kernel:s0`。

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

加载逻辑已集成进 ghostlock54 本体，不再需要任何 sh 脚本。文件放置（重要）：两个文件都放在 /data/local/tmp/ 根目录：

```
/data/local/tmp/ghostlock54
/data/local/tmp/kernelsu.ko
```

流程：

1. 运行 ghostlock54（可加 -m/--module 指定其它 ko 路径；-s/--shell 只开 root shell 不加载 KSU；-h/--help 查看帮助）
2. exploit 提权成功后执行 /data/local/tmp/kernelsu.sh（解析 kallsyms_lookup_name、insmod），完成后提示手动打开 KernelSU Manager
3. 在 Manager 里给需要的 app 授权；之后任意已授权 app 的终端里 su 即可得到 u:r:ksu:s0 的 root shell

模块是 runtime 加载（late-load），不修改 boot.img / 任何分区；重启后重新跑一次 ghostlock54 即可。

SELinux：模块默认 setenforce 1（强制模式）；若某状态（例如旧的 exploit 破坏过 SELinux）强制模式让系统异常，加载时加 permissive=1 参数保持当前模式。exploit 不再 setcon，提权阶段 SELinux 为 permissive（由 exploit 写入），模块加载不受影响。

详见 kernelsu/README.md 与 kernelsu/SUMMARY_5.4_KSU.md（移植全过程与踩坑记录，包括 ColorOS 隐藏内核反提权 oplus_root_check 的绕过、5.4 原生 sepolicy 注入、终端 su 修复、rmmod panic 教训等）。

kernelsu/ 目录下的源代码派生自 KernelSU，按 GPL-2.0 分发，与主仓库的 Apache-2.0 分开。

变更说明（相对上一版）

· KernelSU 加载逻辑改为幽灵脚本：ghostlock54 启动时释放 /data/local/tmp/kernelsu.sh（覆盖），默认模式执行它加载模块，-s 模式可手动执行；新增 -m/--module 与 -h/--help 参数。
· 新增 `-s/--shell`：只开 root shell（保持 u:r:kernel:s0，不 setcon），不加载 KernelSU；默认模式加载模块并提示打开 Manager。
· 新增 kernelsu.sh 释放：ghostlock54 每次启动时把内嵌的加载脚本写到 /data/local/tmp/kernelsu.sh（已存在则覆盖），方便在 `-s` 的 root shell 里手动加载模块。
· 单文件发布：kernelsu.ko 内嵌进 ghostlock54，运行时释放到 /data/local/tmp/kernelsu.ko；发行版只提供 ghostlock54 一个文件。
· 删除 write_value(child_task + 0x8d0, 0)：adb shell 场景下子进程 seccomp.mode 本来就是 0，该写为空操作。写入次数由 5 次降到 4 次，减少一次 PI 竞态失败点，成功率由约 80% 提升。若在 App 进程内跑（父进程带 seccomp filter），需要把这步加回。
· log_context 保留：攻击前后打印进程上下文（纯文件 IO，读 /proc/self/attr/current）。
· 编译命令：llvm-objcopy 把 kernelsu.ko 转成对象后与 ghostlock54.c 一起链接（见上方「编译」）。

许可证

Apache License 2.0