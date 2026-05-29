#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#endif

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

#if defined(_WIN32)
typedef HANDLE status_handle_t;
#define STATUS_HANDLE_INVALID NULL
#define PATH_SEPARATOR '\\'
#else
typedef int status_handle_t;
#define STATUS_HANDLE_INVALID (-1)
#define PATH_SEPARATOR '/'
#endif

static unsigned char *g_allocation;
static size_t g_allocation_size;
static volatile sig_atomic_t g_terminate_requested;

static void usage(FILE *out) {
    fprintf(out,
            "usage:\n"
            "  a1-pol-mem start <pct-of-total-system-ram> [--foreground]\n"
            "  a1-pol-mem --help\n");
}

static void fatal(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void request_termination(void) {
    g_terminate_requested = 1;
}

static void signal_handler(int signum) {
    (void)signum;
    request_termination();
}

#if defined(_WIN32)
static BOOL WINAPI console_handler(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        request_termination();
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

static int install_signal_handlers(void) {
#if defined(_WIN32)
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        return -1;
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        return -1;
    }
    if (signal(SIGBREAK, signal_handler) == SIG_ERR) {
        return -1;
    }
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        return -1;
    }
    return 0;
#else
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGHUP, &sa, NULL) != 0) {
        return -1;
    }

    return 0;
#endif
}

static void free_allocation(void) {
    if (g_allocation != NULL) {
        free(g_allocation);
        g_allocation = NULL;
        g_allocation_size = 0;
    }
}

static bool get_total_ram_bytes(uint64_t *total_bytes) {
#if defined(_WIN32)
    MEMORYSTATUSEX status;

    memset(&status, 0, sizeof(status));
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        *total_bytes = (uint64_t)status.ullTotalPhys;
        return true;
    }
#endif

#if defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        *total_bytes = (uint64_t)info.totalram * (uint64_t)info.mem_unit;
        return true;
    }
#endif

#if defined(__APPLE__)
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0 && memsize > 0) {
        *total_bytes = memsize;
        return true;
    }
#endif

#if !defined(_WIN32) && defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        *total_bytes = (uint64_t)pages * (uint64_t)page_size;
        return true;
    }
#endif

    return false;
}

static bool parse_pct(const char *input, double *pct) {
    char *end = NULL;

    errno = 0;
    *pct = strtod(input, &end);
    if (errno != 0 || end == input || *end != '\0') {
        return false;
    }

    return *pct > 0.0L && *pct <= 100.0L;
}

static bool parse_size_arg(const char *input, size_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(input, &end, 10);
    if (errno != 0 || end == input || *end != '\0' || parsed == 0) {
        return false;
    }
    if (parsed > (unsigned long long)SIZE_MAX) {
        return false;
    }

    *value = (size_t)parsed;
    return true;
}

static bool compute_allocation_size(double pct, size_t *bytes) {
    uint64_t total_ram = 0;
    double requested;

    if (!get_total_ram_bytes(&total_ram) || total_ram == 0) {
        return false;
    }

    requested = ((double)total_ram * pct) / 100.0;
    if (requested < 1.0 || requested > (double)SIZE_MAX) {
        return false;
    }

    *bytes = (size_t)requested;
    return *bytes > 0;
}

static bool status_handle_is_valid(status_handle_t handle) {
#if defined(_WIN32)
    return handle != NULL && handle != INVALID_HANDLE_VALUE;
#else
    return handle >= 0;
#endif
}

static void status_handle_close(status_handle_t handle) {
    if (!status_handle_is_valid(handle)) {
        return;
    }
#if defined(_WIN32)
    CloseHandle(handle);
#else
    close(handle);
#endif
}

static bool status_write(status_handle_t handle, const char *s) {
    size_t len = strlen(s);
    size_t offset = 0;

    while (offset < len) {
#if defined(_WIN32)
        DWORD written = 0;
        DWORD want = (DWORD)((len - offset) > UINT32_MAX ? UINT32_MAX : (len - offset));
        if (!WriteFile(handle, s + offset, want, &written, NULL)) {
            return false;
        }
        if (written == 0) {
            return false;
        }
        offset += written;
#else
        ssize_t n = write(handle, s + offset, len - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        offset += (size_t)n;
#endif
    }

    return true;
}

static bool fill_random(unsigned char *buf, size_t len) {
    const size_t chunk_size = 1024U * 1024U;
    size_t offset = 0;

#if !defined(_WIN32)
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return false;
    }
#endif

    while (offset < len && !g_terminate_requested) {
        size_t remaining = len - offset;
        size_t want = remaining < chunk_size ? remaining : chunk_size;

#if defined(_WIN32)
        while (want > 0 && !g_terminate_requested) {
            ULONG this_read = (ULONG)(want > ULONG_MAX ? ULONG_MAX : want);
            NTSTATUS rc = BCryptGenRandom(NULL, buf + offset, this_read,
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (rc < 0) {
                return false;
            }
            offset += this_read;
            want -= this_read;
        }
#else
        size_t got = 0;
        while (got < want && !g_terminate_requested) {
            ssize_t n = read(fd, buf + offset + got, want - got);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                return false;
            }
            if (n == 0) {
                close(fd);
                return false;
            }
            got += (size_t)n;
        }
        offset += got;
#endif
    }

#if !defined(_WIN32)
    close(fd);
#endif
    return !g_terminate_requested;
}

