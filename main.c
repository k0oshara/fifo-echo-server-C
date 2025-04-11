#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* default configuration */
#define DEFAULT_FIFO_PATH "/tmp/echo_fifo"
#define DEFAULT_LOG_PATH  "/tmp/echo_fifo.log"
#define DEFAULT_ALARM_SEC 5
#define BUF_SIZE          4096

static const char *fifo_path    = DEFAULT_FIFO_PATH;
static const char *log_path     = DEFAULT_LOG_PATH;
static unsigned   alarm_sec     = DEFAULT_ALARM_SEC;
static int        run_as_daemon = 0;
static FILE       *log_fp       = NULL;

/* statistics */
struct stats_s {
    unsigned long      received_messages;
    unsigned long long bytes;
    unsigned long      alarms;
} stats = {0};

/* signal flags */
static volatile sig_atomic_t flag_term  = 0; /* SIGTERM */
static volatile sig_atomic_t flag_int   = 0; /* SIGINT  */
static volatile sig_atomic_t flag_alarm = 0; /* SIGALRM */
static volatile sig_atomic_t flag_usr1  = 0; /* SIGUSR1 */
static volatile sig_atomic_t flag_hup   = 0; /* SIGHUP  */

static void log_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp ? log_fp : stdout, fmt, ap);
    fputc('\n', log_fp ? log_fp : stdout);
    fflush(log_fp ? log_fp : stdout);
    va_end(ap);
}

static void print_stats(void) {
    log_msg("=== statistics ===\nMessages: %lu\nBytes: %llu\nAlarms: %lu", stats.received_messages, stats.bytes, stats.alarms);
}

/* signal handlers */
static void handle_sigterm(int signo) { (void)signo; flag_term  = 1; }
static void handle_sigint (int signo) { (void)signo; flag_int   = 1; }
static void handle_sigalrm(int signo) { (void)signo; flag_alarm = 1; }
static void handle_sigusr1(int signo) { (void)signo; flag_usr1  = 1; }
static void handle_sighup(int signo)  { (void)signo; flag_hup   = 1; }

static void install_handler(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void become_daemon(void) {
    if (run_as_daemon) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);
    
    if (setsid() == -1) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") == -1) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    FILE *new_log = fopen(log_path, "a");
    if (!new_log) {
        perror("fopen log (daemon)");
        exit(EXIT_FAILURE);
    }

    int fd = fileno(new_log);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) close(fd);

    log_fp = stdout;
    run_as_daemon = 1;
    log_msg("[INFO] switched to daemon mode by SIGHUP");
    print_stats();
}

static void diagnostic(void) {
    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%F %T", localtime(&now));
    log_msg("[%s] waiting for data...", tbuf);
    stats.alarms++;
}

static void ensure_fifo(void) {
    if (mkfifo(fifo_path, 0600) == -1) {
        if (errno == EEXIST) {
            struct stat st;
            if (stat(fifo_path, &st) == -1) {
                perror("stat fifo");
                exit(EXIT_FAILURE);
            }
            if (!S_ISFIFO(st.st_mode)) {
                fprintf(stderr, "%s exists and is not a FIFO\n", fifo_path);
                exit(EXIT_FAILURE);
            }
        } else {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }
}

static void main_loop(void) {
    char buf[BUF_SIZE];

    alarm(alarm_sec);

    for (;;) {
        int fd;
retry_open:
        fd = open(fifo_path, O_RDONLY);
        if (fd == -1) {
            if (errno == EINTR) {
                if (flag_term) goto graceful_term;
                if (flag_int)  goto graceful_int;
                if (flag_alarm) { diagnostic(); flag_alarm = 0; alarm(alarm_sec); }
                if (flag_usr1)  { print_stats(); flag_usr1 = 0; }
                if (flag_hup)   { become_daemon(); flag_hup = 0; }
                goto retry_open;
            }
            perror("open fifo");
            exit(EXIT_FAILURE);
        }

        stats.received_messages++;
        unsigned long msg_bytes = 0;

        while (1) {
            ssize_t n = read(fd, buf, sizeof(buf)-1);
            if (n == -1) {
                if (errno == EINTR) {
                    if (flag_term) { close(fd); goto graceful_term; }
                    if (flag_int)  { continue; }
                    if (flag_alarm){ diagnostic(); flag_alarm = 0; alarm(alarm_sec); }
                    if (flag_usr1) { print_stats(); flag_usr1 = 0; }
                    if (flag_hup)  { become_daemon(); flag_hup = 0; }
                    continue;
                }
                perror("read");
                exit(EXIT_FAILURE);
            }
            if (n == 0) break; /* EOF */
            buf[n] = '\0';
            fputs(buf, log_fp ? log_fp : stdout);
            if (buf[n-1] != '\n') fputc('\n', log_fp ? log_fp : stdout);
            fflush(log_fp ? log_fp : stdout);
            msg_bytes += n;
        }

        stats.bytes += msg_bytes;
        close(fd);

        if (flag_term) goto graceful_term;
        if (flag_int)  goto graceful_int;
        if (flag_alarm){ diagnostic(); flag_alarm = 0; alarm(alarm_sec); }
        if (flag_usr1) { print_stats(); flag_usr1 = 0; }
        if (flag_hup)  { become_daemon(); flag_hup = 0; }
    }

graceful_int:
    log_msg("[INFO] SIGINT received – finishing current message and exiting");
graceful_term:
    log_msg("[INFO] termination requested – exiting");
    print_stats();
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-d] [-p fifo_path] [-l log_file] [-n seconds]\n"
            "  -d            start as daemon\n"
            "  -p <path>     fifo path (default %s)\n"
            "  -l <file>     log file  (default %s)\n"
            "  -n <seconds>  diagnostic interval (default %u)\n",
            prog, DEFAULT_FIFO_PATH, DEFAULT_LOG_PATH, DEFAULT_ALARM_SEC);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "dp:l:n:")) != -1) {
        switch (opt) {
        case 'd': run_as_daemon = 1; break;
        case 'p': fifo_path     = optarg; break;
        case 'l': log_path      = optarg; break;
        case 'n': alarm_sec     = (unsigned)atoi(optarg); break;
        default:  usage(argv[0]);
        }
    }

    /* open initial log */
    log_fp = run_as_daemon ? fopen(log_path, "a") : stdout;
    if (!log_fp) {
        perror("fopen log");
        exit(EXIT_FAILURE);
    }

    /* ignore SIGQUIT */
    signal(SIGQUIT, SIG_IGN);

    /* install handlers */
    install_handler(SIGTERM, handle_sigterm, 0);
    install_handler(SIGINT,  handle_sigint,  0);
    install_handler(SIGALRM, handle_sigalrm, 0);
    install_handler(SIGUSR1, handle_sigusr1, 0);
    install_handler(SIGHUP,  handle_sighup,  0);

    if (run_as_daemon) become_daemon();

    ensure_fifo();

    log_msg("[INFO] started. fifo=%s, log=%s, interval=%u sec", fifo_path, log_path, alarm_sec);

    main_loop();

    /* cleanup */
    unlink(fifo_path);
    if (log_fp && log_fp != stdout) fclose(log_fp);

    return 0;
}
