#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <grp.h>

#define SOCKET_PATH "/data/local/tmp/root.sock"
#define WHITELIST_PATH "/data/adb/rootd/whitelist"
#define PACKAGES_LIST "/data/system/packages.list"

int get_uid_by_pkg(const char *pkg) {
    FILE *fp = fopen(PACKAGES_LIST, "r"); if (!fp) return -1;
    char line[512], name[256]; int uid;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%255s %d", name, &uid) == 2 && strcmp(name, pkg) == 0) { fclose(fp); return uid; }
    }
    fclose(fp); return -1;
}

int check_whitelist(uid_t uid) {
    if (uid == 0 || uid == 1000 || uid == 2000) return 1;
    FILE *fp = fopen(WHITELIST_PATH, "r"); if (!fp) return 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;
        if (strncmp(p, "uid:", 4) == 0) {
            if (uid == (uid_t)atoi(p + 4)) { fclose(fp); return 1; }
        } else if (uid == (uid_t)get_uid_by_pkg(p)) { fclose(fp); return 1; }
    }
    fclose(fp); return 0;
}

int main() {
    // 1. 单例检测
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        struct sockaddr_un test_addr = { .sun_family = AF_UNIX };
        strncpy(test_addr.sun_path, SOCKET_PATH, sizeof(test_addr.sun_path) - 1);
        if (connect(test_fd, (struct sockaddr *)&test_addr, sizeof(test_addr)) == 0) {
            close(test_fd); return 0; // 已经有一个在跑了，静默退出
        }
        close(test_fd);
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN); 

    unlink(SOCKET_PATH);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) return 1;
    chmod(SOCKET_PATH, 0777);
    if (listen(server, 10) < 0) return 1;

    // 2. 核心服务循环
    while (1) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;

        struct ucred cred; socklen_t len = sizeof(cred);
        if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0 || !check_whitelist(cred.uid)) {
            const char *msg = "[-] Denied.\n"; write(client, msg, strlen(msg)); close(client); continue;
        }

        char payload[4096] = {0}; int fds[3] = {-1, -1, -1};
        struct msghdr msg = {0}; struct iovec io = { .iov_base = payload, .iov_len = sizeof(payload) - 1 };
        char cmsgbuf[CMSG_SPACE(sizeof(int) * 3)];
        msg.msg_iov = &io; msg.msg_iovlen = 1; msg.msg_control = cmsgbuf; msg.msg_controllen = sizeof(cmsgbuf);
        
        if (recvmsg(client, &msg, 0) > 0) {
            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
            if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
                memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * 3);
        }

        // 3. 解析 payload: uid:gid:groups:cmd
        char *uid_s = payload, *gid_s = strchr(uid_s, ':');
        if (!gid_s) { close(client); continue; } *gid_s++ = '\0';
        char *grp_s = strchr(gid_s, ':');
        if (!grp_s) { close(client); continue; } *grp_s++ = '\0';
        char *cmd = strchr(grp_s, ':');
        if (!cmd) { close(client); continue; } *cmd++ = '\0';

        // 4. 执行逻辑
        pid_t pid = fork();
        if (pid == 0) {
            close(server);
            if (fds[0] != -1) { dup2(fds[0], 0); dup2(fds[1], 1); dup2(fds[2], 2); }
            
            if (isatty(0)) {
                setsid(); ioctl(0, TIOCSCTTY, 0);
                signal(SIGTTIN, SIG_IGN); signal(SIGTTOU, SIG_IGN);
            }

            uid_t u = atoi(uid_s); gid_t g = atoi(gid_s);
            if (u > 0) {
                gid_t g_arr[32]; int g_cnt = 0; char *saveptr = NULL;
                char *g_tok = strtok_r(grp_s, ",", &saveptr);
                while (g_tok && g_cnt < 32) { g_arr[g_cnt++] = atoi(g_tok); g_tok = strtok_r(NULL, ",", &saveptr); }
                setgid(g);
                if (g_cnt > 0) setgroups(g_cnt, g_arr); else setgroups(1, &g);
                setuid(u);
            }

            pid_t inner = fork();
            if (inner == 0) {
                if (strlen(cmd) == 0) execl("/system/bin/sh", "sh", NULL);
                else execl("/system/bin/sh", "sh", "-c", cmd, NULL);
                exit(1);
            }
            
            int status; waitpid(inner, &status, 0);
            char exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            write(client, &exit_code, 1);
            exit(0);
        }
        
        if (fds[0] != -1) { close(fds[0]); close(fds[1]); close(fds[2]); }
        close(client);
    }
    return 0;
}