static bool allocate_and_fill(size_t bytes) {
    g_allocation = (unsigned char *)malloc(bytes);
    if (g_allocation == NULL) {
        return false;
    }

    g_allocation_size = bytes;
    if (!fill_random(g_allocation, g_allocation_size)) {
        free_allocation();
        return false;
    }

    return true;
}

static const char *format_bytes(uint64_t bytes, char *buf, size_t buf_len) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = (double)bytes;
    size_t unit = 0;

    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }

    snprintf(buf, buf_len, "%.2f %s", value, units[unit]);
    return buf;
}

static void idle_until_signal(void) {
    while (!g_terminate_requested) {
#if defined(_WIN32)
        Sleep(1000);
#else
        struct timespec ts;
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
        while (nanosleep(&ts, &ts) != 0 && errno == EINTR && !g_terminate_requested) {
        }
#endif
    }
}

static void redirect_stdio_to_null(void) {
#if defined(_WIN32)
    (void)freopen("NUL", "r", stdin);
    (void)freopen("NUL", "w", stdout);
    (void)freopen("NUL", "w", stderr);
#else
    int fd = open("/dev/null", O_RDWR);

    if (fd < 0) {
        return;
    }

    (void)dup2(fd, STDIN_FILENO);
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO) {
        close(fd);
    }
#endif
}

static unsigned long current_pid(void) {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static int run_worker(size_t bytes, bool foreground, status_handle_t status_handle) {
    char size_buf[64];

    if (install_signal_handlers() != 0) {
        if (status_handle_is_valid(status_handle)) {
            (void)status_write(status_handle, "error: failed to install signal handlers\n");
        } else {
            fatal("error: failed to install signal handlers");
        }
        return 1;
    }

    if (foreground) {
        fprintf(stderr, "allocating %s\n", format_bytes((uint64_t)bytes, size_buf, sizeof(size_buf)));
    }

    if (!allocate_and_fill(bytes)) {
        if (g_terminate_requested) {
            free_allocation();
            return 0;
        }
        if (status_handle_is_valid(status_handle)) {
            (void)status_write(status_handle, "error: allocation or random fill failed\n");
        } else {
            fatal("error: allocation or random fill failed");
        }
        return 1;
    }

    if (status_handle_is_valid(status_handle)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "started pid=%lu allocated=%s\n", current_pid(),
                 format_bytes((uint64_t)bytes, size_buf, sizeof(size_buf)));
        (void)status_write(status_handle, msg);
        status_handle_close(status_handle);
        status_handle = STATUS_HANDLE_INVALID;
    } else if (foreground) {
        fprintf(stderr, "holding %s; terminate with SIGTERM/SIGINT/SIGHUP or Ctrl-C\n",
                format_bytes((uint64_t)bytes, size_buf, sizeof(size_buf)));
    }

    idle_until_signal();
    free_allocation();

    if (foreground) {
        fprintf(stderr, "exiting cleanly\n");
    }

    return 0;
}

#if defined(_WIN32)
static bool append_quoted_arg(char *dst, size_t dst_len, const char *arg) {
    size_t used = strlen(dst);

    if (used + 3 >= dst_len) {
        return false;
    }

    dst[used++] = '"';
    for (const char *p = arg; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            if (used + 1 >= dst_len) {
                return false;
            }
            dst[used++] = '\\';
        }
        if (used + 1 >= dst_len) {
            return false;
        }
        dst[used++] = *p;
    }
    dst[used++] = '"';
    dst[used] = '\0';
    return true;
}

static bool append_raw_arg(char *dst, size_t dst_len, const char *arg) {
    size_t used = strlen(dst);
    size_t arg_len = strlen(arg);

    if (used + 1 + arg_len >= dst_len) {
        return false;
    }

    dst[used++] = ' ';
    memcpy(dst + used, arg, arg_len + 1);
    return true;
}

