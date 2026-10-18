#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <signal.h>

#define SOCKET_PATH "/data/local/tmp/root.sock"

void print_help() {
    printf("Usage: su [options] [uid,gid,groups...] [command [args...]]\n\n");
    printf("Options:\n");
    printf("  -c <command>  Execute command as root\n");
    printf("  -h, --help    Show this help message\n\n");
    printf("Examples:\n");
    printf("  su                           # Interactive root shell\n");
    printf("  su 1000                      # Interactive shell as uid/gid 1000\n");
    printf("  su 1000,1000,1001 id         # Run 'id' as uid=1000, gid=1000, groups=1001\n");
    printf("  su -c \"id\"                   # Run 'id' as root\n");
    printf("  su 0 -c id                   # -c can be placed anywhere\n");
}

int is_valid_id_format(const char *str) {
    if (!str || !*str) return 0;
    int last_was_comma = 0, has_digit = 0;
    for (int i = 0; str[i]; i++) {
        if (str[i] >= '0' && str[i] <= '9') { has_digit = 1; last_was_comma = 0; }
        else if (str[i] == ',') {
            if (last_was_comma || !has_digit) return 0; 
            last_was_comma = 1;
        } else return 0;
    }
    return has_digit && !last_was_comma;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    uid_t uid = 0; gid_t gid = 0; char groups[256] = "0"; char cmd[2048] = {0};
    
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help(); return 0;
    }

    int has_uid = 0;
    int cmd_idx = -1;

    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "-c") == 0) {
            cmd_idx = j + 1;
            break;
        } else if (is_valid_id_format(argv[j]) && !has_uid) {
            has_uid = 1;
            char *saveptr = NULL;
            char *token = strtok_r(argv[j], ",", &saveptr);
            if (token) {
                uid = atoi(token); gid = uid;
                token = strtok_r(NULL, ",", &saveptr);
                if (token) gid = atoi(token);
                int g_count = 0; groups[0] = '\0';
                while ((token = strtok_r(NULL, ",", &saveptr)) && g_count < 31) {
                    strcat(groups, token); strcat(groups, ","); g_count++;
                }
                if (g_count == 0) sprintf(groups, "%d", gid);
                else groups[strlen(groups) - 1] = '\0';
            }
        } else {
            if (j == 1) {
                fprintf(stderr, "[-] 错误: 不支持的参数 '%s' 或格式不正确。\n\n", argv[1]);
                print_help(); return 1;
            }
            cmd_idx = j;
            break;
        }
    }

    if (cmd_idx != -1) {
        if (cmd_idx >= argc) {
            fprintf(stderr, "[-] 错误: -c 后面必须跟命令。\n\n");
            print_help(); return 1;
        }
        for (int j = cmd_idx; j < argc; j++) {
            strcat(cmd, argv[j]);
            if (j < argc - 1) strcat(cmd, " ");
        }
    }

    char payload[4096];
    snprintf(payload, sizeof(payload), "%d:%d:%s:%s", uid, gid, groups, cmd);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[-] 连接 Root Daemon 失败。\n");
        return 1;
    }

    int master = -1, slave = -1;
    if (isatty(STDIN_FILENO)) {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master >= 0) {
            grantpt(master); unlockpt(master);
            slave = open(ptsname(master), O_RDWR | O_NOCTTY);
        }
    }

    int fds[3];
    fds[0] = (slave >= 0) ? slave : STDIN_FILENO;
    fds[1] = (slave >= 0) ? slave : STDOUT_FILENO;
    fds[2] = (slave >= 0) ? slave : STDERR_FILENO;

    struct msghdr msg = {0};
    struct iovec io = { .iov_base = payload, .iov_len = strlen(payload) };
    char cmsgbuf[CMSG_SPACE(sizeof(int) * 3)];
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = cmsgbuf; msg.msg_controllen = sizeof(cmsgbuf);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 3);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * 3);
    
    sendmsg(sock, &msg, 0);
    if (slave >= 0) close(slave);

    char exit_code = 1;
    if (master >= 0) {
        struct termios orig, raw;
        tcgetattr(STDIN_FILENO, &orig);
        raw = orig; cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        fd_set readfds; char buf[4096]; int n;
        while (1) {
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds); FD_SET(master, &readfds); FD_SET(sock, &readfds);
            if (select((master > sock ? master : sock) + 1, &readfds, NULL, NULL, NULL) < 0) break;

            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                n = read(STDIN_FILENO, buf, sizeof(buf)); if (n <= 0) break;
                write(master, buf, n);
            }
            if (FD_ISSET(master, &readfds)) {
                n = read(master, buf, sizeof(buf)); if (n <= 0) break;
                write(STDOUT_FILENO, buf, n);
            }
            if (FD_ISSET(sock, &readfds)) {
                read(sock, &exit_code, 1); break;
            }
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        close(master);
    } else {
        read(sock, &exit_code, 1);
    }

    close(sock);
    return (unsigned char)exit_code;
}