static int start_daemon(size_t bytes, const char *program_path) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_handle = NULL;
    HANDLE write_handle = NULL;
    char command_line[4096];
    char bytes_arg[64];
    char handle_arg[64];
    char status[512];
    DWORD bytes_read = 0;
    int rc = 1;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&read_handle, &write_handle, &sa, 0)) {
        fatal("error: CreatePipe failed");
        return 1;
    }
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        fatal("error: SetHandleInformation failed");
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return 1;
    }

    snprintf(bytes_arg, sizeof(bytes_arg), "%llu", (unsigned long long)bytes);
    snprintf(handle_arg, sizeof(handle_arg), "%llu",
             (unsigned long long)(uintptr_t)write_handle);

    command_line[0] = '\0';
    if (!append_quoted_arg(command_line, sizeof(command_line), program_path) ||
        !append_raw_arg(command_line, sizeof(command_line), "--worker") ||
        !append_raw_arg(command_line, sizeof(command_line), bytes_arg) ||
        !append_raw_arg(command_line, sizeof(command_line), handle_arg)) {
        fatal("error: command line too long");
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return 1;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
        fatal("error: CreateProcess failed");
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return 1;
    }

    CloseHandle(write_handle);
    write_handle = NULL;

    if (ReadFile(read_handle, status, (DWORD)sizeof(status) - 1, &bytes_read, NULL) &&
        bytes_read > 0) {
        status[bytes_read] = '\0';
        fputs(status, stderr);
        rc = strncmp(status, "started ", 8) == 0 ? 0 : 1;
    } else {
        fatal("error: daemon exited before reporting status");
        rc = 1;
    }

    CloseHandle(read_handle);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return rc;
}
#else
static int start_daemon(size_t bytes, const char *program_path) {
    int pipefd[2];
    pid_t first_child;
    char status[512];
    ssize_t n;
    int child_status = 0;

    (void)program_path;

    if (pipe(pipefd) != 0) {
        fatal("error: pipe failed: %s", strerror(errno));
        return 1;
    }

    first_child = fork();
    if (first_child < 0) {
        fatal("error: fork failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (first_child == 0) {
        pid_t second_child;

        close(pipefd[0]);
        if (setsid() < 0) {
            (void)status_write(pipefd[1], "error: setsid failed\n");
            _exit(1);
        }

        second_child = fork();
        if (second_child < 0) {
            (void)status_write(pipefd[1], "error: second fork failed\n");
            _exit(1);
        }

        if (second_child > 0) {
            _exit(0);
        }

        umask(022);
        (void)chdir("/");
        redirect_stdio_to_null();
        _exit(run_worker(bytes, false, pipefd[1]));
    }

    close(pipefd[1]);

    n = read(pipefd[0], status, sizeof(status) - 1);
    if (n > 0) {
        status[n] = '\0';
        fputs(status, stderr);
        child_status = strncmp(status, "started ", 8) == 0 ? 0 : 1;
    } else {
        fatal("error: daemon exited before reporting status");
        child_status = 1;
    }

    close(pipefd[0]);
    (void)waitpid(first_child, NULL, 0);
    return child_status;
}
#endif

static int start_command(int argc, char **argv) {
    bool foreground = false;
    double pct = 0.0;
    size_t bytes = 0;

    if (argc < 3 || argc > 4) {
        usage(stderr);
        return 2;
    }

    if (!parse_pct(argv[2], &pct)) {
        fatal("error: percentage must be a number greater than 0 and no more than 100");
        return 2;
    }

    if (argc == 4) {
        if (strcmp(argv[3], "--foreground") != 0) {
            usage(stderr);
            return 2;
        }
        foreground = true;
    }

    if (!compute_allocation_size(pct, &bytes)) {
        fatal("error: failed to compute allocation size");
        return 1;
    }

    if (foreground) {
        return run_worker(bytes, true, STATUS_HANDLE_INVALID);
    }

    return start_daemon(bytes, argv[0]);
}

static int worker_command(int argc, char **argv) {
    size_t bytes = 0;
    status_handle_t status_handle = STATUS_HANDLE_INVALID;

    if (argc != 4 || !parse_size_arg(argv[2], &bytes)) {
        return 2;
    }

#if defined(_WIN32)
    {
        size_t handle_value = 0;
        if (!parse_size_arg(argv[3], &handle_value)) {
            return 2;
        }
        status_handle = (HANDLE)(uintptr_t)handle_value;
    }
    redirect_stdio_to_null();
#else
    {
        size_t fd_value = 0;
        if (!parse_size_arg(argv[3], &fd_value) || fd_value > (size_t)INT_MAX) {
            return 2;
        }
        status_handle = (int)fd_value;
    }
#endif

    return run_worker(bytes, false, status_handle);
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "--worker") == 0) {
        return worker_command(argc, argv);
    }

    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        return start_command(argc, argv);
    }

    usage(stderr);
    return 2;
}
