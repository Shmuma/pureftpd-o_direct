#include <config.h>

#define DEFINE_GLOBALS
#include "messages.h"
#include "ftpd_p.h"
#include "dynamic.h"
#include "ftpwho-update.h"
#include "ftpwho-read.h"
#include "globals.h"
#include "caps.h"
#if defined(WITH_UPLOAD_SCRIPT)
# include "upload-pipe.h"
#endif
#ifdef WITH_ALTLOG
# include "altlog.h"
#endif
#ifdef QUOTAS
# include "quotas.h"
#endif
#ifdef WITH_DIRALIASES
# include "diraliases.h"
#endif
#include "ftpd.h"
#include "bsd-glob.h"
#include "getloadavg.h"
#ifdef WITH_PRIVSEP
# include "privsep.h"
#endif
#ifdef WITH_TLS
# include "tls.h"
#endif
#ifdef WITH_BONJOUR
# include "bonjour.h"
#endif

#ifdef WITH_DMALLOC
# include <dmalloc.h>
#endif

#ifndef HAVE_SYS_FSUID_H
void disablesignals(void)
{
    sigset_t sigs;
    
    sigfillset(&sigs);
    if (sigprocmask(SIG_BLOCK, &sigs, &old_sigmask) < 0) {
        _EXIT(EXIT_FAILURE);
    }
}

static void enablesignals(void)
{
    if (sigprocmask(SIG_SETMASK, &old_sigmask, NULL) < 0) {
        _EXIT(EXIT_FAILURE);
    }
}

void usleep2(const unsigned long microsec)
{
    disablesignals();
    usleep(microsec);
    enablesignals();
}
#endif

int safe_write(const int fd, const void *buf_, size_t count)
{
    ssize_t written;    
    register const char *buf = (const char *) buf_;
    
    while (count > (size_t) 0U) {
        for (;;) {
            if ((written = write(fd, buf, count)) <= (ssize_t) 0) {
                if (errno == EAGAIN) {
                    sleep(1);
                } else if (errno != EINTR) {
                    return -1;
                }
                continue;
            }
            break;
        }
        buf += written;
        count -= written;
    }
    return 0;
}

#ifdef WITH_TLS
int secure_safe_write(const void *buf_, size_t count)
{
    ssize_t written;
    register const char *buf = (const char *) buf_;
    size_t ssw_status = count;
    
    while (count > (size_t) 0U) {
        for (;;) {
            if ((written = SSL_write(tls_data_cnx, buf, count)) <= (ssize_t) 0) {
                if (errno == EAGAIN) {
                    sleep(1);
                } else if (errno != EINTR) {
                    return -1;
                }
                continue;
            }
            break;
        }
        buf += written;
        count -= written;
    }
    return ssw_status;
}
#endif

static void overlapcpy(register char *d, register const char *s)
{
    while (*s != 0) {
        *d++ = *s++;
    }
    *d = 0;
}

static void safe_fd_set(const int fd, fd_set * const fds)
{
    if (fd == -1) {
        return;
    }
    FD_SET(fd, fds);    
}

static int safe_fd_isset(const int fd, const fd_set * const fds)
{
    if (fd == -1) {
        return 0;
    }
    return FD_ISSET(fd, fds);
}

static int init_tz(void)
{
    char stbuf[10];                                                             
    struct tm *tm;                                                              
    time_t now;                                                                 
    
#ifdef HAVE_TZSET
    tzset();
#endif
#ifdef HAVE_PUTENV    
    time(&now);                                                                 
    if ((tm = localtime(&now)) == NULL ||
        strftime(stbuf, sizeof stbuf, "%z", tm) != (size_t) 5U) {
        return -1;
    }
    snprintf(default_tz_for_putenv, sizeof default_tz_for_putenv,
             "TZ=UTC%c%c%c:%c%c", (*stbuf == '-' ? '+' : '-'),
             stbuf[1], stbuf[2], stbuf[3], stbuf[4]);
    putenv(default_tz_for_putenv);
#endif   
    return 0;
}

void simplify(char *subdir)
{
    char *a;
    
    if (subdir == NULL || *subdir == 0) {
        return;
    }
    while ((a = strstr(subdir, "//")) != NULL) {
        overlapcpy(a, a + 1);
    }
    while ((a = strstr(subdir, "/./")) != NULL) {
        overlapcpy(a, a + 2);
    }
    while (strncmp(subdir, "../", 3) == 0) {
        subdir += 3;
    }
    a = strstr(subdir, "/../");
    if (a != NULL) {
        if (a == subdir) {
            while (strncmp(subdir, "/../", 4) == 0) {
                overlapcpy(subdir, subdir + 3);
            }
            a = strstr(subdir, "/../");
        }
        while (a != NULL) {
            char *nextcomponent = a + 4;
            if (a != subdir && *a == '/') {
                a--;
            }
            while (a != subdir && *a != '/') {
                a--;
            }
            if (*a == '/') {
                a++;
            }
            overlapcpy(a, nextcomponent);
            a = strstr(subdir, "/../");
        }
    }
    a = subdir;
    if (*a == '.') {
        a++;
        if (*a == 0) {
            return;
        }
        if (*a == '/') {
            while (*a == '/') {
                a++;
            }
            overlapcpy(subdir, a);
        }
    }
    if (*a == 0) {
        return;
    }
    a = subdir + strlen(subdir) - (size_t) 1U;
    if (*a != '.' || a == subdir) {
        return;
    }
    a--;
    if (*a == '/' || a == subdir) {
        a[1] = 0;
        return;
    }
    if (*a != '.' || a == subdir) {
        return;
    }
    a--;
    if (*a != '/') {
        return;
    }
    *a = 0;
    if ((a = strrchr(subdir, '/')) == NULL) {
        *subdir = '/';
        subdir[1] = 0;
        return;
    }
    a[1] = 0;    
}

int checkprintable(register const char *s)
{
    register int ret = 0;    
    register unsigned char c;
    
    while ((c = (unsigned char) *s) != 0U) {
        if (ISCTRLCODE(c)) {
            ret--;
            break;
        }
        s++;
    }
    
    return ret;    
}

void die(const int err, const int priority, const char * const format, ...)
{
    va_list va;
    char line[MAX_SYSLOG_LINE];
    
#ifndef HAVE_SYS_FSUID_H
    disablesignals();
#endif
    va_start(va, format);
    vsnprintf(line, sizeof line, format, va);
    va_end(va);    
#ifdef WITH_TLS
    if (tls_cnx != NULL) {        
        char buf[MAX_SERVER_REPLY_LEN];
        snprintf(buf, sizeof buf, "%d %s\r\n", err, line);
        SSL_write(tls_cnx, buf, strlen(buf));
    } else
#endif
    {
        printf("%d %s\r\n", err, line);
        fflush(stdout);
    }
    logfile(priority, "%s", line);
    _EXIT(-priority - 1);
}

void die_mem(void)
{
    die(421, LOG_ERR, MSG_OUT_OF_MEMORY);
}

static RETSIGTYPE sigurg(int sig)
{
    int olderrno;
    int readen;
    unsigned char fodder;

    (void) sig;
    if (xferfd == -1) {
        return;
    }
    olderrno = errno;
    closedata();
#ifndef HAVE_SYS_FSUID_H
    disablesignals();
#endif
    addreply_noformat(426, MSG_ABORTED);
    doreply();
    do {
        if ((readen = read(0, &fodder, (size_t) 1U)) < (ssize_t) 0 && 
            errno == EINTR) {
            continue;
        }
    } while (readen > (ssize_t) 0 && fodder != '\n');
    addreply_noformat(226, MSG_ABORTED);
    doreply();
#ifndef HAVE_SYS_FSUID_H
    enablesignals();
#endif
    errno = olderrno;
}

static RETSIGTYPE sigalarm(int sig)
{
    (void) sig;
#ifndef HAVE_SYS_FSUID_H
    disablesignals();
#endif    
    die(421, LOG_INFO, MSG_TIMEOUT);
}

#ifndef NO_STANDALONE
static RETSIGTYPE sigchild(int sig)
{
    const int olderrno = errno;
    pid_t pid;
    
    (void) sig;
# ifdef HAVE_WAITPID
    while ((pid = waitpid((pid_t) -1, NULL, WNOHANG)) > (pid_t) 0) {
        if (nb_children > 0U) {
            nb_children--;
        }
#  ifdef FTPWHO
        ftpwho_unlinksbfile(pid);
#  endif
        iptrack_delete_pid(pid);
    }
# else
    while ((pid = wait3(NULL, WNOHANG, NULL)) > (pid_t) 0) {
        if (nb_children > 0U) {
            nb_children--;
        }
#  ifdef FTPWHO
        ftpwho_unlinksbfile(pid);
#  endif
        iptrack_delete_pid(pid);
    }
# endif
    errno = olderrno;
}
#endif

static RETSIGTYPE sigterm_client(int sig)
{
    (void) sig;
    
#ifndef HAVE_SYS_FSUID_H
    disablesignals();
#endif
    _EXIT(EXIT_SUCCESS);
}

#ifndef NO_STANDALONE
static RETSIGTYPE sigterm(int sig)
{
    const int olderrno = errno;
    (void) sig;
    
    stop_server = 1;
    if (listenfd != -1) {
        shutdown(listenfd, 2);
        (void) close(listenfd);
    }
    if (listenfd6 != -1) {
        shutdown(listenfd6, 2);
        (void) close(listenfd6);
    }
    errno = olderrno;
}

static void set_cloexec_flag(const int fd)
{
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}
#endif

static void clearargs(int argc, char **argv)
{
#ifndef NO_PROCNAME_CHANGE
# if defined(__linux__) && !defined(HAVE_SETPROCTITLE)
    int i;
    char *first = NULL;
    char *next = NULL;

    for (i = 0; i < argc; i++) {
        if (first == NULL) {
            first = argv[i];
        }
        if (next == NULL || argv[i] == next + 1) {
            next = argv[i] + strlen(argv[i]);
        }
    }
    for (i = 0; environ[i] != NULL; i++) {
        if (first == NULL) {
            first = argv[i];
        }
        if (next == NULL || argv[i] == next + 1) {
            next = argv[i] + strlen(argv[i]);
        }
    }
    if (first == NULL || next == NULL) {
        return;
    }
    argv_lth = next - first;
    argv0 = argv;
    if (environ != NULL) {
        char **new_environ;
        unsigned int env_nb = 0U;

        while (environ[env_nb] != NULL) {
            env_nb++;
        }
        if ((new_environ = malloc((1U + env_nb) * sizeof (char *))) == NULL) {
            abort();
        }
        new_environ[env_nb] = NULL;
        while (env_nb > 0U) {
            env_nb--;
            new_environ[env_nb] = strdup(environ[env_nb]);
        }
        environ = new_environ;
    }
# else
    (void) argc;
    (void) argv;
# endif
#endif
}

void setprocessname(const char * const title)
{
#ifndef NO_PROCNAME_CHANGE
# ifdef HAVE_SETPROCTITLE
    setproctitle("-%s", title);
# elif defined(__linux__)    
    if (argv0 != NULL && argv_lth > strlen(title) - 2) {
        memset(argv0[0], 0, argv_lth);
        strncpy(argv0[0], title, argv_lth - 2);
        argv0[1] = NULL;
    }
# elif defined(__hpux__)
    union pstun pst;
    
    pst.pst_command = title;
    pstat(PSTAT_SETCMD, pst, strlen(title), 0, 0);
# endif
#endif
    (void) title;
}

/* Check whether an address is valid, return 1 if ok, 0 otherwise.
 * Unfortunately, multicasting with the FTP protocol is impossible,
 * you have to use things like MTP instead. So prohibit multicast.
 */

static int checkvalidaddr(const struct sockaddr_storage * const addr)
{
    if (addr == NULL) {
        return 0;
    }
    /* Some versions of MacOS X have broken IN* macros */
#ifdef __APPLE_CC__
    return 1;
#endif
    if (STORAGE_FAMILY(*addr) == AF_INET6) {
        if (IN6_IS_ADDR_MULTICAST(&STORAGE_SIN_ADDR6_NF(*addr)) ||
            IN6_IS_ADDR_UNSPECIFIED(&STORAGE_SIN_ADDR6_NF(*addr))) {
            return 0;
        }
        return 1;
    } else if (STORAGE_FAMILY(*addr) == AF_INET) {
        if (ntohl(STORAGE_SIN_ADDR(*addr)) == INADDR_ANY ||
            ntohl(STORAGE_SIN_ADDR(*addr)) == INADDR_NONE ||
            ntohl(STORAGE_SIN_ADDR(*addr)) == INADDR_BROADCAST ||
            IN_MULTICAST(ntohl(STORAGE_SIN_ADDR(*addr))) ||
            IN_BADCLASS(ntohl(STORAGE_SIN_ADDR(*addr)))) {
            return 0;
        }
        return 1;
    }
    return 0;
}

/* Convert a 4-in-6 address to pure IPv4 */

static void fourinsix(struct sockaddr_storage *v6)
{
    struct sockaddr_storage v4;
    
    if (v6ready == 0 || STORAGE_FAMILY(*v6) != AF_INET6 ||
        IN6_IS_ADDR_V4MAPPED(&STORAGE_SIN_ADDR6_NF(*v6)) == 0) {
        return;
    }
    memset(&v4, 0, sizeof v4);
    STORAGE_FAMILY(v4) = AF_INET;
    memcpy(&STORAGE_SIN_ADDR(v4),
           (unsigned char *) &STORAGE_SIN_ADDR6(*v6) + 12,
           sizeof STORAGE_SIN_ADDR(v4));
    STORAGE_PORT(v4) = STORAGE_PORT6(*v6);
    SET_STORAGE_LEN(v4, sizeof(struct sockaddr_in));
    *v6 = v4;
}

/* Return 0 if s1 == s2 , 1 if s1 != s2 , -1 if error */

static int addrcmp(const struct sockaddr_storage * const s1,
                   const struct sockaddr_storage * const s2)
{
    if (STORAGE_FAMILY(*s1) == AF_INET6) {
        if (STORAGE_FAMILY(*s2) != AF_INET6) {
            return 1;
        }
        if (IN6_ARE_ADDR_EQUAL(&STORAGE_SIN_ADDR6_NF(*s1), &STORAGE_SIN_ADDR6_NF(*s2))) {
            return 0;
        } else {
            return 1;
        }
    } else if (STORAGE_FAMILY(*s1) == AF_INET) {
        if (STORAGE_FAMILY(*s2) != AF_INET) {
            return 1;
        }
        if (STORAGE_SIN_ADDR(*s1) == STORAGE_SIN_ADDR(*s2)) {
            return 0;
        } else {
            return 1;
        }
    }
    return -1;
}

static int generic_aton(const char *src, struct sockaddr_storage *a)
{
    if (inet_pton(AF_INET6, src, &STORAGE_SIN_ADDR6(*a)) > 0) {
        STORAGE_FAMILY(*a) = AF_INET6;
        return 0;
    }
    if (inet_pton(AF_INET, src, &STORAGE_SIN_ADDR(*a)) > 0) {
        STORAGE_FAMILY(*a) = AF_INET;
        return 0;
    }
    memset(a, 0, sizeof *a);
    
    return -1;
}

void logfile(const int crit, const char *format, ...)
{
#ifdef NON_ROOT_FTP
    (void) crit;
    (void) format;
#else
    const char *urgency;    
    va_list va;
    char line[MAX_SYSLOG_LINE];
    
    if (no_syslog != 0) {
        return;
    }
    va_start(va, format);
    vsnprintf(line, sizeof line, format, va);
    va_end(va);    
    switch (crit) {
    case LOG_INFO:
        urgency = "[INFO] ";
        break;
    case LOG_WARNING:
        urgency = "[WARNING] ";
        break;
    case LOG_ERR:
        urgency = "[ERROR] ";
        break;
    case LOG_NOTICE:
        urgency = "[NOTICE] ";
        break;
    case LOG_DEBUG:
        urgency = "[DEBUG] ";
        break;
    default:
        urgency = "";
    }
# ifdef SAVE_DESCRIPTORS
    openlog("pure-ftpd", log_pid, syslog_facility);
# endif
    syslog(crit, "(%s@%s) %s%s",
           ((loggedin != 0 && *account != 0) ? account : "?"),
           (*host != 0 ? host : "?"),
           urgency, line);
# ifdef SAVE_DESCRIPTORS
    closelog();
# endif
#endif
}

#ifndef NO_STANDALONE

/* this is taken from the code examples for Stevens' "Advanced
 * Programming in the Unix Environment. The code is publicly available
 * at ftp://ftp.uu.net/published/books/stevens.advprog.tar.Z */

static unsigned int open_max(void)
{
    long z;
    
    if ((z = (long) sysconf(_SC_OPEN_MAX)) < 0L) {
        perror("_SC_OPEN_MAX");
        _EXIT(EXIT_FAILURE);
    }
    return (unsigned int) z;
}

#endif

#ifdef WITH_RFC2640
char *charset_fs2client(const char *string)
{
    char *output = NULL, *output_;
    size_t inlen, outlen, outlen_;
    
    inlen = strlen(string);
    outlen_ = outlen = inlen * (size_t) 4U + (size_t) 1U;
    if (outlen <= inlen ||
        (output_ = output = calloc(outlen, (size_t) 1U)) == NULL) {
        die_mem();
    }
    if (utf8 > 0 && strcasecmp(charset_fs, "utf-8") != 0) {
        if (iconv(iconv_fd_fs2utf8, (char **) &string, &inlen,
                  &output_, &outlen_) == (size_t) -1) {
            strncpy(output, string, strlen(string));
        }
    } else if (utf8 <= 0 && strcasecmp(charset_client, charset_fs) != 0) {
        if (iconv(iconv_fd_fs2client,
                  (char **) &string, &inlen,
                  &output_, &outlen_) == (size_t) -1) {
            strncpy(output, string, strlen(string));
        }
    } else {
        strncpy(output, string, outlen);
    }
    output[outlen - 1U] = 0;
    
    return output;
}
#endif

static void addreply_newline(const char * const str, const size_t size)
{
    struct reply *newline;
    
    if ((newline = (struct reply *) malloc(offsetof(struct reply, line) +
                                           size)) == NULL) {
        die_mem();
    }
    if (firstreply == NULL) {
        firstreply = newline;
    } else {
        lastreply->next = newline;
    }
    newline->next = NULL;
    lastreply = newline;
    memcpy(newline->line, str, size);    
}

void addreply_noformat(const int code, const char * const line)
{
    if (code != 0) {
        replycode = code;
    }
    addreply_newline(line, strlen(line) + (size_t) 1U);
}

void addreply(const int code, const char * const line, ...)
{
    register char *a;
    register char *b;
    va_list ap;
    int last;
    char buf[MAX_SERVER_REPLY_LEN];
    
    if (code != 0) {
        replycode = code;
    }
    va_start(ap, line);
    vsnprintf(buf, sizeof buf, line, ap);
    va_end(ap);
    last = 0;
    a = buf;
    for (;;) {
        b = strchr(a, '\n');
        if (b != NULL) {
            *b = 0;
        } else {
            b = a;
            while (*b != 0) {
                b++;
            }
            last++;
        }
        addreply_newline(a, (size_t) (b - a) + (size_t) 1U);
        if (last != 0) {
            break;
        }
        a = b + 1;
    }
}

void doreply(void)
{
    register struct reply *scannedentry;
    register struct reply *nextentry;
    
    if ((scannedentry = firstreply) == NULL) {
        return;
    }
    CORK_ON(1);
    do {
        nextentry = scannedentry->next;
#ifdef WITH_TLS
        if (tls_cnx != NULL) {
            char buf[MAX_SERVER_REPLY_LEN];
            
            snprintf(buf, sizeof buf, "%3d%c%s\r\n", replycode, 
                     nextentry == NULL ? ' ' : '-', scannedentry->line);
            SSL_write(tls_cnx, buf, strlen(buf));            
        } else
#endif
        {
            printf("%3d%c%s\r\n", replycode, nextentry == NULL ? ' ' : '-',
                   scannedentry->line);
        }
        if (logging > 1) {
            logfile(LOG_DEBUG, "%3d%c%s", replycode, 
                    nextentry == NULL ? ' ' : '-', scannedentry->line);
        }       
    } while ((scannedentry = nextentry) != NULL);
    fflush(stdout);    
    CORK_OFF(1);
    /* We don't free() after printf() because of Solaris stream bugs,
     * Thanks to Kenneth Stailey */
    scannedentry = firstreply;
    do {
        nextentry = scannedentry->next;
        free(scannedentry);
    } while ((scannedentry = nextentry) != NULL);
    firstreply = lastreply = NULL;
}

/* Check whether a file name is valid. Files names starting
 * with a dot are only allowed to root and to users
 * chroot()ed in their home directories -Jedi. */

static int checknamesanity(const char *name, int dot_ok)
{
    register const char *namepnt;
    
#ifdef PARANOID_FILE_NAMES
    const char *validchars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefgihjklmnopqrstuvwxyz"
        "0123456789./-_";
#endif
    
    if (name == NULL || *name == 0) {
        return -1;
    }
    /* optimize . and .. */
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) {
        return 0;
    }
    namepnt = name;
#ifdef PARANOID_FILE_NAMES
    /* we want to make sure we don't get any non-alphanumeric file name */
    if (strlen(namepnt) != strspn(namepnt, validchars)) {
        return -1;
    }
#endif
#ifdef QUOTAS
    if (hasquota() == 0 && strstr(namepnt, QUOTA_FILE) != NULL) {
        return -1;                     /* .ftpquota => *NO* */
    }
#endif
#ifndef ALLOW_DELETION_OF_TEMPORARY_FILES    
    if (strstr(namepnt, PUREFTPD_TMPFILE_PREFIX) == namepnt) {
        return -1;
    }
#endif
    while (*namepnt != 0) {
        if (ISCTRLCODE(*namepnt) || *namepnt == '\\') {
            return -1;
        }
        if (dot_ok == 0) {
            if (*namepnt == '/') {
                namepnt++;
            } else if (namepnt != name) {
                namepnt++;
                continue;
            }
            if (namepnt[0] == 0) {     /* /$ */
                return 0;
            }
            if (namepnt[0] == '.') {   /* /. */
                if (namepnt[1] == 0) { /* /.$ => ok */
                    return 0;
                }
                if (namepnt[1] == '.') {   /* /.. */
                    if (namepnt[2] == 0) {   /* /..$ => ok */
                        return 0;
                    }
                    if (namepnt[2] != '/') {   /* /..[^/] => *NO* */
                        return -1;
                    }
                } else if (namepnt[1] != '/') {   /* /.[^/]/ => *NO* */
                    return -1;
                }
            }
            if (namepnt != name) {
                continue;
            }
        }
        namepnt++;
    }
    return 0;
}

static void do_ipv6_port(char *p, char delim)
{
    char *deb;    
    struct sockaddr_storage a;
    
    deb = p;
    while (*p && strchr("0123456789abcdefABCDEF:", *p) != NULL) {
        p++;
    }
    if (*p != delim || atoi(p + 1) == 0) {
        nope:
        (void) close(datafd);
        datafd = -1;
        addreply_noformat(501, MSG_SYNTAX_ERROR_IP);
        return;
    }
    *p++ = 0;
    if (generic_aton(deb, &a) != 0) {
        goto nope;
    }
    doport2(a, (unsigned int) atoi(p));
}

#ifndef MINIMAL
void doesta(void)
{
    struct sockaddr_storage dataconn;    
    socklen_t socksize;    
    char hbuf[NI_MAXHOST];
    char pbuf[NI_MAXSERV];
    
    if (passive != 0 || datafd == -1) {
        addreply_noformat(520, MSG_ACTIVE_DISABLED);
        return;
    }
    if (xferfd == -1) {
        opendata();
        if (xferfd == -1) {
            addreply_noformat(425, MSG_CANT_CREATE_DATA_SOCKET);
            return;
        }
    }
    socksize = (socklen_t) sizeof dataconn;
    if (getsockname(xferfd, (struct sockaddr *) &dataconn, &socksize) < 0 ||
        getnameinfo((struct sockaddr *) &dataconn, STORAGE_LEN(dataconn),
                    hbuf, sizeof hbuf, pbuf, sizeof pbuf,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        addreply_noformat(425, MSG_GETSOCKNAME_DATA);
        closedata();
        return;
    }
    addreply(225, "Connected from (|%c|%s|%s|)",
             STORAGE_FAMILY(dataconn) == AF_INET6 ? '2' : '1', hbuf, pbuf);
}

void doestp(void)
{
    struct sockaddr_storage dataconn;
    socklen_t socksize;        
    char hbuf[NI_MAXHOST];
    char pbuf[NI_MAXSERV];
    
    if (passive == 0 || datafd == -1) {
        addreply_noformat(520, MSG_CANT_PASSIVE);
        return;
    }
    if (xferfd == -1) {
        opendata();
        if (xferfd == -1) {
            addreply_noformat(425, MSG_CANT_CREATE_DATA_SOCKET);
            return;
        }
    }
    socksize = (socklen_t) sizeof dataconn;
    if (getpeername(xferfd, (struct sockaddr *) &dataconn, &socksize) < 0 ||
        getnameinfo((struct sockaddr *) &dataconn, STORAGE_LEN(dataconn),
                    hbuf, sizeof hbuf, pbuf, sizeof pbuf,
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        addreply_noformat(425, MSG_GETSOCKNAME_DATA);
        closedata();
        return;
    }
    addreply(225, "Connected to (|%c|%s|%s|)",
             STORAGE_FAMILY(dataconn) == AF_INET6 ? '2' : '1', hbuf, pbuf);
}
#endif

void doeprt(char *p)
{
    char delim;
    int family;
    
    delim = *p++;
    family = atoi(p);
    while (isdigit((unsigned char) *p)) {
        p++;
    }
    if (*p == delim) {
        p++;
    } else {
        addreply_noformat(501, MSG_SYNTAX_ERROR_IP);
        return;
    }
    if (family == 2 && v6ready) {
        do_ipv6_port(p, delim);
        return;
    }
    if (family != 1) {
        if (v6ready) {
            addreply_noformat(522, MSG_ONLY_IPV4V6);
        } else {
            addreply_noformat(522, MSG_ONLY_IPV4);
        }
        return;
    }
    
    {
        unsigned int a1, a2, a3, a4, port = 0U;
        /* there should be dot-decimal ip as rfc2428 states,
         * but troll used for some reason "comma-decimal" notation
         * so I decided to leave it */
        if ((sscanf(p, "%u,%u,%u,%u", &a1, &a2, &a3, &a4) != 4 &&
             sscanf(p, "%u.%u.%u.%u", &a1, &a2, &a3, &a4) != 4) ||
            a1 > 255U || a2 > 255U || a3 > 255U || a4 > 255U ||
            (a1 | a2 | a3 | a4) == 0U) {
            addreply_noformat(501, MSG_SYNTAX_ERROR_IP);
            return;
        }
        while (*p && strchr("0123456789.,", *p)) {
            p++;
        }
        if (*p == delim) {
            port = (unsigned int) atoi(++p);
            while (*p && isdigit((unsigned char) *p)) {
                p++;
            }
        }
        if (*p != delim || port > 65535U || port <= 0U) {
            addreply_noformat(501, MSG_SYNTAX_ERROR_IP);
            return;
        } else {
            struct sockaddr_storage a;
            
            memset(&a, 0, sizeof a);
            STORAGE_FAMILY(a) = AF_INET;
            STORAGE_SIN_ADDR(a) =
                htonl((a1 << 24) | (a2 << 16) | (a3 << 8) | a4);
            SET_STORAGE_LEN(a, sizeof(struct sockaddr_in));
            doport2(a, port);
        }
    }
}

void stripctrl(char * const buf, size_t len)
{
    if (len <= (size_t) 0U) {
        return;
    }
    do {
        len--;
        if (ISCTRLCODE(buf[len]) &&
            buf[len] != 0 && buf[len] != '\n') {
            buf[len] = '_';
        }
    } while (len != (size_t) 0U);
}

#ifndef MINIMAL

/*
 * small help routine to display a banner
 * type = 0 reads .banner/welcome.msg
 * type = 1 reads .message (after cd'ing into a directory)
 */
void dobanner(const int type)
{
    char buffer[512];    
    FILE *msg;
    size_t buflen;
    unsigned int nblines = BANNER_MAXLINES;    
    
    switch (type) {
    case 0:
        if ((msg = fopen(".banner", "r")) == NULL
# ifdef WITH_WELCOME_MSG
            && (msg = fopen("welcome.msg", "r")) == NULL
# endif
            ) {
            return;
        }
        break;
    case 1:
        if ((msg = fopen(".message", "r")) == NULL) {
            return;
        }
        break;
    default:
        return;
    }   
    
    while (fgets(buffer, sizeof buffer, msg) != NULL && nblines > 0U) {
        nblines--;      
        if ((buflen = strlen(buffer)) > (size_t) 0U) {
            buflen--;
            while (buffer[buflen] == '\n' || buffer[buflen] == '\r') {
                buffer[buflen] = 0;
                if (buflen == (size_t) 0U) {
                    break;
                }
                buflen--;
            }
            stripctrl(buffer, buflen);
        }
        addreply_noformat(0, buffer);
    }
    (void) fclose(msg);
}

#endif

#ifndef MINIMAL

int modernformat(const char *file, char *target, size_t target_size,
                 const char * const prefix)
{
    const char *ft;
    struct tm *t;
    struct stat st;
    int ret = 0;
    
    if (stat(file, &st) != 0 ||
        !(t = gmtime((time_t *) &st.st_mtime))) {
        return -1;
    }
    if (S_ISREG(st.st_mode)) {
        ft = "file";
    } else if (S_ISDIR(st.st_mode)) {
        ret = 1;
        ft = "dir";
        if (*file == '.') {
            if (file[1] == '.' && file[2] == 0) {
                ft = "pdir";
            } else if (file[1] == 0) {
                ft = "cdir";
            }
        } else if (*file == '/' && file[1] == 0) {
            ft = "pdir";
        }
    } else {
        ft = "unknown";
    }
    if (guest != 0) {
        if (SNCHECK(snprintf(target, target_size,
                             "%stype=%s;siz%c=%llu;modify=%04d%02d%02d%02d%02d%02d;UNIX.mode=0%o;unique=%xg%llx; %s",
                             prefix,
                             ft,
                             ret ? 'd' : 'e',
                             (unsigned long long) st.st_size,
                             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                             t->tm_hour, t->tm_min, t->tm_sec,
                             (unsigned int) st.st_mode & 07777,
                             (unsigned int) st.st_dev,
                             (unsigned long long) st.st_ino,
                             file), target_size)) {
            _EXIT(EXIT_FAILURE);
        }
    } else {
        if (SNCHECK(snprintf(target, target_size,
                             "%stype=%s;siz%c=%llu;modify=%04d%02d%02d%02d%02d%02d;UNIX.mode=0%o;UNIX.uid=%lld;UNIX.gid=%lld;unique=%xg%llx; %s",
                             prefix,
                             ft,
                             ret ? 'd' : 'e',
                             (unsigned long long) st.st_size,
                             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                             t->tm_hour, t->tm_min, t->tm_sec,
                             (unsigned int) st.st_mode & 07777,
                             (unsigned long long) st.st_uid,
                             (unsigned long long) st.st_gid,
                             (unsigned int) st.st_dev,
                             (unsigned long long) st.st_ino,
                             file), target_size)) {
            _EXIT(EXIT_FAILURE);
        }
    }
    return ret;
}

#define MLST_BEGIN "Begin" CRLF

void domlst(const char * const file)
{
    char line[MAXPATHLEN + 256U] = MLST_BEGIN;
    
    if (modernformat(file, line + (sizeof MLST_BEGIN - 1U),
                     sizeof line - (sizeof MLST_BEGIN - 1U), " ") >= 0) {
        addreply_noformat(0, line);
        addreply_noformat(250, "End.");
    } else {
        addreply_noformat(550, MSG_STAT_FAILURE2);
    }
}

void donoop(void)
{
#ifdef BORING_MODE
    addreply_noformat(200, "dc.w $4E71");
#else
    addreply_noformat(200, MSG_SLEEPING);
#endif
}

#endif

void dositetime(void)
{
    char tmp[64];
    const struct tm *tm;
    time_t now;
    
    if ((now = time(NULL)) == (time_t) -1 || (tm = localtime(&now)) == NULL) {
        addreply_noformat(550, "time()");
        return;
    }
    strftime(tmp, sizeof tmp, "%Y-%m-%d %H:%M:%S", tm);
    addreply_noformat(211, tmp);
}

static int doinitsupgroups(const char *user, const uid_t uid, const gid_t gid)
{
#ifndef NON_ROOT_FTP
# ifdef HAVE_SETGROUPS
    if (setgroups(1U, &gid) != 0) {
        return -1;
    }
# else
    (void) gid;
# endif
# ifdef HAVE_INITGROUPS            
    if (user == NULL) {
        const struct passwd * const lpwd = getpwuid(uid);
        
        if (lpwd != NULL && lpwd->pw_name != NULL) {
            user = lpwd->pw_name;
        } else {        
            return 0;
        }
    }
    initgroups(user, gid);
# else
    (void) user;
    (void) uid;
# endif
#else
    (void) user;
    (void) uid;
    (void) gid;    
#endif
    return 0;
}

void douser(const char *username)
{
    struct passwd *pw;
    
    if (loggedin) {
        if (username) {
            if (!guest) {
                addreply_noformat(530, MSG_ALREADY_LOGGED);
            } else if (broken_client_compat != 0) {
                addreply_noformat(331, MSG_ANY_PASSWORD);
            } else {
                addreply_noformat(230, MSG_ANONYMOUS_LOGGED);
                dot_read_ok = dot_read_anon_ok;
                dot_write_ok = 0;
            }
        }
        return;
    }
    if (anon_only <= 0 && username != NULL && *username != 0 &&
        (anon_only < 0 || (strcasecmp(username, "ftp") &&
                           strcasecmp(username, "anonymous")))) {
        strncpy(account, username, sizeof(account) - 1);
        account[sizeof(account) - (size_t) 1U] = 0;
        addreply(331, MSG_USER_OK, account);
        loggedin = 0;
    } else if (anon_only < 0) {
        if (broken_client_compat != 0) {
            addreply(331, MSG_USER_OK, username);
            return;
        } else {
            die(530, LOG_DEBUG, MSG_NO_ANONYMOUS_LOGIN);
        }
    } else {
#ifdef WITH_VIRTUAL_HOSTS
        char name[MAXPATHLEN];
        char hbuf[NI_MAXHOST];
#endif
        if (chrooted != 0) {
            die(421, LOG_DEBUG, MSG_CANT_DO_TWICE);
        }
        
#ifdef PER_USER_LIMITS
        if (per_anon_max > 0U && ftpwho_read_count("ftp") >= per_anon_max) {
            addreply(421, MSG_PERUSER_MAX, (unsigned long) per_anon_max);
            doreply();
            _EXIT(1);
        }
#endif  
        
#ifdef NON_ROOT_FTP
        {
            static struct passwd pw_;
            char s[MAXPATHLEN + 1U];            
            
            if (getcwd(s, sizeof s - (size_t) 1U) == NULL) {
                cantsec:
                die(421, LOG_ERR, MSG_UNABLE_SECURE_ANON);
            }
            pw_.pw_uid = geteuid();
            pw_.pw_gid = getegid();
# if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
            if ((pw_.pw_dir = getenv("WIN32_ANON_DIR")) == NULL) {
                pw_.pw_dir = WIN32_ANON_DIR;
            }
# else
            pw_.pw_dir = strdup(s);    /* checked for == NULL later */
# endif
            pw = &pw_;
        }
#else
        if ((pw = getpwnam("ftp")) == NULL ||
            pw->pw_uid == 0 || pw->pw_gid == 0 ||
            doinitsupgroups("ftp", (uid_t) -1, pw->pw_gid) != 0 ||
            setgid(pw->pw_gid) || setegid(pw->pw_gid)) {
            cantsec:
            die(421, LOG_ERR, MSG_UNABLE_SECURE_ANON);
        }
#endif
#ifdef WITH_VIRTUAL_HOSTS
        if (getnameinfo((struct sockaddr *) &ctrlconn, STORAGE_LEN(ctrlconn),
                        hbuf, sizeof hbuf, NULL,
                        (size_t) 0U, NI_NUMERICHOST) != 0
            || SNCHECK(snprintf(name, sizeof name, VHOST_PATH "/%s", hbuf),
                       sizeof name)) {
            _EXIT(EXIT_FAILURE);
        }
        if (chdir(name) != 0)         /* non-virtual */
#endif
        {
            char *hd;
            size_t rd_len;
            
            if (pw->pw_dir == NULL || *pw->pw_dir != '/') {
                goto cantsec;
            }
            if ((hd = strstr(pw->pw_dir, "/./")) != NULL) {
                rd_len = (size_t) (hd - pw->pw_dir) + sizeof "/";
                if ((root_directory = malloc(rd_len)) == NULL) {
                    goto cantsec;
                }
                memcpy(root_directory, pw->pw_dir, rd_len);
                root_directory[rd_len - (size_t) 1U] = 0;
                hd += 2;
            } else {
                rd_len = strlen(pw->pw_dir) + sizeof "/";
                if ((root_directory = malloc(rd_len)) == NULL) {
                    goto cantsec;
                }
                snprintf(root_directory, rd_len, "%s/", pw->pw_dir);
                hd = (char *) "/";
            }
            if (chdir(root_directory) || chroot(root_directory) || chdir(hd)) {
                goto cantsec;
            }
            logfile(LOG_INFO, MSG_ANONYMOUS_LOGGED);
        }
#ifdef WITH_VIRTUAL_HOSTS
        else {                       /* virtual host */
            const size_t rd_len = strlen(hbuf) + sizeof ":/";
            
            if ((root_directory = malloc(rd_len)) == NULL ||
                chdir(name) || chroot(name) || chdir("/") ||
                SNCHECK(snprintf(root_directory, rd_len, "%s:/", hbuf),
                        rd_len)) {
                goto cantsec;
            }
            logfile(LOG_INFO, MSG_ANONYMOUS_LOGGED_VIRTUAL ": %s", hbuf);
        }
#endif
        chrooted = 1;
        authresult.uid = pw->pw_uid;
        authresult.gid = pw->pw_gid;
        if ((authresult.dir = strdup(pw->pw_dir)) == NULL) {
            die_mem();
        }
        
#ifdef THROTTLING
        if (throttling != 0) {
            addreply_noformat(0, MSG_BANDWIDTH_RESTRICTED);
            (void) nice(NICE_VALUE);
        } else {
            throttling_delay = throttling_bandwidth_ul =
                throttling_bandwidth_dl = 0UL;
        }
#endif

#ifndef NON_ROOT_FTP
        if (authresult.uid > (uid_t) 0) {
# ifdef WITH_PRIVSEP
            if (setuid(authresult.uid) != 0 || seteuid(authresult.uid) != 0) {
                goto cantsec;
            }
# else
#  ifdef HAVE_SYS_FSUID_H
            setfsgid(authresult.gid);
            setfsuid(authresult.uid);
#  else
            if (seteuid(authresult.uid) != 0) {
                goto cantsec;
            }
#  endif
# endif
        }
#endif
        
#ifdef USE_CAPABILITIES
        drop_login_caps();
#endif
        
#ifndef MINIMAL
        dobanner(0);
#endif
        
        if (broken_client_compat != 0) {
            addreply_noformat(331, MSG_ANONYMOUS_ANY_PASSWORD);
        } else {
            addreply_noformat(230, MSG_ANONYMOUS_LOGGED);
        }
        dot_write_ok = 0;
        dot_read_ok = dot_read_anon_ok;
        strncpy(account, "ftp", sizeof account - (size_t) 1U);
        account[(sizeof account) - 1U] = 0;
#ifdef FTPWHO
        if (shm_data_cur != NULL) {
            ftpwho_lock();
            strncpy(shm_data_cur->account, account,
                    sizeof shm_data_cur->account - (size_t) 1U);
            shm_data_cur->account[sizeof shm_data_cur->account - 1U] = 0;
            ftpwho_unlock();
            state_needs_update = 1;
        }
#endif
        loggedin = guest = 1;
#ifdef QUOTAS
        user_quota_size = user_quota_files = ULONG_LONG_MAX;
#endif
    }
    if (getcwd(wd, sizeof wd - (size_t) 1U) == NULL) {
        wd[0] = '/';
        wd[1] = 0;
    }
#ifdef WITH_BONJOUR
    refreshManager();
#endif
}

static AuthResult pw_check(const char *account, const char *password,
                           const struct sockaddr_storage * const sa,
                           const struct sockaddr_storage * const peer)
{
    Authentications *auth_scan = first_authentications;
    AuthResult result;
    
    result.auth_ok = -1;
    while (auth_scan != NULL) {
#ifdef THROTTLING
        result.throttling_bandwidth_ul = throttling_bandwidth_ul;
        result.throttling_bandwidth_dl = throttling_bandwidth_dl;
        result.throttling_ul_changed = result.throttling_dl_changed = 0;
#endif
#ifdef QUOTAS
        result.user_quota_size = user_quota_size;
        result.user_quota_files = user_quota_files;
        result.quota_size_changed = result.quota_files_changed = 0;
#endif
#ifdef RATIOS
        result.ratio_upload = ratio_upload;
        result.ratio_download = ratio_download;
        result.ratio_ul_changed = result.ratio_dl_changed = 0;
#endif
#ifdef PER_USER_LIMITS
        result.per_user_max = per_user_max;
#endif  
        auth_scan->auth->check(&result, account, password, sa, peer);
        if (result.auth_ok < 0) {
            break;
        } else if (result.auth_ok > 0) {
#ifdef THROTTLING
            if ((result.throttling_ul_changed |
                 result.throttling_dl_changed) != 0) {
                if (result.throttling_ul_changed != 0 &&
                    result.throttling_bandwidth_ul > 0UL) {
                    throttling_bandwidth_ul = result.throttling_bandwidth_ul;
                    ul_chunk_size = throttling_bandwidth_ul;
                    if (ul_chunk_size > MAX_UL_CHUNK_SIZE) {
                        ul_chunk_size = MAX_UL_CHUNK_SIZE;
                    }
                }
                if (result.throttling_dl_changed != 0 &&
                    result.throttling_bandwidth_dl > 0UL) {
                    throttling_bandwidth_dl = result.throttling_bandwidth_dl;
                    dl_chunk_size = throttling_bandwidth_dl & ~(page_size - 1);
                    if (dl_chunk_size < page_size) {
                        dl_chunk_size = page_size;
                    }
                }
                throttling_delay = 1000000 /
                    (throttling_bandwidth_dl | throttling_bandwidth_ul);
                throttling = 2;
            }
#endif
#ifdef QUOTAS
            if (result.quota_size_changed != 0) {
                user_quota_size = result.user_quota_size;
            }
            if (result.quota_files_changed != 0) {
                user_quota_files = result.user_quota_files;
            }
#endif
#ifdef RATIOS
            if (result.ratio_ul_changed != 0) {
                ratio_upload = result.ratio_upload;
                ratio_for_non_anon = 1;
            }
            if (result.ratio_dl_changed != 0) {
                ratio_download = result.ratio_download;
            }
#endif
#ifdef PER_USER_LIMITS
            per_user_max = result.per_user_max;
#endif
            
#ifdef NON_ROOT_FTP
            result.uid = geteuid();
            result.gid = getegid();
#endif      
            
            return result;
        }
        auth_scan = auth_scan->next;
    }
    
    return result;
}

/*
 * Check if an user belongs to the trusted group, either in his
 * primary group, or his supplementary groups. Root is always trusted.
 */

static int check_trustedgroup(const uid_t uid, const gid_t gid)
{
    GETGROUPS_T *alloca_suppgroups;
    int n;
    int n2;
    int result = 0;
    
    if (uid == (uid_t) 0) {
        return 1;
    }
    if (userchroot == 2) {
        return 0;
    }
    if (gid == chroot_trustedgid) {
        return 1;
    }
#ifdef HAVE_GETGROUPS
    if ((n = getgroups(0, NULL)) <= 0) {
        return 0;
    }
    if ((alloca_suppgroups =
         ALLOCA(n * (sizeof *alloca_suppgroups))) == NULL) {
        die_mem();
    }
    n2 = getgroups(n, alloca_suppgroups);
    /* Jedi's paranoia */
    if (n2 < n) {
        n = n2;
    }
    result = 0;
    while (n != 0) {
        n--;
        if (alloca_suppgroups[n] == (GETGROUPS_T) chroot_trustedgid) {
            result = 1;
            break;
        }
    };
    ALLOCA_FREE(alloca_suppgroups);
#endif
    
    return result;
}

/*
 * Create a home directory on demand.
 */

static int create_home_and_chdir(const char * const home)
{
    char *pathcomp;
    char *z;    
    size_t len;
    const char delim = '/';        
    
    if (home == NULL || *home != '/') {
        return -1;
    }
    if (chdir(home) == 0) {
        return 0;
    }
    if (create_home == 0) {
        return -1;
    }
    len = strlen(home) + (size_t) 1U;
    if (len < (size_t) 2U || *home != delim) {
        return -1;
    }
    if ((pathcomp = ALLOCA(len)) == NULL) {
        return -1;
    }
    memcpy(pathcomp, home, len);       /* safe, no possible overflow */
    z = pathcomp;
    for (;;) {
        z++;        
        if (*z == 0) {
            break;
        }
        if (*z == delim) {
            *z = 0;            
            if (z[1] == 0) {
                break;
            }
            (void) mkdir(pathcomp, (mode_t) 0755);
            *z = delim;
        }
    }
    ALLOCA_FREE(pathcomp);
    (void) mkdir(home, (mode_t) 0700);
    if (chdir(home) != 0) {
        return -1;
    }
    if (chmod(home, (mode_t) 0777 & ~u_mask_d) < 0 ||
        chown(home, authresult.uid, authresult.gid) < 0) {
        return -1;
    }
    
    return chdir(home);
}

static void randomsleep(unsigned int t) {
    usleep2((unsigned long) (zrand() % PASSWD_FAILURE_DELAY));        
    usleep2(t * PASSWD_FAILURE_DELAY);
}

void dopass(char *password)
{
    static unsigned int tapping;    
    gid_t *groups = NULL;
    char *hd;
    int ngroups;
#if defined(NGROUPS_MAX) && NGROUPS_MAX > 0
    int ngroups_max = NGROUPS_MAX; /* Use the compile time value */
#else
    int ngroups_max = 1; /* use a sane default */
#endif
#ifdef WITH_RFC2640
    char *nwd = NULL;
#endif
    
    if (loggedin != 0) {
        if (guest != 0) {
            addreply_noformat(230, MSG_NO_PASSWORD_NEEDED);
#ifdef LOG_ANON_EMAIL
            snprintf(account, sizeof account, "ftp: <%s> ", password);
#endif
        } else {
            addreply_noformat(530, MSG_CANT_DO_TWICE);
        }
        return;
    }
    if (*account == 0) {
        addreply_noformat(530, MSG_WHOAREYOU);
        return;
    }
    authresult = pw_check(account, password, &ctrlconn, &peer);
    {
        /* Clear password from memory, paranoia */        
        register volatile char *password_ = (volatile char *) password;
        
        while (*password_ != 0) {
            *password_++ = 0;
        }
    }
    if (authresult.auth_ok != 1) {
        tapping++;
        randomsleep(tapping);
        addreply_noformat(530, MSG_AUTH_FAILED);
        doreply();
        if (tapping > MAX_PASSWD_TRIES) {
            logfile(LOG_ERR, MSG_AUTH_TOOMANY);
            _EXIT(EXIT_FAILURE);
        }
        logfile(LOG_WARNING, MSG_AUTH_FAILED_LOG, account);
        return;
    }
    if (authresult.uid < useruid) {
        logfile(LOG_WARNING, MSG_ACCOUNT_DISABLED, account);
        randomsleep(tapping);
        if (tapping >= MAX_PASSWD_TRIES) {
            addreply_noformat(530, MSG_AUTH_FAILED);
            doreply();
            _EXIT(EXIT_FAILURE);
        }
        addreply_noformat(530, MSG_NOTRUST);
        doreply();
        return;
    }
    
#ifdef PER_USER_LIMITS
    if (per_user_max > 0U && ftpwho_read_count(account) >= per_user_max) {
        addreply(421, MSG_PERUSER_MAX, (unsigned long) per_user_max);
        doreply();
        _EXIT(1);
    }
#endif
    
    /* Add username to the uid/name cache */
    (void) getname(authresult.uid);
    
    if (
#if defined(WITH_LDAP) || defined(WITH_MYSQL) || defined(WITH_PGSQL) || defined(WITH_PUREDB) || defined(WITH_EXTAUTH)
        doinitsupgroups(NULL, authresult.uid, authresult.gid) != 0
#else
        doinitsupgroups(account, (uid_t) -1, authresult.gid) != 0
#endif
        ) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
        (void) 0;
#else
        die(421, LOG_WARNING, MSG_NOTRUST);
#endif
    }
    
    /* handle /home/user/./public_html form */
    if ((root_directory = strdup(authresult.dir)) == NULL) {
        die_mem();
    }
    hd = strstr(root_directory, "/./");
    if (hd != NULL) {
        if (chrooted != 0) {
            die(421, LOG_DEBUG, MSG_CANT_DO_TWICE);
        }
        if (create_home_and_chdir(root_directory)) {
            die(421, LOG_ERR, MSG_NO_HOMEDIR);
        }
        *++hd = 0;
        hd++;        
        if (chroot(root_directory) || chdir(hd)) {
            die(421, LOG_ERR, MSG_NO_HOMEDIR);
        }
        chrooted = 1;
#ifdef RATIOS
        if (ratio_for_non_anon == 0) {
            ratio_upload = ratio_download = 0U;
        }
        if (check_trustedgroup(authresult.uid, authresult.gid) != 0) {
            dot_write_ok = dot_read_ok = 1;
            ratio_upload = ratio_download = 0U;
            keepallfiles = 0;
        }
#endif
    } else {
        (void) free(root_directory);
        root_directory = (char *) "/";
        if (create_home_and_chdir(authresult.dir)) {
            die(421, LOG_ERR, MSG_NO_HOMEDIR);
        }
    }
    if (getcwd(wd, sizeof wd - (size_t) 1U) == NULL) {
        wd[0] = '/';
        wd[1] = 0;
    }
#ifdef HAVE_SYS_FSUID_H
    setfsuid(authresult.uid);
#endif
#ifndef NON_ROOT_FTP
    if (setgid(authresult.gid) ||
        setegid(authresult.gid)) {
        _EXIT(EXIT_FAILURE);
    }
# ifndef HAVE_SYS_FSUID_H
    if (seteuid(authresult.uid) != 0) {
        _EXIT(EXIT_FAILURE);
    }
# endif
#endif
    if (check_trustedgroup(authresult.uid, authresult.gid) != 0) {
        userchroot = 0;
        dot_write_ok = dot_read_ok = 1;
        keepallfiles = 0;
#ifdef RATIOS
        ratio_upload = ratio_download = 0U;
#endif
#ifdef QUOTAS
        user_quota_files = user_quota_size = ULONG_LONG_MAX;
#endif
    }
#ifdef QUOTAS
    if (hasquota() == 0) {
        userchroot = 1;
    }
#endif
    if (loggedin == 0) {
        candownload = 1;        /* real users can always download */
    }
#ifdef THROTTLING
    if ((throttling == 2) || (guest != 0 && throttling == 1)) {
        addreply_noformat(0, MSG_BANDWIDTH_RESTRICTED);
        (void) nice(NICE_VALUE);
    } else {
        throttling_delay = throttling_bandwidth_dl =
            throttling_bandwidth_ul = 0UL;
    }
#endif
#if !defined(MINIMAL) && defined(HAVE_GETGROUPS)
# ifdef SAFE_GETGROUPS_0
    ngroups = getgroups(0, NULL);
    if (ngroups > ngroups_max) {
        ngroups_max = ngroups;  
    }
# elif defined(_SC_NGROUPS_MAX)
    /* get the run time value */
    ngroups = (int) sysconf(_SC_NGROUPS_MAX);
    if (ngroups > ngroups_max) {
        ngroups_max = ngroups;  
    }    
# endif
    if ((groups = malloc(sizeof(GETGROUPS_T) * ngroups_max)) == NULL) {
        die_mem();
    }
    ngroups = getgroups(ngroups_max, groups);
    if (guest == 0 && ngroups > 0) {
        char reply[80 + MAX_USER_LENGTH];
        const char *q;       
        size_t p;
        
        if (SNCHECK(snprintf(reply, sizeof reply,
                             MSG_USER_GROUP_ACCESS ": ", account),
                    sizeof reply)) {
            _EXIT(EXIT_FAILURE);
        }
        p = strlen(reply);
        do {
            ngroups--;
            if ((ngroups != 0 && groups[ngroups] == groups[0]) ||
                (q = getgroup(groups[ngroups])) == NULL) {
                continue;
            }
            if (p + strlen(q) > 75) {
                reply[p] = 0;
                addreply(0, "%s", reply);
                *reply = 0;
                p = (size_t) 0U;
            }
            reply[p++] = ' ';
            while (*q != 0 && p < sizeof reply - (size_t) 1U) {
                reply[p++] = *q++;
            }
        } while (ngroups > 0);
        reply[p] = 0;
        addreply(0, "%s", reply);
    }
    free(groups);
#endif
    if (guest == 0 && allowfxp == 1) {
        addreply_noformat(0, MSG_FXP_SUPPORT);
    }
#ifdef RATIOS
    if (ratio_for_non_anon != 0 && ratio_upload > 0) {
        addreply(0, MSG_RATIO, ratio_upload, ratio_download);
    }
#endif
    if (userchroot != 0 && chrooted == 0) {
#ifndef NON_ROOT_FTP
# ifndef HAVE_SYS_FSUID_H
        disablesignals();
        seteuid((uid_t) 0);
# endif
#endif
        if (chdir(wd) || chroot(wd)) {    /* should never fail */
#ifdef WITH_PRIVSEP
            (void) setuid(authresult.uid);
            (void) seteuid(authresult.uid);
#else
# ifndef HAVE_SYS_FSUID_H
#  ifndef NON_ROOT_FTP
            (void) seteuid(authresult.uid);
#  endif
# endif
#endif
            die(421, LOG_ERR, MSG_CHROOT_FAILED);
        }
#ifdef WITH_PRIVSEP
        if (setuid(authresult.uid) != 0 || seteuid(authresult.uid) != 0) {
            _EXIT(EXIT_FAILURE);
        }
        enablesignals();        
#else
# ifndef NON_ROOT_FTP
#  ifndef HAVE_SYS_FSUID_H
        if (seteuid(authresult.uid) != 0) {
            _EXIT(EXIT_FAILURE);
        }
        enablesignals();
#  endif
# endif
#endif
#ifdef USE_CAPABILITIES
        drop_login_caps();
#endif        
        chrooted = 1;
#ifdef RATIOS
        if (ratio_for_non_anon == 0) {
            ratio_upload = ratio_download = 0U;
        }
#endif
        {
            const size_t rd_len = strlen(wd) + sizeof "/";
            
            if ((root_directory = malloc(rd_len)) == NULL) {
                die_mem();
            }
            snprintf(root_directory, rd_len, "%s/", wd);
        }
        wd[0] = '/';
        wd[1] = 0;
        if (chdir(wd)) {
            _EXIT(EXIT_FAILURE);
        }
#ifdef WITH_RFC2640
        nwd = charset_fs2client(wd);
        addreply(230, MSG_CURRENT_RESTRICTED_DIR_IS, nwd);
        free(nwd);
#else
        addreply(230, MSG_CURRENT_RESTRICTED_DIR_IS, wd);
#endif
    } else {
#ifdef WITH_RFC2640
        nwd = charset_fs2client(wd);
        addreply(230, MSG_CURRENT_DIR_IS, nwd);
        free(nwd);
#else
        addreply(230, MSG_CURRENT_DIR_IS, wd);
#endif
    }
    logfile(LOG_INFO, MSG_IS_NOW_LOGGED_IN, account);
#ifdef FTPWHO
    if (shm_data_cur != NULL) {
        ftpwho_lock();
        strncpy(shm_data_cur->account, account,
                sizeof shm_data_cur->account - (size_t) 1U);
        shm_data_cur->account[sizeof shm_data_cur->account - 1U] = 0;
        ftpwho_unlock();
        state_needs_update = 1;
    }
#endif
    loggedin = 1;
    if (getcwd(wd, sizeof wd - (size_t) 1U) == NULL) {
        wd[0] = '/';
        wd[1] = 0;
    }
#ifndef MINIMAL
    dobanner(0);
#endif
#ifdef QUOTAS
    displayquota(NULL);
#endif
#ifdef WITH_BONJOUR
    refreshManager();
#endif
}

void docwd(const char *dir)
{
#ifndef MINIMAL
    static unsigned long failures = 0UL;
#endif
    const char *where;
    char buffer[MAXPATHLEN + 256U];
#ifdef WITH_RFC2640
    char *nwd = NULL;
#endif
    
    if (loggedin == 0) {
        goto kaboom;
    }
    /*
     * secure and conformant tilde expansion routine. Need to be packaged in
     * a function so that it can be called in other commands and avoid
     * duplicate code in ls.c             -frank.
     */
    where = dir;
    if (dir == NULL || *dir == 0) {
        goto gohome;
    }
    if (*dir == '~') {
        const struct passwd *pw;
        
        if (dir[1] == 0) {         /* cd ~ */
            gohome:
            strncpy(buffer, chrooted != 0 ? "/" : authresult.dir,
                    sizeof buffer);
            buffer[sizeof buffer - (size_t) 1U] = 0;
            where = buffer;
        } else {                   /* cd ~user or cd ~user/ */
            char *bufpnt = buffer;
            size_t s = sizeof buffer;
            const char *dirscan = dir + 1;
            
            while (*dirscan != 0 && *dirscan != '/') {
                if (--s <= 0) {
                    goto kaboom;   /* script kiddy's playing */
                }
                *bufpnt++ = *dirscan++;
            }
            *bufpnt = 0;
            if (*buffer == 0) {        /* ~/... */
                snprintf(buffer, sizeof buffer, "%s%s",
                         chrooted != 0 ? "/" : authresult.dir, dirscan);
                where = buffer;
            } else if (authresult.slow_tilde_expansion == 0) {
                if (chrooted != 0 || guest != 0 ||
                    (pw = getpwnam(buffer)) == NULL || pw->pw_dir == NULL) {
                    /* try with old where = dir */
                } else {
                    snprintf(buffer, sizeof buffer, "%s%s", pw->pw_dir, dirscan);
                    where = buffer;
                }
            }
        }
    }
    if (checknamesanity(where, dot_read_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, where);
        return;
    }
    if (chdir(where) != 0) {
        
#ifdef WITH_DIRALIASES
        const int real_errno = errno;
        const char *where_alias;
        
        if ((where_alias = lookup_alias(where)) == NULL ||
            chdir(where_alias) != 0) {
            errno = real_errno;
        } else {
            goto chdir_success;
        }
#endif
    
        if (SNCHECK(snprintf(buffer, sizeof buffer,
                             MSG_CANT_CHANGE_DIR ": %s",
                             dir, strerror(errno)), sizeof buffer)) {
            _EXIT(EXIT_FAILURE);
        }
        logfile(LOG_INFO, "%s", buffer);
        addreply(550, "%s", buffer);
        
#ifndef MINIMAL
# ifndef NO_DIRSCAN_DELAY
        if (failures >= MAX_DIRSCAN_TRIES) {
            _EXIT(EXIT_FAILURE);
        }
        usleep2(failures * DIRSCAN_FAILURE_DELAY);  
        failures++;
# endif
#endif
        
        return;
    }
    
#ifdef WITH_DIRALIASES
    chdir_success:
#endif
    
#ifndef MINIMAL
    failures = 0UL;
    dobanner(1);
#endif
    if (getcwd(wd, sizeof wd - (size_t) 1U) == NULL) {
        if (*dir == '/') {
            if (SNCHECK(snprintf(wd, sizeof wd, "%s", dir), sizeof wd)) { /* already checked */
                _EXIT(EXIT_FAILURE);
            }
        } else {
            if (SNCHECK(snprintf(wd, sizeof wd, "%s/%s", wd, dir),
                        sizeof wd)) {
                kaboom:
                die(421, LOG_ERR, MSG_PATH_TOO_LONG);
            }
        }
    }
#ifdef WITH_RFC2640
    nwd = charset_fs2client(wd);
    addreply(250, MSG_CURRENT_DIR_IS, nwd);
    free(nwd);
#else
    addreply(250, MSG_CURRENT_DIR_IS, wd);
#endif
}

static void iptropize(const struct sockaddr_storage *ss)
{
    size_t t = sizeof *ss;
    register const unsigned char *s = (const unsigned char *) ss;
    
    iptropy = getpid();
    do {
        iptropy += (iptropy << 5);
        iptropy ^= (int) *s++;
    } while (--t > 0U);
}

#ifdef PROBE_RANDOM_AT_RUNTIME
static void pw_zrand_probe(void)
{
    static const char * const devices[] = {
        "/dev/arandom", "/dev/urandom", "/dev/random", NULL
    };
    register const char * const *device = devices;
    
    do {
        if (access(*device, F_OK | R_OK) == 0) {
            random_device = *device;
            break;
        }
        device++;
    } while (*device != NULL);
}
#endif

unsigned int zrand(void)
{
    int fd;
    int ret;
    
    if (chrooted != 0 ||
#ifdef PROBE_RANDOM_AT_RUNTIME
        ((fd = open(random_device, O_RDONLY | O_NONBLOCK)) == -1)        
#elif defined(HAVE_DEV_ARANDOM)
        ((fd = open("/dev/arandom", O_RDONLY | O_NONBLOCK)) == -1)
#elif defined(HAVE_DEV_URANDOM)
        ((fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK)) == -1)
#else
        ((fd = open("/dev/random", O_RDONLY | O_NONBLOCK)) == -1)
#endif
        ) {
        nax:
#ifdef HAVE_RANDOM
        return (unsigned int) (random() ^ iptropy);        
#else
        return (unsigned int) (rand() ^ iptropy);
#endif
    }
    if (read(fd, &ret, sizeof ret) != (ssize_t) sizeof ret) {
        (void) close(fd);
        goto nax;
    }
    (void) close(fd);
    return (unsigned int) (ret ^ iptropy);
}

static void keepalive(const int fd, int keep)
{
#ifdef SO_KEEPALIVE
    {
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &keep, sizeof keep);
    }
#endif
}

/* psvtype = 0: PASV */
/* psvtype = 1: EPSV */
/* psvtype = 2: SPSV */

void dopasv(int psvtype)
{
    struct sockaddr_storage dataconn;    /* my data connection endpoint */    
    unsigned long a = 0U;
    unsigned int p;
    int on;
    unsigned int firstporttried;
    
    if (loggedin == 0) {
        addreply_noformat(530, MSG_NOT_LOGGED_IN);
        return;
    }
    if (datafd != -1) {                /* for buggy clients */
        (void) close(datafd);
        datafd = -1;
    }
    fourinsix(&ctrlconn);
    if (STORAGE_FAMILY(ctrlconn) == AF_INET6 && psvtype == 0) {
        addreply_noformat(425, MSG_CANT_PASV);
        return;
    }
    firstporttried = firstport + zrand() % (lastport - firstport + 1);
    p = firstporttried;
    datafd = socket(STORAGE_FAMILY(ctrlconn), SOCK_STREAM, IPPROTO_TCP);
    if (datafd == -1) {
        error(425, MSG_CANT_PASSIVE);
        return;
    }
    on = 1;
    if (setsockopt(datafd, SOL_SOCKET, SO_REUSEADDR,
                   (char *) &on, sizeof on) < 0) {
        error(421, "setsockopt");
        return;
    }    
    dataconn = ctrlconn;
    for (;;) {
        if (STORAGE_FAMILY(dataconn) == AF_INET6) {
            STORAGE_PORT6(dataconn) = htons(p);
        } else {
            STORAGE_PORT(dataconn) = htons(p);
        }
        if (bind(datafd, (struct sockaddr *) &dataconn,
                 STORAGE_LEN(dataconn)) == 0) {
            break;
        }
        p--;
        if (p < firstport) {
            p = lastport;
        }
        if (p == firstporttried) {
            (void) close(datafd);
            datafd = -1;
            addreply_noformat(425, MSG_PORTS_BUSY);
            return;
        }
    }
    alarm(idletime);
    if (listen(datafd, DEFAULT_BACKLOG_DATA) < 0) {
        (void) close(datafd);
        datafd = -1;
        error(425, MSG_GETSOCKNAME_DATA);
        return;
    }
    switch (psvtype) {
    case 0:
        if (STORAGE_FAMILY(force_passive_ip) == 0) {
            a = ntohl(STORAGE_SIN_ADDR(dataconn));
        } else if (STORAGE_FAMILY(force_passive_ip) == AF_INET6) {
            (void) close(datafd);
            datafd = -1;
            addreply_noformat(425, MSG_NO_EPSV);
            return;
        } else if (STORAGE_FAMILY(force_passive_ip) == AF_INET) {
            a = ntohl(STORAGE_SIN_ADDR(force_passive_ip));
        } else {
            _EXIT(EXIT_FAILURE);
        }

        /* According to RFC, any message can follow 227. But broken NAT gateways
         * and connection tracking code rely on this. So don't translate the following
         * messages */

        addreply(227, "Entering Passive Mode (%lu,%lu,%lu,%lu,%u,%u)",
                 (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255,
                 (p >> 8) & 255, p & 255);
        break;
    case 1:
        addreply(229, "Extended Passive mode OK (|||%u|)", p);
        break;
    case 2:
        addreply(227, "%u", p);
        break;
    default:
        _EXIT(EXIT_FAILURE);
    }
    passive = 1;
}

void doport(const char *arg)
{
    unsigned int a1, a2, a3, a4, p1, p2;
    struct sockaddr_storage a;

    if (sscanf(arg, "%u,%u,%u,%u,%u,%u",
               &a1, &a2, &a3, &a4, &p1, &p2) != 6 ||
        a1 > 255 || a2 > 255 || a3 > 255 || a4 > 255 ||
        p1 > 255 || p2 > 255 || (a1|a2|a3|a4) == 0 ||
        (p1 | p2) == 0) {
        addreply_noformat(501, MSG_SYNTAX_ERROR_IP);
        return;
    }
    memset(&a, 0, sizeof a);
    STORAGE_FAMILY(a) = AF_INET;
    STORAGE_SIN_ADDR(a) =
        htonl((a1 << 24) | (a2 << 16) | (a3 << 8) | a4);
    SET_STORAGE_LEN(a, sizeof(struct sockaddr_in));
    doport2(a, (p1 << 8) | p2);
}

#ifndef WITH_PRIVSEP

static int doport3(const int protocol)
{
    struct sockaddr_storage dataconn;  /* his endpoint */    
    
# ifndef NON_ROOT_FTP
    static const in_port_t portlist[] = FTP_ACTIVE_SOURCE_PORTS;
    const in_port_t *portlistpnt = portlist;
# else
    static const in_port_t portlist[] = { 0U };
    const in_port_t *portlistpnt = portlist;
# endif
    int on;
    
# ifndef NON_ROOT_FTP
#  ifndef HAVE_SYS_FSUID_H
    disablesignals();
    seteuid((uid_t) 0);
#  endif
# endif    
    if ((datafd = socket(protocol, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        data_socket_error:
# ifndef NON_ROOT_FTP
#  ifndef HAVE_SYS_FSUID_H
        if (seteuid(authresult.uid) != 0) {
            _EXIT(EXIT_FAILURE);
        }
        enablesignals();
#  endif
# endif
        (void) close(datafd);
        datafd = -1;
        error(425, MSG_CANT_CREATE_DATA_SOCKET);
        
        return -1;
    }
    on = 1;
# ifdef SO_REUSEPORT
    (void) setsockopt(datafd, SOL_SOCKET, SO_REUSEPORT,
                      (char *) &on, sizeof on);    
# else
    (void) setsockopt(datafd, SOL_SOCKET, SO_REUSEADDR,
                      (char *) &on, sizeof on);
# endif
    memcpy(&dataconn, &ctrlconn, sizeof dataconn);
    for (;;) {
        if (STORAGE_FAMILY(dataconn) == AF_INET6) {
            STORAGE_PORT6(dataconn) = htons(*portlistpnt);
        } else {
            STORAGE_PORT(dataconn) = htons(*portlistpnt);
        }
        if (bind(datafd, (struct sockaddr *) &dataconn, 
                 STORAGE_LEN(dataconn)) == 0) {
            break;
        }
# ifdef USE_ONLY_FIXED_DATA_PORT
        (void) sleep(1U);
# else
        if (*portlistpnt == (in_port_t) 0U) {
            goto data_socket_error;
        }
        portlistpnt++;
# endif
    }
# ifndef NON_ROOT_FTP
#  ifndef HAVE_SYS_FSUID_H
    if (seteuid(authresult.uid) != 0) {
        _EXIT(EXIT_FAILURE);
    }
    enablesignals();
#  endif
# endif
    
    return 0;
}

#else

/* Privilege-separated version of doport3() */

static int doport3(const int protocol)
{
    if ((datafd = privsep_bindresport(protocol, ctrlconn)) == -1) {
        error(425, MSG_CANT_CREATE_DATA_SOCKET);
        
        return -1;
    }
    return 0;
}

#endif

void doport2(struct sockaddr_storage a, unsigned int p)
{
    if (loggedin == 0) {
        addreply_noformat(530, MSG_NOT_LOGGED_IN);
        return;
    }
    if (epsv_all != 0) {
        addreply_noformat(501, MSG_ACTIVE_DISABLED);
        return;
    }
    if (datafd != -1) {    /* for buggy clients saying PORT over and over */
        (void) close(datafd);
        datafd = -1;
    }
    if (p < 1024U) {
        addreply_noformat(501, MSG_BAD_PORT);
        return;
    }
    if (doport3(STORAGE_FAMILY(a) == AF_INET6 ? PF_INET6 : PF_INET) != 0) {
        return;
    }
    peerdataport = (in_port_t) p;
    if (addrcmp(&a, &peer) != 0) {
        char hbuf[NI_MAXHOST];
        char peerbuf[NI_MAXHOST];
        
        if (getnameinfo((struct sockaddr *) &a, STORAGE_LEN(a),
                        hbuf, sizeof hbuf, NULL,
                        (size_t) 0U, NI_NUMERICHOST) != 0 ||
            getnameinfo((struct sockaddr *) &peer, STORAGE_LEN(peer),
                        peerbuf, sizeof peerbuf, NULL,
                        (size_t) 0U, NI_NUMERICHOST) != 0) {
            goto hu;
        }
        if (allowfxp == 0 || (allowfxp == 1 && guest != 0)) {
            hu:
            (void) close(datafd);
            datafd = -1;
            addreply(500, MSG_NO_FXP, hbuf, peerbuf);
            return;
        } else {
            addreply(0, MSG_FXP, peerbuf, hbuf);
            memcpy(&peer, &a, sizeof a);
        }
    }
    passive = 0;
    
    addreply_noformat(200, MSG_PORT_SUCCESSFUL);
    return;
}

void closedata(void)
{
    volatile int tmp_xferfd = xferfd;   /* do not simplify this... */

#ifdef WITH_TLS
    tls_close_session(&tls_data_cnx);
    tls_data_cnx = NULL;
#endif    
    xferfd = -1;           /* ...it avoids a race */
    (void) close(tmp_xferfd);
}

void opendata(void)
{
    struct sockaddr_storage dataconn;    /* his data connection endpoint */
    int fd;
    socklen_t socksize;
    
    if (xferfd != -1) {
        closedata();
    }
    if (datafd == -1) {
        addreply_noformat(425, MSG_NO_DATA_CONN);        
        return;
    }
    
    if (passive != 0) {
        fd_set rs;
        struct timeval tv;
        
        alarm(idletime);

        for (;;) {
            FD_ZERO(&rs);
            FD_SET(datafd, &rs);
            tv.tv_sec = idletime;
            tv.tv_usec = 0;
            /* I suppose it would be better to listen for ABRT too... */
            
            if (select(datafd + 1, &rs, NULL, NULL, &tv) <= 0) {
                die(421, LOG_INFO, MSG_TIMEOUT_DATA , 
                    (unsigned long) idletime);
            }
            socksize = (socklen_t) sizeof(dataconn);
            memset(&dataconn, 0, sizeof dataconn);
            if ((fd = accept(datafd, (struct sockaddr *) &dataconn,
                             &socksize)) == -1) {
                nope:
                (void) close(datafd);
                datafd = -1;
                error(421, MSG_ACCEPT_FAILED);
                return;
            }
            if (STORAGE_FAMILY(dataconn) != AF_INET
                && STORAGE_FAMILY(dataconn) != AF_INET6) {
                (void) close(fd);
                goto nope;
            }
            fourinsix(&dataconn);
            if (addrcmp(&peer, &dataconn) == 0) {
                break;
            }
            if (allowfxp == 0 || (allowfxp == 1 && guest != 0)) {
                shutdown(fd, 2);
                (void) close(fd);
            } else {
                break;
            }
        }
        addreply_noformat(150, MSG_ACCEPT_SUCCESS);
    } else {
        struct sockaddr_storage peer2;
        unsigned long tries = 1UL + idletime / 2UL;
        
        peer2 = peer;
        if (STORAGE_FAMILY(peer) == AF_INET6) {
            STORAGE_PORT6(peer2) = htons(peerdataport);
        } else {
            STORAGE_PORT(peer2) = htons(peerdataport);
        }
        again:
        if (connect(datafd, (struct sockaddr *) &peer2,
                    STORAGE_LEN(peer2)) != 0) {
            if ((errno == EAGAIN || errno == EINTR
#ifdef EADDRINUSE
                 || errno == EADDRINUSE
#endif
                 ) && tries > 0UL) {
                tries--;
                usleep2(1000000UL);
                goto again;
            }
            addreply(425, MSG_CNX_PORT_FAILED ": %s",
                     peerdataport, strerror(errno));
            (void) close(datafd);
            datafd = -1;
            return;
        }
        fd = datafd;
        datafd = -1;
        addreply(150, MSG_CNX_PORT, peerdataport);
    }
    
    {
        int fodder;
#ifdef IPTOS_THROUGHPUT
        fodder = IPTOS_THROUGHPUT;
        setsockopt(fd, SOL_IP, IP_TOS, (char *) &fodder, sizeof fodder);
#endif
#ifndef NO_TCP_NOPUSH
# ifdef TCP_NOPUSH
        fodder = 1;
        setsockopt(fd, SOL_TCP, TCP_NOPUSH, (char *) &fodder, sizeof fodder);
# endif
#endif
#ifndef NO_TCP_LARGE_WINDOW
# if defined(SO_SNDBUF) || defined(SO_RCVBUF)
        fodder = 65536;
# endif
# ifdef SO_SNDBUF
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &fodder, sizeof fodder);
# endif        
# ifdef SO_RCVBUF
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &fodder, sizeof fodder);
# endif        
#endif
#ifndef NO_KEEPALIVE
        keepalive(fd, 1);
#endif
    }
    xferfd = fd;
#ifdef WITH_TLS
    if (data_protection_level == CPL_PRIVATE) {
        tls_init_data_session(xferfd);
    }
#endif
}

#ifndef MINIMAL
void dochmod(char *name, mode_t mode)
{
    static dev_t root_st_dev;
    static ino_t root_st_ino;
    struct stat st2;
    int fd = -1;
    
    if (nochmod != 0 && authresult.uid != (uid_t) 0) {
        addreply(550, MSG_CHMOD_FAILED, name);
        return;
    }
# ifndef ANON_CAN_CHANGE_PERMS
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_CHANGE_PERMS);
        return;
    }
# endif
    if (name == NULL || *name == 0) {
        addreply_noformat(501, MSG_NO_FILE_NAME);
        return;
    }
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        return;
    }
    fd = open(name, O_RDONLY);
    if (fd == -1) {
        goto failure;
    }
    if ((root_st_dev | root_st_ino) == 0) {
        struct stat st;
        
        if (stat("/", &st) != 0) {
            goto failure;
        }
        root_st_dev = st.st_dev;
        root_st_ino = st.st_ino;
    }
    if (fstat(fd, &st2) != 0) {
        goto failure;
    }
# ifdef QUOTAS
    if (hasquota() == 0 && S_ISDIR(st2.st_mode)) {
        mode |= 0500;
    }
# endif
    if (st2.st_ino == root_st_ino && st2.st_dev == root_st_dev) {
        mode |= 0700;
    } else if (be_customer_proof != 0) {
        mode |= (S_ISDIR(st2.st_mode) ? 0700 : 0600);
    }
    if (fchmod(fd, mode) < 0 && chmod(name, mode) < 0) {
        failure:
        if (fd != -1) {
            (void) close(fd);
        }
        addreply(550, MSG_CHMOD_FAILED ": %s", name, strerror(errno));
        return;
    }
    (void) close(fd);
    addreply(200, MSG_CHMOD_SUCCESS, name);
}

void doutime(char *name, const char * const wanted_time)
{
    struct tm tm;
    time_t ts;
    struct utimbuf tb;
    
# ifndef ANON_CAN_CHANGE_UTIME
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_CHANGE_PERMS);
        return;
    }
# endif
    if (name == NULL || *name == 0) {
        addreply_noformat(501, MSG_NO_FILE_NAME);
        return;
    }
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        return;
    }
    memset(&tm, 0, sizeof tm);
    sscanf(wanted_time, "%4u%2u%2u%2u%2u%2u", &tm.tm_year, &tm.tm_mon,
           &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_mon--;
    tm.tm_year -= 1900;
# ifdef USE_LOCAL_TIME_FOR_SITE_UTIME
    ts = mktime(&tm);
# else
#  ifdef HAVE_TIMEGM
    ts = timegm(&tm);
#  elif defined(HAVE_PUTENV)
    {
        putenv("TZ=UTC+00:00");
#   ifdef HAVE_TZSET
        tzset();
#   endif
        ts = mktime(&tm);
        putenv(default_tz_for_putenv);
        tzset();        
    }
#  else
    ts = mktime(&tm);
#  endif
# endif
    if (tm.tm_mon < 0 || tm.tm_year <= 0 || ts == (time_t) -1) {
        addreply_noformat(501, MSG_TIMESTAMP_FAILURE);
        return;
    }
    tb.actime = tb.modtime = ts;
    if (utime(name, &tb) < 0) {
        addreply(550, "utime(%s): %s", name, strerror(errno));
    } else {
        addreply_noformat(250, "UTIME OK");
    }    
}
#endif

void dodele(char *name)
{
#ifndef ANON_CAN_DELETE
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_DELETE);
        return;
    }
#endif
    if (name == NULL || *name == 0) {
        addreply_noformat(501, MSG_NO_FILE_NAME);
        return;
    }
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        return;
    }
    if (keepallfiles != 0) {
#ifdef EPERM
        errno = EPERM;
#else
        errno = 1;
#endif
        goto denied;
    }
    
    /*
     * What we do here may look a bit strange. It's to defend against
     * change-after-stat attacks. If we simply do lstat(name), then unlink(name)
     * there's a race. An attacker can rename the file between these two
     * system calls, so that a big file is lstat()ed, but a dummy tiny file is
     * unlinked. That way, an attacker could easily get extra quota.
     * To defend against this attack, we rename the file to an unique dot-file
     * (an atomic operation) . People subject to quotas can't access dot-files.
     * So we can securely stat it and unlink it. Having the pid in the file
     * name should be enough to avoid that two concurrent sessions create the
     * same temporary file. But to be paranoid to the extreme, we add some
     * random number to that.
     */
    
#ifdef QUOTAS
    {
        char *p;
        struct stat st;
        struct stat st2;
        size_t dirlen = (size_t) 0U;    
        char qtfile[MAXPATHLEN + 1];
        
        if ((p = strrchr(name, '/')) != NULL) {
            if ((dirlen = p - name + (size_t) 1U) >= sizeof qtfile) {
                goto denied;       /* should never happen */
            }
            memcpy(qtfile, name, dirlen);   /* safe, dirlen < sizeof qtfile */
        }   
        if (SNCHECK(snprintf(qtfile + dirlen, sizeof qtfile - dirlen,
                             PUREFTPD_TMPFILE_PREFIX "rename.%lu.%x",
                             (unsigned long) getpid(), zrand()),
                    sizeof qtfile)) {
            goto denied;
        }
        if (lstat(name, &st) != 0) {
            goto denied;
        }
        if (!S_ISREG(st.st_mode)
# ifndef NEVER_DELETE_SYMLINKS
            && !S_ISLNK(st.st_mode)
# endif
            ) {
# ifdef EINVAL
            errno = EINVAL;
# endif
            goto denied;
        }
        if (rename(name, qtfile) != 0) {
            goto denied;
        }
        if (lstat(qtfile, &st2) != 0 ||
            st.st_dev != st2.st_dev ||
            st.st_ino != st2.st_ino ||
            st.st_size != st2.st_size) {
# ifdef EINVAL
            errno = EINVAL;
# endif            
            goto denied;
        }        
        if (unlink(qtfile) < 0) {
            /*
             * Race if rename() goes to an existing file.
             * seems very difficult to exploit, though.
             * Does a perfect userland answer exist, after all?
             */
            (void) rename(qtfile, name);
            goto denied;
        }
        {
            Quota quota;
            int overflow;
            
            (void) quota_update(&quota, -1LL,
                                -((long long) st.st_size), &overflow);
            displayquota(&quota);
        }
    }
#else
    if (unlink(name) < 0) {
        goto denied;
    }
#endif
    addreply(250, MSG_DELE_SUCCESS, name);
    logfile(LOG_NOTICE, "%s: " MSG_DELE_SUCCESS, wd, name);
    return;
    
    denied:
    addreply(550, MSG_DELE_FAILED ": %s", name, strerror(errno));
}

#ifndef WITHOUT_ASCII
static ssize_t doasciiwrite(int fd, const char * const buf, size_t size)
{
    char *asciibuf;
    register char *asciibufpnt;
    size_t z = (size_t) 0U;
    int writeret;
    
    if (size <= (size_t) 0U) {         /* stupid paranoia #1 */
        return (ssize_t) 0;
    }
    if (size > ASCII_CHUNKSIZE ||      /* stupid paranoia #2 */
        (asciibuf = ALLOCA((size_t) (ASCII_CHUNKSIZE * 2U))) == NULL) {
        return (ssize_t) -1;
    }
    asciibufpnt = asciibuf;
    do {
        if (buf[z] == '\n') {
            *asciibufpnt++ = '\r';
        }
        *asciibufpnt++ = buf[z];
        z++;
    } while (z < size);

#ifdef WITH_TLS
    if (enforce_tls_auth && data_protection_level == CPL_PRIVATE) {
        writeret = secure_safe_write(asciibuf,
                                     (size_t) (asciibufpnt - asciibuf));
    } else
#endif
    {
        writeret = safe_write(fd, asciibuf, (size_t) (asciibufpnt - asciibuf));
    }
    ALLOCA_FREE(asciibuf);
    if (writeret < 0) {
        return (ssize_t) -1;
    }
    return size;
}
#endif

static double get_usec_time(void)
{
    struct timeval tv;
    struct timezone tz;
    
    if (gettimeofday(&tv, &tz) < 0) {
        return 0.0;
    }
    return (double) tv.tv_sec + ((double) tv.tv_usec) / 1000000.0;
}

static void displayrate(const char *word, off_t size,
                        const double started,
                        const char * const name, int up)
{
    double ended;
    double t;
    double speed;
    char speedstring[64];

    ended = get_usec_time();
    
    t = ended - started;
    addreply_noformat(226, MSG_TRANSFER_SUCCESSFUL);
    if (t > 0.0 && size > (off_t) 0) {
        speed = size / t;
    } else {
        speed = 0.0;
    }
    if (speed > 524288.0) {
        addreply(0, MSG_TRANSFER_RATE_M, t, speed / 1048576.0);
    } else if (speed > 512.0) {
        addreply(0, MSG_TRANSFER_RATE_K, t, speed / 1024.0);
    } else if (speed > 0.1) {
        addreply(0, MSG_TRANSFER_RATE_B, t, speed);
    }
    if (!SNCHECK(snprintf(speedstring, sizeof speedstring,
                          " (%llu bytes, %.2fKB/sec)",
                          (unsigned long long) size, speed / 1024.0),
                 sizeof speedstring)) {
        logfile(LOG_NOTICE, "%s%s%s%s %s %s", root_directory,
                *name == '/' ? "" : wd,
                (*name != '/' && (!*wd || wd[strlen(wd) - 1] != '/'))
                ? "/" : "", name, word, speedstring);
    }
    /* Tons of #ifdef here, but it avoids a pointless call to realpath() */
#if defined(WITH_UPLOAD_SCRIPT) || defined(WITH_ALTLOG)
    if (
# ifdef WITH_ALTLOG
        altlog_format != ALTLOG_NONE
# endif
# if defined(WITH_UPLOAD_SCRIPT) && defined(WITH_ALTLOG)
        ||
# endif
# if (defined(WITH_UPLOAD_SCRIPT))
        (do_upload_script != 0 && up != 0)
# endif
        )
    {
        char *alloca_filename_real;
        const size_t sizeof_filename_real = MAXPATHLEN + VHOST_PREFIX_MAX_LEN;
        char *resolved_path;
        const size_t sizeof_resolved_path = MAXPATHLEN + 1U;
        
        if ((resolved_path = malloc(sizeof_resolved_path)) == NULL) {
            return;
        }
        resolved_path[sizeof_resolved_path - 1U] = 0;
        if (realpath(name, resolved_path) == NULL) {
            (void) unlink(name);            
            free(resolved_path);
            logfile(LOG_ERR, "realpath() failure : [%s] => [%s]",
                    name, strerror(errno));            
            return;
        }
        if (resolved_path[sizeof_resolved_path - 1U] != 0) {
            for (;;) {
                *resolved_path++ = 0;
            }
        }
        if ((alloca_filename_real = ALLOCA(sizeof_filename_real)) == NULL) {
            free(resolved_path);
            return;
        }
# ifdef WITH_VIRTUAL_CHROOT
        if (SNCHECK(snprintf(alloca_filename_real, sizeof_filename_real,
                             "\001%s", resolved_path), sizeof_filename_real)) {
            goto rp_failure;
        }        
# else
        if (SNCHECK(snprintf(alloca_filename_real, sizeof_filename_real,
                             "\001%s%s", root_directory,
                             (*resolved_path == '/' ? resolved_path + 1 :
                              resolved_path)), sizeof_filename_real)) {
            goto rp_failure;
        }
# endif
# ifdef WITH_ALTLOG
        (void) altlog_writexfer(up,
                                alloca_filename_real + 1, size, t);
# endif
# if defined(WITH_UPLOAD_SCRIPT)
        if (do_upload_script != 0 && up != 0) {
            upload_pipe_push(account, alloca_filename_real);
        }
# endif
        rp_failure:
        free(resolved_path);
        ALLOCA_FREE(alloca_filename_real);
    }
#else
    (void) up;
#endif
}

static void displayopenfailure(const char * const name)
{
    char buffer[MAXPATHLEN + 42U];
    const int e = errno;
    
    if (SNCHECK(snprintf(buffer, sizeof buffer, MSG_OPEN_FAILURE, name),
                sizeof buffer)) {
        _EXIT(EXIT_FAILURE);
    }
    errno = e;
    error(550, buffer);
}

#ifndef SENDFILE_NONE
int sendfile_send(int f, struct stat st, double *started)
{
    off_t left;
    off_t o;
# ifdef THROTTLING
    double ended;
    off_t transmitted = 0;
# endif

    o = restartat;
    while (o < st.st_size) {
# ifdef FTPWHO
    /* There is no locking here, and it is intentional */
        if (shm_data_cur != NULL) {
            shm_data_cur->download_current_size = o;
        }
# endif
        left = st.st_size - o;
# ifdef FTPWHO
        if (left > (off_t) dl_chunk_size) {
            left = (off_t) dl_chunk_size;
        }
# elif defined(THROTTLING)
        if (throttling_bandwidth_dl > 0UL &&
            left > (off_t) dl_chunk_size) {
            left = (off_t) dl_chunk_size;
        }
# endif
        while (left > (off_t) 0) {
# ifdef SENDFILE_LINUX
            ssize_t w;

            do {
                w = sendfile(xferfd, f, &o, (size_t) left);
            } while (w < 0 && errno == EINTR);
            if (w == 0) {
                w--;
            }
# elif defined(SENDFILE_FREEBSD)
            off_t w;

            if (sendfile(f, xferfd, o, (size_t) left, NULL, &w, 0) < 0) {
                if ((errno != EAGAIN && errno != EINTR) || w < (off_t) 0) {
                    w = (off_t) -1;
                } else {
                    o += w;
                }
            } else {
                o += w;
            }
# elif defined(SENDFILE_HPUX)
            sbsize_t w;

            if ((w = sendfile(xferfd, f, o, (bsize_t) left,NULL, 0)) < 0) {
                if ((errno != EAGAIN && errno != EINTR) || w < (off_t) 0) {
                    w = (off_t) -1;
                } else {
                    o += w;
                }
            } else {
                o += w;
            }
# elif defined(SENDFILEV_SOLARIS)
            ssize_t w;
            struct sendfilevec vec[1];

            vec[0].sfv_fd   = f;
            vec[0].sfv_flag = 0;
            vec[0].sfv_off  = o;
            vec[0].sfv_len  = (size_t) left;
            if (sendfilev(xferfd, vec, 1, &w) < 0) {
                if ((errno != EAGAIN && errno != EINTR) || w < (off_t) 0) {
                    w = (off_t) -1;
                } else {
                    o += w;
                }
            } else {
                o += w;
            }
# endif
            if (w < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    /* wait idletime seconds for progsress */
                    fd_set rs;
                    fd_set ws;
                    struct timeval tv;

                    FD_ZERO(&rs);
                    FD_ZERO(&ws);
                    FD_SET(0, &rs);
                    safe_fd_set(xferfd, &ws);
                    tv.tv_sec = idletime;
                    tv.tv_usec = 0;
                    if (xferfd == -1 ||
                        select(xferfd + 1, &rs, &ws, NULL, &tv) <= 0 ||
                        FD_ISSET(0, &rs)) {
                        /* we assume it is ABRT since nothing else is legal */
                        (void) close(f);
                        closedata();
                        addreply_noformat(426, MSG_ABORTED);
                        return -1;
                    } else if (!(safe_fd_isset(xferfd, &ws))) {
                    /* client presumably gone away */
                        closedata();
                        die(421, LOG_INFO, MSG_TIMEOUT_DATA,
                            (unsigned long) idletime);
                    }
                w = 0;
                } else {
                    (void) close(f);
                    if (xferfd != -1) {
                        closedata();
                        addreply_noformat(450, MSG_DATA_WRITE_FAILED);
                        logfile(LOG_INFO, MSG_ABORTED);
                    }
                    return -1;
                }
            }

            if (w < 0) {        /* Maybe the file has shrunk? */
                if (fstat(f, &st) < 0) {
                    o = st.st_size;
                }
                left = (off_t) 0;
            } else if (w != 0) {
                downloaded += (unsigned long long) w;
                left -= w;
# ifdef THROTTLING
                if (o < st.st_size && throttling_bandwidth_dl > 0UL) {
                    long double delay;
                    ended = get_usec_time();
                    transmitted += w;
                    delay = (transmitted /
                            (long double) throttling_bandwidth_dl) -
                            (long double) (ended - *started);
                    if (delay > (long double) MAX_THROTTLING_DELAY) {
                        *started = ended;
                        transmitted = (off_t) 0;
                        delay = (long double) MAX_THROTTLING_DELAY;
                    }
                    if (delay > 0.0L) {
                        usleep2((unsigned long) (delay * 1000000.0L));
                    }
                }
# endif
            }
        }
    }

    return 0;
}
#endif  /*  End !defined(SENDFILE_NONE) */


int mmap_send(int f, struct stat st, double *started)
{
    off_t s;
    off_t skip;
    off_t o;
    char *p, *buf;
    off_t left;
# ifdef THROTTLING
    double ended;
    off_t transmitted = 0;
# endif

    o = restartat & ~(map_size - 1);
    skip = restartat - o;
    while (o < st.st_size) {
# ifdef FTPWHO
        if (shm_data_cur != NULL) {
            shm_data_cur->download_current_size = o;
        }
# endif
        left = st.st_size - o;
        if (left > (off_t) dl_chunk_size) {
            left = (off_t) dl_chunk_size;
        }
        buf = mmap(0, left, PROT_READ, MAP_FILE | MAP_SHARED, f, o);
        if (buf == (char *) MAP_FAILED) {
            closedata();
            (void) close(f);
            error(451, MSG_MMAP_FAILED);
            return -1;
        }
        p = buf;
        o += left;
        s = left;
        while (left > skip) {
            ssize_t w;

# ifdef WITH_TLS
            if (enforce_tls_auth && data_protection_level == CPL_PRIVATE) {
                w = secure_safe_write (p + skip, (size_t) (left - skip));
            } else
# endif
            {
                while ((w = write(xferfd, p + skip, (size_t) (left - skip))) <
                       (ssize_t) 0 && errno == EINTR);
            }
            if (w < (ssize_t) 0) {
                if (errno == EAGAIN && xferfd != -1) {
                /* wait idletime seconds for progress */
                    fd_set rs;
                    fd_set ws;
                    struct timeval tv;

                    FD_ZERO(&rs);
                    FD_ZERO(&ws);
                    FD_SET(0, &rs);
                    safe_fd_set(xferfd, &ws);
                    tv.tv_sec = idletime;
                    tv.tv_usec = 0;
                    if (xferfd == -1 ||
                        select(xferfd + 1, &rs, &ws, NULL, &tv) <= 0 ||
                        FD_ISSET(0, &rs)) {
                        /* we assume it is ABRT since nothing else is legal */
                        (void) munmap(buf, s);
                        (void) close(f);
                        closedata();
                        addreply_noformat(426, MSG_ABORTED);
                        return -1;
                    } else if (!(safe_fd_isset(xferfd, &ws))) {
                        /* client presumably gone away */
                        die(421, LOG_INFO, MSG_TIMEOUT_DATA ,
                            (unsigned long) idletime);
                    }
                    w = (ssize_t) 0;
                } else {
                        (void) close(f);
                        if (xferfd != -1) {
                            closedata();
                            addreply_noformat(450, MSG_DATA_WRITE_FAILED);
                            logfile(LOG_INFO, MSG_ABORTED);
                        }
                        return -1;
                }
            }
            downloaded += (unsigned long long) w;
            left -= w;
            p += w;
# ifdef THROTTLING
            if (throttling_bandwidth_dl > 0UL) {
                long double delay;

                ended = get_usec_time();
                transmitted += w;
                delay = (transmitted /
                         (long double) throttling_bandwidth_dl) -
                         (long double) (ended - *started);
                if (delay > (long double) MAX_THROTTLING_DELAY) {
                    *started = ended;
                    transmitted = (off_t) 0;
                    delay = (long double) MAX_THROTTLING_DELAY;
                }
                if (delay > 0.0L) {
                    usleep2((unsigned long) (delay * 1000000.0L));
                }
            }
# endif
        }
        skip = (off_t) 0;
        (void) munmap(buf, s);
    }

    return 0;
}

void doretr(char *name)
{
    int f;
    off_t s;
    off_t skip;
    off_t o;
    struct stat st;
    char *p, *buf;
    off_t left;
    double started;
#ifdef THROTTLING
    double ended;
    off_t transmitted = 0;
#endif

    if (!candownload) {
        addreply(550, MSG_LOAD_TOO_HIGH, load);
        goto end;
    }
# if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__CYGWIN__)
    if (type < 1 || (type == 1 && restartat > (off_t) 1)) {
        addreply_noformat(503, MSG_NO_ASCII_RESUME);
        goto end;
    }
# endif
    if (checknamesanity(name, dot_read_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        goto end;
    }
    if ((f = open(name, O_RDONLY)) == -1) {
        displayopenfailure(name);
        goto end;
    }
    if (fstat(f, &st) < 0) {
        stat_failure:
        (void) close(f);
        error(451, MSG_STAT_FAILURE);
        goto end;
    }
    if (S_ISLNK(st.st_mode)) {
        if (stat(name, &st) < 0) {
            goto stat_failure;
        }
    }
    if (restartat && (restartat > st.st_size)) {
        (void) close(f);
        addreply(451, MSG_REST_TOO_LARGE_FOR_FILE "\n" MSG_REST_RESET,
                 (long long) restartat, (long long) st.st_size);
        goto end;
    }
    if (!S_ISREG(st.st_mode) || ((off_t) st.st_size != st.st_size)) {
        (void) close(f);
        addreply_noformat(550, MSG_NOT_REGULAR_FILE);
        goto end;
    }
    if (warez != 0 && st.st_uid == warez && guest != 0) {
        (void) close(f);
        addreply(550, MSG_NOT_MODERATED);
        goto end;
    }
#ifdef RATIOS
    if (ratio_upload > 0U && ratio_download > 0U) {
        if ((downloaded + st.st_size - restartat) / ratio_download >
            (uploaded / ratio_upload)) {
            (void) close(f);
            addreply(550, MSG_RATIO_DENIAL, ratio_upload, ratio_download,
                     (unsigned long long) uploaded / 1024ULL,
                     (unsigned long long) downloaded / 1024ULL);
            goto end;
        }
    }
#endif
    opendata();
    if (xferfd == -1) {
        (void) close(f);
        goto end;
    }
    if (restartat > st.st_size) {
        /* some clients insist on doing this.  I can't imagine why. */
        addreply_noformat(226, MSG_NO_MORE_TO_DOWNLOAD);
        (void) close(f);
        closedata();
        goto end;
    }
#ifdef NON_BLOCKING_DATA_SOCKET
    {
        int flags;

        if ((flags = fcntl(xferfd, F_GETFL, 0)) < 0) {
        (void) close(f);
    closedata();
        error(451, "fcntl");
        goto end;
    }
        flags |= FNDELAY;
        fcntl(xferfd, F_SETFL, flags);
    }
#endif

#ifndef DISABLE_HUMOR
    if ((time(NULL) % 100) == 0) {
        addreply_noformat(0, MSG_WINNER);
    }
#endif
    if (st.st_size - restartat > 4096) {
        addreply(0, MSG_KBYTES_LEFT, (double) ((st.st_size - restartat) / 1024.0));
    }
    doreply();
    state_needs_update = 1;
    setprocessname("pure-ftpd (DOWNLOAD)");

#ifdef FTPWHO
    if (shm_data_cur != NULL) {
        const size_t sl = strlen(name);

        ftpwho_lock();
        shm_data_cur->state = FTPWHO_STATE_DOWNLOAD;
        shm_data_cur->download_total_size = st.st_size;
        shm_data_cur->download_current_size = restartat;
        shm_data_cur->restartat = restartat;
        (void) time(&shm_data_cur->xfer_date);
        if (sl < sizeof shm_data_cur->filename - 1U) {
            /* no overflow, see the previous line */
            strcpy(shm_data_cur->filename, name);   /* audited - ok */
        } else {
            /* same thing here, no possible buffer overflow, keep cool */
            strcpy(shm_data_cur->filename,   /* audited - ok */
                   &name[sl - sizeof shm_data_cur->filename + 1U]);
        }
        ftpwho_unlock();
    }
#endif
    alarm(MAX_SESSION_XFER_IDLE);
    started = get_usec_time();

#ifndef WITHOUT_ASCII
    if (type == 2)
#endif
    {            /* Binary */
        CORK_ON(xferfd);
#ifndef SENDFILE_NONE
# if defined(WITH_TLS)
        if (data_protection_level == CPL_NONE ||
            data_protection_level == CPL_CLEAR)
# endif /* End WITH_TLS */
        {
            if (sendfile_send(f, st, &started) < 0) {
                goto end;
            }
        }
# if defined(WITH_TLS)
        else
# endif /* End WITH_TLS */
        
#endif  /*      End !defined(SENDFILE_NONE)     */

#if defined(SENDFILE_NONE) || defined(WITH_TLS)
        {
            if (mmap_send(f, st, &started) < 0) {
                goto end;
            }
        }
#endif  /*      SENDFILE_NONE || WITH_TLS       */
    }
#ifndef WITHOUT_ASCII
    else {                    /* ASCII */
        o = restartat & ~(map_size - 1);
        skip = restartat - o;
        CORK_ON(xferfd);
        while (o < st.st_size) {
# ifdef FTPWHO
            if (shm_data_cur != NULL) {
                shm_data_cur->download_current_size = o;
            }
# endif
            left = st.st_size - o;
            if (left > (off_t) dl_chunk_size) {
                left = (off_t) dl_chunk_size;
            }
            if (left > (off_t) ASCII_CHUNKSIZE) {
                left = ASCII_CHUNKSIZE;
            }
            buf = mmap(0, left, PROT_READ, MAP_FILE | MAP_SHARED, f, o);
            if (buf == (char *) MAP_FAILED) {
                closedata();
                (void) close(f);
                error(451, MSG_MMAP_FAILED);
                goto end;
            }
            p = buf;
            o += left;
            s = left;
            while ((off_t) left > skip) {
                ssize_t w;
                w = doasciiwrite(xferfd, (const char *) p + skip,
                                 (size_t) (left - skip));
                if (w < (ssize_t) 0) {
                    if (xferfd != -1 && (errno == EAGAIN || errno == EINTR)) {
                        /* wait idletime seconds for progress */
                        fd_set rs;
                        fd_set ws;
                        struct timeval tv;

                        FD_ZERO(&rs);
                        FD_ZERO(&ws);
                        FD_SET(0, &rs);
                        safe_fd_set(xferfd, &ws);
                        tv.tv_sec = idletime;
                        tv.tv_usec = 0;
                        if (xferfd == -1 ||
                            select(xferfd + 1, &rs, &ws, NULL, &tv) <= 0 ||
                            FD_ISSET(0, &rs)) {
                            /* we assume it is ABRT since nothing else is legal */
                            (void) munmap(buf, s);
                            closedata();
                            (void) close(f);
                            addreply_noformat(426, MSG_ABORTED);
                            goto end;
                        } else if (!(safe_fd_isset(xferfd, &ws))) {
                            /* client presumably gone away */
                            die(421, LOG_INFO, MSG_TIMEOUT_DATA ,
                                   (unsigned long) idletime);
                        }
                        w = (ssize_t) 0;
                    } else {
                        (void) close(f);
                        if (xferfd != -1) {
                            closedata();
                            addreply_noformat(450, MSG_DATA_WRITE_FAILED);
                            logfile(LOG_INFO, MSG_ABORTED);
                        }
                        goto end;
                    }
                }
                downloaded += (unsigned long long) w;
                left -= w;
                p += w;
# ifdef THROTTLING
                if (o < st.st_size && throttling_bandwidth_dl > 0UL) {
                    long double delay;

                    ended = get_usec_time();
                    transmitted += w;
                    delay = (transmitted / (long double) throttling_bandwidth_dl)
                        - (long double) (ended - started);
                    if (delay > (long double) MAX_THROTTLING_DELAY) {
                        started = ended;
                        transmitted = (off_t) 0;
                        delay = (long double) MAX_THROTTLING_DELAY;
                    }
                    if (delay > 0.0L) {
                        usleep2((unsigned long) (delay * 1000000.0L));
                    }
                }
# endif
            }
            skip = (off_t) 0;
            (void) munmap(buf, s);
        }
    }
#endif
    (void) close(f);
    closedata();
    displayrate(MSG_DOWNLOADED, st.st_size - restartat, started, name, 0);
    end:
        restartat = (off_t) 0;
}

void dorest(const char *name)
{
    char *endptr;
    
    restartat = (off_t) strtoull(name, &endptr, 10);
    if (*endptr != 0 || restartat < (off_t) 0) {
        restartat = 0;
        addreply(501, MSG_REST_NOT_NUMERIC "\n" MSG_REST_RESET);
    } else {
        if (type == 1 && restartat != 0) {
#ifdef STRICT_REST
            addreply_noformat(504, MSG_REST_ASCII_STRICT);
#else
            addreply(350, MSG_REST_ASCII_WORKAROUND,
                     (long long) restartat);
#endif
        } else {
            if (restartat != 0) {
                logfile(LOG_NOTICE, MSG_REST_SUCCESS, (long long) restartat);
            }
            addreply(350, MSG_REST_SUCCESS, (long long) restartat);
        }
    }
}

void domkd(char *name)
{
#ifdef QUOTAS
    Quota quota;
    int overflow;
#endif
    
    if (guest != 0 && allow_anon_mkdir == 0) {
        addreply_noformat(550, MSG_ANON_CANT_MKD);
        return;
    }
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply_noformat(550, MSG_SANITY_DIRECTORY_FAILURE);
        return;
    }
#ifdef QUOTAS
    (void) quota_update(&quota, 1LL, 0LL, &overflow);
    if (overflow != 0) {
        addreply(550, MSG_QUOTA_EXCEEDED, name);
        goto end;
    }
#endif
    if ((mkdir(name, (mode_t) (0777 & ~u_mask_d))) < 0) {
#ifdef QUOTAS
        (void) quota_update(&quota, -1LL, 0LL, &overflow);
#endif
        error(550, MSG_MKD_FAILURE);
    } else {
        addreply(257, "\"%s\" : " MSG_MKD_SUCCESS, name);
    }
#ifdef QUOTAS
    end:
    displayquota(&quota);
#endif
}

void dormd(char *name)
{
#ifdef QUOTAS
    Quota quota;
    int overflow;
#endif
    
#ifndef ANON_CAN_DELETE    
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_RMD);
        return;
    }
#endif
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply_noformat(550, MSG_SANITY_DIRECTORY_FAILURE);
        return;
    }
    if ((rmdir(name)) < 0) {
        error(550, MSG_RMD_FAILURE);
    } else {
#ifdef QUOTAS
        (void) quota_update(&quota, -1LL, 0LL, &overflow);
        displayquota(&quota);
#endif
        addreply_noformat(250, MSG_RMD_SUCCESS);
    }
}

#ifndef MINIMAL
void dofeat(void)
{
# define FEAT  "Extensions supported:" CRLF \
    " EPRT" CRLF " IDLE" CRLF " MDTM" CRLF " SIZE" CRLF \
        " REST STREAM" CRLF \
        " MLST type*;size*;sizd*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;unique*;" CRLF \
        " MLSD"
    
# ifdef WITH_TLS
#  define FEAT_TLS CRLF " AUTH TLS" CRLF " PBSZ" CRLF " PROT"
# else
#  define FEAT_TLS ""
# endif
# ifdef DEBUG
#  define FEAT_DEBUG CRLF " XDBG"
# else
#  define FEAT_DEBUG ""
# endif
# ifdef WITH_VIRTUAL_CHROOT
#  define FEAT_TVFS ""
# else
#  define FEAT_TVFS CRLF " TVFS"
# endif
# define FEAT_PASV CRLF " PASV" CRLF " EPSV" CRLF " SPSV"
    
# ifdef MINIMAL
#  define FEAT_ESTA ""
#  define FEAT_ESTP ""
# else
#  define FEAT_ESTA CRLF " ESTA"
#  define FEAT_ESTP CRLF " ESTP"
# endif
    
# ifdef WITH_RFC2640
#  define FEAT_UTF8 CRLF " UTF8"
# else
#  define FEAT_UTF8 ""
# endif
    
    char feat[] = FEAT FEAT_DEBUG FEAT_TLS FEAT_UTF8 FEAT_TVFS FEAT_ESTA FEAT_PASV FEAT_ESTP;
    
    if (disallow_passive != 0) {
        feat[sizeof FEAT FEAT_DEBUG FEAT_TLS FEAT_UTF8 FEAT_TVFS FEAT_ESTA] = 0;
    }
# ifndef MINIMAL
    else if (STORAGE_FAMILY(force_passive_ip) != 0) {
        feat[sizeof FEAT FEAT_DEBUG FEAT_TLS FEAT_UTF8 FEAT_TVFS FEAT_ESTA FEAT_PASV] = 0;
    }
# endif
    addreply_noformat(0, feat);
    addreply_noformat(211, "End.");
}
#endif

#ifndef MINIMAL
void dostou(void)
{
    char file[64];    
    static unsigned int seq = 0U;
    struct timeval tv;
    struct timezone tz;
    
    if (gettimeofday(&tv, &tz) != 0) {
        error(553, MSG_TIMESTAMP_FAILURE);
        return;
    }
    if (SNCHECK(snprintf(file, sizeof file, "pureftpd.%08lx.%02lx.%04x",
                         (unsigned long) tv.tv_sec,
                         (unsigned long) tv.tv_usec & 0xff,
                         seq), sizeof file)) {
        _EXIT(EXIT_FAILURE);
    }
    seq++;
    seq &= 0xffff;
    addreply(150, "FILE: %s", file);
    dostor(file, 0, 1);
}
#endif

static int tryautorename(const char * const atomic_file, char * const name)
{
    char name2[MAXPATHLEN];    
    unsigned int gc = 0U;
    
    if (link(atomic_file, name) == 0) {
        if (unlink(atomic_file) != 0) {
            unlink(name);
        }
        return 0;
    }
    for (;;) {
        gc++;
        if (gc == 0U ||
#ifdef AUTORENAME_REVERSE_ORDER
            SNCHECK(snprintf(name2, sizeof name2, "%u.%s", gc, name),
                    sizeof name2)
#else
            SNCHECK(snprintf(name2, sizeof name2, "%s.%u", name, gc),
                    sizeof name2)
#endif   
            ) {
            break;
        }
        if (link(atomic_file, name2) == 0) {
            if (unlink(atomic_file) != 0) {
                unlink(name2);
            }
            return 0;
        }
        switch (errno) {
#ifdef EEXIST
        case EEXIST:
#endif
#ifdef EISDIR
        case EISDIR:
#endif
#ifdef ETXTBSY
        case ETXTBSY:
#endif
            continue;
        }
        break;
    }
    return -1;
}

static char *get_atomic_file(const char * const file)
{
    static char res[MAXPATHLEN];
    char *z;
    size_t orig_len;
    size_t slash;
    size_t sizeof_atomic_prefix;
    
    if (file == NULL) {
        return res;
    }
    if ((z = strrchr(file, '/')) == NULL) {
        *res = 0;
        orig_len = (size_t) 0U;
    } else {
        slash = (size_t) (z - file);
        if (slash >= (sizeof res - (size_t) 1U)) {
            return NULL;
        }
        slash++;
        if (file[slash] == 0) {
            return NULL;
        }        
        strncpy(res, file, slash);
        res[slash] = 0;
        orig_len = strlen(res);        
    }
    sizeof_atomic_prefix = strlen(atomic_prefix) + (size_t) 1U;    
    if (sizeof res - orig_len < sizeof_atomic_prefix) {
        return NULL;
    }
    memcpy(res + orig_len, atomic_prefix, sizeof_atomic_prefix);
    
    return res;
}

void delete_atomic_file(void)
{
    const char *atomic_file;

    if ((atomic_file = get_atomic_file(NULL)) == NULL ||
        *atomic_file == 0) {
        return;
    }
    (void) unlink(atomic_file);
}

#ifdef QUOTAS
static int dostor_quota_update_close_f(const int overwrite,
                                       const off_t filesize,
                                       const off_t restartat,
                                       const char * const atomic_file,
                                       const char * const name, const int f)
{
    Quota quota;
    int overflow;
    int ret = 0;
    
    (void) quota_update(&quota, overwrite != 0 ? 0LL : 1LL,
                        (long long) (filesize - restartat), &overflow);
    if (overflow != 0) {
        addreply(550, MSG_QUOTA_EXCEEDED, name);
        /* ftruncate+unlink is overkill, but it reduces possible races */
        (void) ftruncate(f, (off_t) 0);
        (void) close(f);
        unlink(atomic_file);
        ret = -1;
    } else {
        (void) close(f);
    }
    displayquota(&quota);
    
    return ret;
}
#endif

void dostor(char *name, const int append, const int autorename)
{
    int f;
    char *p;
    const char *atomic_file = NULL;
    char *buf;    
    const size_t sizeof_buf = ul_chunk_size;
    ssize_t r;
    off_t filesize = (off_t) 0U;
    STATFS_STRUCT statfsbuf;
    struct stat st;
    double started;
#ifdef THROTTLING
    double ended;
    off_t transmitted = 0;
#endif
    int unlinkret = -1;
#ifdef QUOTAS
    signed char overwrite = 0;
    int quota_exceeded;
#endif
#ifndef WITHOUT_ASCII
    char *cpy = NULL;
#endif
    
    if ((buf = ALLOCA(sizeof_buf)) == NULL) {
        die_mem();
    }
    if (type < 1 || (type == 1 && restartat > (off_t) 1)) {
        addreply_noformat(503, MSG_NO_ASCII_RESUME);
        goto end;
    }
#ifndef ANON_CAN_RESUME
    if (guest != 0 && anon_noupload != 0) {
        addreply_noformat(553, MSG_ANON_CANT_OVERWRITE);
        goto end;
    }
#endif
    if (maxdiskusagepct > 0.0) {
        char *alloca_namedir;
        size_t name_len;
        register char *z;
        
#ifdef CHECK_SYMLINKS_DISK_SPACE
        if (STATFS(name, &statfsbuf) == 0) {
            goto okcheckspace;
        }
#endif
        name_len = strlen(name) + (size_t) 1U;
        if (name_len < (size_t) 2U || 
            (alloca_namedir = ALLOCA(name_len)) == NULL) {
            goto cantcheckspace;
        }
        memcpy(alloca_namedir, name, name_len);
        if ((z = strrchr(alloca_namedir, '/')) != NULL) {
            if (z == alloca_namedir) {
                *z++ = '.';
            }
            *z = 0;
        } else {
            alloca_namedir[0] = '.';
            alloca_namedir[1] = 0;
        }
        if (STATFS(alloca_namedir, &statfsbuf) != 0) {
            ALLOCA_FREE(alloca_namedir);
            goto cantcheckspace;
        }
        ALLOCA_FREE(alloca_namedir);
        
#ifdef CHECK_SYMLINKS_DISK_SPACE        
        okcheckspace:
#endif
        if ((double) STATFS_BLOCKS(statfsbuf) > 0.0) {
            double jam;
            
            jam = (double) STATFS_BAVAIL(statfsbuf) /
                (double) STATFS_BLOCKS(statfsbuf);
            if (jam < maxdiskusagepct) {
                addreply_noformat(553, MSG_NO_DISK_SPACE);
                goto end;
            }
        }
    }
    cantcheckspace:
    if (checknamesanity(name, dot_write_ok) != 0 ||
        (atomic_file = get_atomic_file(name)) == NULL) {
        addreply(553, MSG_SANITY_FILE_FAILURE, name);
        /* implicit : atomic_file = NULL */
        goto end;
    }
    if (restartat > (off_t) 0 || (autorename == 0 && no_truncate == 0)) {
        if (rename(name, atomic_file) != 0 && errno != ENOENT) {
            error(553, MSG_RENAME_FAILURE);
            atomic_file = NULL;
            goto end;
        }
    }
    if ((f = open(atomic_file, O_CREAT | O_WRONLY,
                  (mode_t) 0777 & ~u_mask)) == -1) {
        error(553, MSG_OPEN_FAILURE2);
        goto end;
    }
    if (fstat(f, &st) < 0) {
        (void) close(f);
        error(553, MSG_STAT_FAILURE2);
        goto end;
    }
    if (!S_ISREG(st.st_mode)) {
        (void) close(f);
        addreply_noformat(553, MSG_NOT_REGULAR_FILE);
        goto end;
    }
    /* Anonymous users *CAN* overwrite 0-bytes files - This is the right behavior */
    if (st.st_size > (off_t) 0) {
#ifndef ANON_CAN_RESUME
        if (guest != 0) {
            addreply_noformat(553, MSG_ANON_CANT_OVERWRITE);
            (void) close(f);
            goto end;
        }
#endif
        if (append != 0) {
            restartat = st.st_size;
        }
    } else {
        restartat = (off_t) 0;
    }
    if (restartat > st.st_size) {
        restartat = st.st_size;
    }
    if (restartat > (off_t) 0 && lseek(f, restartat, SEEK_SET) < (off_t) 0) {
        (void) close(f);
        error(451, "seek");
        goto end;
    }
    if (restartat < st.st_size) {
        if (ftruncate(f, restartat) < 0) {
            (void) close(f);
            error(451, "ftruncate");
            goto end;
        }
#ifdef QUOTAS
        {
            int overflow;
            Quota quota;
            
            (void) quota_update(&quota, 0LL, 
                                (long long) (restartat - st.st_size), 
                                &overflow);
        }
        overwrite++;
#endif
    }
    opendata();
    if (xferfd == -1) {
        (void) close(f);
        goto end;
    }
    doreply();
    state_needs_update = 1;
    setprocessname("pure-ftpd (UPLOAD)");
    filesize = restartat;
    
#ifdef FTPWHO
    if (shm_data_cur != NULL) {
        const size_t sl = strlen(name);
        
        ftpwho_lock();
        shm_data_cur->state = FTPWHO_STATE_UPLOAD;
        shm_data_cur->download_total_size = (off_t) 0U;
        shm_data_cur->download_current_size = (off_t) filesize;
        shm_data_cur->restartat = restartat;
        (void) time(&shm_data_cur->xfer_date);
        if (sl < sizeof shm_data_cur->filename - 1U) {
            /* no overflow, see the previous line */
            strcpy(shm_data_cur->filename, name);   /* audited - ok */
        } else {
            /* same thing here, no possible buffer overflow, keep cool */
            strcpy(shm_data_cur->filename,   /* audited - ok */
                   &name[sl - sizeof shm_data_cur->filename + 1U]);
        }
        ftpwho_unlock();
    }
#endif
#ifndef WITHOUT_ASCII
    if (type == 1) {
        if ((cpy = malloc(sizeof_buf)) == NULL) {
            goto errasc;
        }
    }
#endif
    alarm(MAX_SESSION_XFER_IDLE);    
    started = get_usec_time();    
    do {
        /* wait idletime seconds for data to be available */
        fd_set rs;
        struct timeval tv;
        
        FD_ZERO(&rs);
        FD_SET(0, &rs);
        safe_fd_set(xferfd, &rs);
        tv.tv_sec = idletime;
        tv.tv_usec = 0;        
        if (xferfd == -1 ||
            select(xferfd + 1, &rs, NULL, NULL, &tv) <= 0) {
            databroken:
            (void) close(f);
            closedata();
            if (atomic_file != NULL) {
                unlinkret = unlink(atomic_file);
                atomic_file = NULL;                
            }
            addreply_noformat(0, MSG_ABRT_ONLY);
            addreply_noformat(426, MSG_ABORTED);
            addreply(0, "%s %s", name,
                     unlinkret ? MSG_UPLOAD_PARTIAL : MSG_REMOVED);
            goto end;
        }
        if (FD_ISSET(0, &rs)) {
            char mwbuf[LINE_MAX];
            ssize_t readen;
            
            do {
                readen = read(0, mwbuf, sizeof mwbuf - (size_t) 1U);
            } while (readen == -1 && errno == EINTR);
            if (readen == -1) {
                goto databroken;
            }
            mwbuf[readen] = 0;
            if (strncasecmp(mwbuf, "ABORT",
                            sizeof "ABORT" - (size_t) 1U) == 0 ||
                strncasecmp(mwbuf, "QUIT",
                            sizeof "QUIT" - (size_t) 1U) == 0) {
                goto databroken;
            }
            if (readen > (ssize_t) 0 && mwbuf[readen - 1] == '\n') {
                addreply_noformat(202, ".");
                doreply();
            }
        }
#ifdef QUOTAS
        if ((unsigned long long) filesize > user_quota_size) {
            goto quota_exceeded;
        }
#endif
#ifdef WITH_TLS
        if (enforce_tls_auth && data_protection_level == CPL_PRIVATE) {
            r = SSL_read(tls_data_cnx, buf, sizeof_buf);
        } else
#endif
        {
            r = read(xferfd, buf, sizeof_buf);
        }
        if (r > (ssize_t) 0) {
            p = buf;
            
#ifdef THROTTLING
            if (throttling_bandwidth_ul > 0UL) {
                long double delay;
                
                ended = get_usec_time();
                transmitted += (off_t) r;
                delay = (transmitted / (long double) throttling_bandwidth_ul)
                    - (long double) (ended - started);
                if (delay > (long double) MAX_THROTTLING_DELAY) {
                    started = ended;
                    transmitted = (off_t) 0;
                    delay = (long double) MAX_THROTTLING_DELAY;
                }
                if (delay > 0.0L) {
                    usleep2((unsigned long) (delay * 1000000.0L));
                }
            }
#endif
            {
                int w;
                
#ifndef WITHOUT_ASCII
                if (type == 1) {       /* Fuckin ASCII conversion */
                    size_t asciibytes = (size_t)  0U;
                    size_t i = (size_t) 0U;
                    char *cpypnt = cpy;
                    
                    while ((ssize_t) i < r) {
                        if (p[i] != '\r') {
                            *cpypnt++ = p[i];
                            asciibytes++;
                        }
                        i++;
                    }
                    if ((w = safe_write(f, cpy, asciibytes)) == 0) {
                        filesize += (off_t) asciibytes;                 
                    }
                } else
#endif
                {
                    if ((w = safe_write(f, p, (size_t) r)) == 0) {
                        filesize += (off_t) r;
                    }
                }
                if (w < 0) {
                    errasc:
#ifdef QUOTAS
                    dostor_quota_update_close_f(overwrite, filesize,
                                                restartat, atomic_file, 
                                                name, f);
#else                        
                    (void) close(f);
#endif
                    closedata();
                    error(450, MSG_WRITE_FAILED);
                    if (guest != 0) {
                        unlinkret = unlink(atomic_file);
                        atomic_file = NULL;
                    }
                    addreply(0, "%s %s", name,
                             unlinkret ? MSG_UPLOAD_PARTIAL : MSG_REMOVED);
                    goto end;
                } 
#ifdef FTPWHO
                /* Don't update download_total_size, it'd be useless */
                if (shm_data_cur != NULL) {
                    shm_data_cur->download_current_size = filesize;
                }
#endif                    
            }
        } else if (r < 0) {
            error(451, MSG_DATA_READ_FAILED);
            goto databroken;
        }
    } while (r > (ssize_t) 0);
#ifdef SHOW_REAL_DISK_SPACE
    if (FSTATFS(f, &statfsbuf) == 0) {
        double space;
        
        space = (double) STATFS_FRSIZE(statfsbuf) *
            (double) STATFS_BAVAIL(statfsbuf);
        if (space > 524288.0) {
            addreply(0, MSG_SPACE_FREE_M, space / 1048576.0);
        } else {
            addreply(0, MSG_SPACE_FREE_K, space / 1024.0);
        }    
    }
#endif
#ifdef QUOTAS    
    quota_exceeded:
#endif
    closedata();
#ifdef QUOTAS
    quota_exceeded = 
        dostor_quota_update_close_f(overwrite, filesize, restartat,
                                    atomic_file, name, f);
#else
    (void) close(f);
#endif
    uploaded += (unsigned long long) (filesize - restartat);
#ifdef QUOTAS
    if (quota_exceeded == 0)
#endif
    {
        if (autorename != 0 && restartat == (off_t) 0) {
            if (tryautorename(atomic_file, name) != 0) {
                error(553, MSG_RENAME_FAILURE);
            } else {
                atomic_file = NULL;
            }
        } else {
            if (rename(atomic_file, name) != 0) {
                error(553, MSG_RENAME_FAILURE);
            } else {
                atomic_file = NULL;
            }
        }
        displayrate(MSG_UPLOADED, filesize - restartat, started, name, 1);
    }
    
    end:
#ifndef WITHOUT_ASCII
    free(cpy);
#endif
    ALLOCA_FREE(buf);
    restartat = (off_t) 0;
    if (atomic_file != NULL) {
        if (rename(atomic_file, name) != 0) {
            error(553, MSG_RENAME_FAILURE);
            unlink(atomic_file);            
        }
    }
}

void domdtm(const char *name)
{
    struct stat st;
    struct tm *t;

    if (!name || !*name) {
        addreply_noformat(500, MSG_MISSING_ARG);
    } else if (stat(name, &st)) {
#ifdef DEBUG
        if (debug != 0) {
            addreply(0, "arg: %s, wd: %s", name, wd);
        }
#endif
        addreply_noformat(550, MSG_STAT_FAILURE2);
    } else if (!S_ISREG(st.st_mode)) {
        addreply_noformat(550, MSG_NOT_REGULAR_FILE);
    } else {
        t = gmtime((time_t *) &(st.st_mtime));
        if (!t) {
            addreply_noformat(550, MSG_GMTIME_FAILURE);
        } else {
            addreply(213, "%04d%02d%02d%02d%02d%02d",
                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                     t->tm_hour, t->tm_min, t->tm_sec);
        }
    }
}

void dosize(const char *name)
{
    struct stat st;

    if (!name || !*name) {
        addreply_noformat(500, MSG_MISSING_ARG);
    } else if (stat(name, &st)) {
#ifdef DEBUG
        if (debug != 0) {
            addreply(0, "arg: %s, wd: %s", name, wd);
        }
#endif
        addreply_noformat(550, MSG_STAT_FAILURE2);
    } else if (!S_ISREG(st.st_mode)) {
        addreply_noformat(550, MSG_NOT_REGULAR_FILE);
    } else {
        addreply(213, "%llu", (unsigned long long) st.st_size);
    }
}

void dotype(const char *arg)
{
    replycode = 200;            /* bloody awful hack */

    if (!arg || !*arg) {
        addreply(501, MSG_MISSING_ARG "\n" "A(scii) I(mage) L(ocal)");
    } else if (tolower((unsigned char) *arg) == 'a')
        type = 1;
    else if (tolower((unsigned char) *arg) == 'i')
        type = 2;
    else if (tolower((unsigned char) *arg) == 'l') {
        if (arg[1] == '8') {
            type = 2;
        } else if (isdigit((unsigned char) arg[1])) {
            addreply_noformat(504, MSG_TYPE_8BIT_FAILURE);
        } else {
            addreply_noformat(0, MSG_MISSING_ARG);
            type = 2;
        }
    } else {
        addreply(504, MSG_TYPE_UNKNOWN ": %s", arg);
    }

    addreply(0, MSG_TYPE_SUCCESS " %s", (type > 1) ? "8-bit binary" : "ASCII");
}

void dostru(const char *arg)
{
    if (arg == NULL || !*arg) {
        addreply_noformat(500, MSG_MISSING_ARG);
    } else if (strcasecmp(arg, "F")) {
        addreply_noformat(504, MSG_STRU_FAILURE);
    } else {
        addreply_noformat(200, "F OK");
    }
}

void domode(const char *arg)
{
    if (arg == NULL || !*arg) {
        addreply_noformat(500, MSG_MISSING_ARG);
    } else if (strcasecmp(arg, "S")) {
        addreply_noformat(504, MSG_MODE_FAILURE);
    } else {
        addreply_noformat(200, "S OK");
    }
}

void dornfr(char *name)
{
    struct stat st;

#ifndef ANON_CAN_RENAME
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_RENAME);
        return;
    }
#endif
    if (disallow_rename != 0) {
        addreply_noformat(550, MSG_RENAME_FAILURE);
        return;
    }
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        return;
    }
    if ((lstat(name, &st)) == 0) {
        if (renamefrom != NULL) {
            addreply_noformat(0, MSG_RENAME_ABORT);
            (void) free(renamefrom);
        }
        if ((renamefrom = strdup(name)) == NULL) {
            die_mem();
        }
        addreply_noformat(350, MSG_RENAME_RNFR_SUCCESS);
    } else {
        addreply_noformat(550, MSG_FILE_DOESNT_EXIST);
    }
}

void dornto(char *name)
{
#ifndef ANON_CAN_RENAME
    if (guest != 0) {
        addreply_noformat(550, MSG_ANON_CANT_RENAME);
        goto bye;
    }
#endif
    if (renamefrom == NULL) {
        addreply_noformat(503, MSG_RENAME_NORNFR);
        goto bye;
    }    
    if (checknamesanity(name, dot_write_ok) != 0) {
        addreply(550, MSG_SANITY_FILE_FAILURE, name);
        return;                        /* don't clear rnfrom buffer */
    }
    /*
     * Refuse atomic replacement of the same file for users with quotas,
     * or with keepallfiles.
     */
#ifdef QUOTAS
    if (hasquota() == 0) {
        struct stat st;

        if (lstat(name, &st) == 0) {
            addreply_noformat(550, MSG_RENAME_ALREADY_THERE);
            goto bye;            
        }
    }
#endif
    /*
     * There's a race between lstat() and rename(). But exploiting it is:
     * 1) very hard
     * 2) a script kiddy can only *lose* space.
     */
    if ((rename(renamefrom, name)) < 0) {
        addreply(550, MSG_RENAME_FAILURE ": %s", strerror(errno));
    } else {
        addreply_noformat(250, MSG_RENAME_SUCCESS);
        logfile(LOG_NOTICE, MSG_RENAME_SUCCESS ": [%s]->[%s]", 
                renamefrom, name);
    }
    bye:
    (void) free(renamefrom);
    renamefrom = NULL;
}

#ifndef MINIMAL
void doopts(char *args)
{
    char *cmdopts;
        
    if ((cmdopts = strchr(args, ' ')) != NULL) {
        cmdopts++;
    }

#ifdef WITH_RFC2640
    if (strncasecmp("utf8 ", args, 5) == 0) {
        if (cmdopts == NULL) {
            addreply_noformat(501, "OPTS UTF8: " MSG_MISSING_ARG);          
        } else if ((iconv_fd_fs2utf8 == NULL || iconv_fd_utf82fs == NULL)
                   && strcasecmp(charset_fs, "utf-8") != 0) {
            addreply_noformat(500, "Disabled");
        } else if (strncasecmp(cmdopts, "on", sizeof "on" - 1U) == 0) {
            utf8 = 1;       
            addreply_noformat(200, "OK, UTF-8 enabled");
        } else if (strncasecmp(cmdopts, "off", sizeof "off" - 1U) == 0)  {
            utf8 = 0;
            addreply_noformat(200, "OK, UTF-8 disabled");
        } else {
            addreply_noformat(500, MSG_UNKNOWN_COMMAND);
        }
        return; 
    }
#endif
    if (strncasecmp("mlst ", args, 5) == 0) {
        addreply_noformat(200, " MLST OPTS "
                          "type;size;sizd;modify;UNIX.mode;UNIX.uid;"
                          "UNIX.gid;unique;");
        return;
    }   
    addreply_noformat(500, MSG_UNKNOWN_COMMAND);
}
#endif

void error(int n, const char *msg)
{
    const char *e = strerror(errno);

    logfile(LOG_ERR, "%s: %s", msg, e);
    addreply(n, "%s: %s", msg, e);
    CORK_OFF(1);
}

static void fixlimits(void)
{
#ifdef HAVE_SETRLIMIT
    static struct rlimit lim;

    lim.rlim_max = lim.rlim_cur = MAX_CPU_TIME;
    setrlimit(RLIMIT_CPU, &lim);
    lim.rlim_max = lim.rlim_cur = MAX_DATA_SIZE;
    setrlimit(RLIMIT_DATA, &lim);
# ifndef DEBUG
    lim.rlim_max = lim.rlim_cur = 0;
    setrlimit(RLIMIT_CORE, &lim);
# endif
#endif
}

#ifdef COOKIE
static void fortune(void)
{
    int fd;
    char *buf;
    char *bufpnt;
    char *bufend;
    struct stat st;
    off_t gl;
    char *fortunepnt;
    char fortune[2048];

    if (fortunes_file == NULL || *fortunes_file == 0) {
        return;
    }
    if ((fd = open(fortunes_file, O_RDONLY)) == -1) {
        logfile(LOG_ERR, MSG_OPEN_FAILURE, fortunes_file);
        return;
    }
    if (fstat(fd, &st) < 0 ||
        (((S_IRUSR | S_IRGRP | S_IROTH) & st.st_mode) !=
         (S_IRUSR | S_IRGRP | S_IROTH)) ||
        !(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) || st.st_size < 2 ||
        (buf = mmap(NULL, (size_t) st.st_size,
                PROT_READ, MAP_FILE | MAP_SHARED, fd,
                (off_t) 0)) == (char *) MAP_FAILED) {
        (void) close(fd);
        logfile(LOG_ERR, MSG_OPEN_FAILURE, fortunes_file);
        return;
    }
# ifdef HAVE_RANDOM
    gl = (off_t) (random() % (st.st_size - 1U));
# else
    gl = (off_t) (rand() % (st.st_size - 1U));    
# endif
    bufpnt = buf + gl;
    bufend = buf + st.st_size;
    while (bufpnt != buf) {
        if (bufpnt[0] == '\n') {
            if (&bufpnt[-1] != buf && bufpnt[-1] == '%') {
                if (&bufpnt[-2] != buf && bufpnt[-2] == '\n') {
                    break;
                }
            }
        }
        bufpnt--;
    }
    if (bufpnt != buf) {
        while (bufpnt != bufend && *bufpnt == '\n') {
            bufpnt++;
        }
    }
    fortunepnt = fortune;
    while (*bufpnt != 0 && bufpnt != bufend &&
           fortunepnt != &fortune[sizeof fortune - 1U]) {
        if (bufpnt[0] == '\n') {
            if (&bufpnt[1] != bufend && bufpnt[1] == '%') {
                if (&bufpnt[2] != bufend && bufpnt[2] == '\n') {
                    break;
                }
            }
        }
        *fortunepnt++ = *bufpnt++;
    }
    if (fortunepnt == fortune) {
        goto bye;
    }
    do {
        fortunepnt--;
    } while (fortunepnt != fortune && (*fortunepnt == '\n' ||
                                       isspace((unsigned char) *fortunepnt)));
    fortunepnt[1] = 0;
    fortunepnt = fortune;
    while (*fortunepnt == '\n') {
        fortunepnt++;
    }
    if (*fortunepnt == 0) {
    goto bye;
    }
    addreply_noformat(220, "<<");
    addreply(220, "%s", fortunepnt);
    addreply_noformat(220, ">>");
    bye:
    (void) munmap(buf, st.st_size);
    (void) close(fd);
}
#endif

#if !defined(NO_STANDALONE) && !defined(NO_INETD)
static int check_standalone(void)
{
    socklen_t socksize = (socklen_t) sizeof ctrlconn;

    if (getsockname(0, (struct sockaddr *) &ctrlconn, &socksize) != 0) {
        return 1;
    }
    return 0;
}
#endif

static void set_signals_client(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART;
#ifdef SIGURG
    sa.sa_handler = sigurg;
    (void) sigaction(SIGURG, &sa, NULL);
#endif
#if defined(WITH_TLS) && defined(SIGIO)
# ifndef SIGURG
    sa.sa_handler = sigurg;
# endif
    (void) sigaction(SIGIO, &sa, NULL);
#endif
    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sa, NULL);
    sa.sa_handler = SIG_DFL;
    (void) sigaction(SIGCHLD, &sa, NULL);    
#ifdef SIGFPE
    (void) sigaction(SIGFPE, &sa, NULL);
#endif
    sa.sa_flags = 0;
    sa.sa_handler = sigalarm;
    (void) sigaction(SIGALRM, &sa, NULL);
    sa.sa_handler = sigterm_client;
    (void) sigaction(SIGTERM, &sa, NULL);
    (void) sigaction(SIGHUP, &sa, NULL);
    (void) sigaction(SIGQUIT, &sa, NULL);
    (void) sigaction(SIGINT, &sa, NULL);
#ifdef SIGXCPU
    (void) sigaction(SIGXCPU, &sa, NULL);
#endif
}

static void set_signals(void)
{
#ifndef NO_STANDALONE
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sigchild;
    (void) sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sa, NULL);
    (void) sigaction(SIGALRM, &sa, NULL);
    (void) sigaction(SIGURG, &sa, NULL);
    
    sa.sa_flags = 0;
    sa.sa_handler = sigterm;
    (void) sigaction(SIGTERM, &sa, NULL);
    (void) sigaction(SIGHUP, &sa, NULL);
    (void) sigaction(SIGQUIT, &sa, NULL);
    (void) sigaction(SIGINT, &sa, NULL);
# ifdef SIGXCPU
    (void) sigaction(SIGXCPU, &sa, NULL);
# endif
#endif
}

static void dns_sanitize(char *z)
{
    while (*z != 0) {
        if ((*z >= 'a' && *z <= 'z') ||
            (*z >= '0' && *z <= '9') ||
            *z == '.' || *z == '-' ||
            (*z >= 'A' && *z <= 'Z')) {
            /* unless */
        } else {
            *z = '_';
        }
        z++;
    }
}

static void fill_atomic_prefix(void)
{
    char tmp_atomic_prefix[MAXPATHLEN];
    
    snprintf(tmp_atomic_prefix, sizeof tmp_atomic_prefix,
             "%s%lx.%x.%lx.%x",
             ATOMIC_PREFIX_PREFIX, 
             (unsigned long) session_start_time,
             (unsigned int) serverport,
             (unsigned long) getpid(),
             zrand());
    if ((atomic_prefix = strdup(tmp_atomic_prefix)) == NULL) {
        die_mem();
    }
}

static void doit(void)
{
    socklen_t socksize;

    session_start_time = time(NULL);
    fixlimits();
#ifdef F_SETOWN
    fcntl(0, F_SETOWN, getpid());
#endif
    set_signals_client();
    (void) umask((mode_t) 0);
    socksize = (socklen_t) sizeof ctrlconn;
    if (getsockname(0, (struct sockaddr *) &ctrlconn, &socksize) != 0) {
        die(421, LOG_ERR, MSG_NO_SUPERSERVER);
    }
    fourinsix(&ctrlconn);
    if (checkvalidaddr(&ctrlconn) == 0) {
        die(425, LOG_ERR, MSG_INVALID_IP);
    }
    if (STORAGE_FAMILY(ctrlconn) == AF_INET6) {
        serverport = ntohs((in_port_t) STORAGE_PORT6(ctrlconn));
    } else {
        serverport = ntohs((in_port_t) STORAGE_PORT(ctrlconn));
    }
    if (trustedip != NULL && addrcmp(&ctrlconn, trustedip) != 0) {
       anon_only = 1;
    }
    socksize = (socklen_t) sizeof peer;
    if (getpeername(0, (struct sockaddr *) &peer, &socksize)) {
        die(421, LOG_ERR, MSG_GETPEERNAME ": %s" , strerror(errno));
    }
    fourinsix(&peer);
    if (checkvalidaddr(&peer) == 0) {
        die(425, LOG_ERR, MSG_INVALID_IP);
    }
#ifndef DONT_LOG_IP
    for (;;) {
        int eai;
        
        if ((eai = getnameinfo
             ((struct sockaddr *) &peer, STORAGE_LEN(peer), host,
              sizeof host, NULL, (size_t) 0U, 
              resolve_hostnames != 0 ? 0 : NI_NUMERICHOST)) == 0) {
            break;
        }
        /* 
         * getnameinfo() is lousy on MacOS X Panther and returns EAI_NONAME
         * or EAI_SYSTEM (errno=ENOENT) when no name is found instead of
         * filling the buffer with the IP.
         */
# if defined(EAI_NONAME) && defined(EAI_SYSTEM)
        if ((eai == EAI_NONAME || eai == EAI_SYSTEM) &&
            resolve_hostnames != 0 &&
           getnameinfo
            ((struct sockaddr *) &peer, STORAGE_LEN(peer), host,
             sizeof host, NULL, (size_t) 0U, NI_NUMERICHOST) == 0) {
            break;
        }
# endif
        die(425, LOG_ERR, MSG_INVALID_IP);        
    }
#endif
#ifndef DONT_LOG_IP
    dns_sanitize(host);
#else
    *host = '?';
    host[1] = 0;
#endif
    iptropize(&peer);
    logfile(LOG_INFO, MSG_NEW_CONNECTION, host);

#ifndef NO_BANNER
# ifdef BORING_MODE
    addreply_noformat(0, MSG_WELCOME_TO " Pure-FTPd.");
# else
#  ifdef DEBUG
    addreply_noformat(0, "--------- " MSG_WELCOME_TO 
                      " Pure-FTPd " PACKAGE_VERSION VERSION_PRIVSEP VERSION_TLS " ----------");
#  else
    addreply_noformat(0, "--------- " MSG_WELCOME_TO 
                      " Pure-FTPd" VERSION_PRIVSEP VERSION_TLS " ----------");
#  endif    
# endif
#else
    addreply_noformat(220, "FTP server ready.");
#endif
    
    fill_atomic_prefix();
    
    if (maxusers > 0U) {
        unsigned int users;

#ifdef NO_STANDALONE
        users = daemons(serverport);
#else
# ifdef NO_INETD
        users = nb_children;
# else
        if (standalone) {
            users = nb_children;
        } else {
            users = daemons(serverport);
        }
# endif
#endif
        if (users > maxusers) {
            addreply(421, MSG_MAX_USERS, (unsigned long) maxusers);
            doreply();
            _EXIT(1);
        }
#ifndef NO_BANNER
        if (users > 0) {
            addreply(0, MSG_NB_USERS, users, maxusers);
        }
#endif
    }

    /* It's time to add a new entry to the ftpwho list */
#ifdef FTPWHO
    {
        ftpwho_initwho();
        if (shm_data_cur != NULL) {
            ftpwho_lock();
            shm_data_cur->pid = getpid();
            shm_data_cur->state = FTPWHO_STATE_IDLE;
            shm_data_cur->addr = peer;
            shm_data_cur->local_addr = ctrlconn;
            shm_data_cur->date = session_start_time;
            shm_data_cur->xfer_date = shm_data_cur->date;
            (shm_data_cur->account)[0] = '?';
            (shm_data_cur->account)[1] = 0;
            shm_data_cur->download_total_size = (off_t) 0;
            shm_data_cur->download_current_size = (off_t) 0;
            ftpwho_unlock();
        }
    }
#endif

#ifdef WITH_ALTLOG
    if (altlog_format != ALTLOG_NONE) {
        if (altlog_format == ALTLOG_W3C) {
            if ((altlog_fd = open(altlog_filename, 
                                  O_CREAT | O_WRONLY | O_NOFOLLOW | O_EXCL,
                                  (mode_t) 0600)) != -1) {
                altlog_write_w3c_header();
            } else if (errno == EEXIST) {
                altlog_fd = open(altlog_filename, O_WRONLY | O_NOFOLLOW);
            } 
        } else {
            altlog_fd = open(altlog_filename,
                             O_CREAT | O_WRONLY | O_NOFOLLOW, (mode_t) 0600);
        }
        if (altlog_fd == -1) {
            logfile(LOG_ERR, "altlog %s: %s", altlog_filename, strerror(errno));
        }
    }
#endif
    /* Back to the client - Get the 5 min load average */
    {
        double load_[2];
        
        if (getloadavg(load_, sizeof load_ / sizeof load_[0]) < 0) {
            load = 0.0;
        } else {
            load = load_[1];
        }
    }
#ifndef NON_ROOT_FTP
    wd[0] = '/';
    wd[1] = 0;
    if (chdir(wd)) {
        _EXIT(EXIT_FAILURE);
    }
#endif
    {
        int fodder;
#ifdef IPTOS_LOWDELAY
        fodder = IPTOS_LOWDELAY;
        setsockopt(0, SOL_IP, IP_TOS, (char *) &fodder, sizeof fodder);
        setsockopt(1, SOL_IP, IP_TOS, (char *) &fodder, sizeof fodder);        
#endif
#ifdef SO_OOBINLINE
        fodder = 1;
        setsockopt(0, SOL_SOCKET, SO_OOBINLINE, 
                   (char *) &fodder, sizeof fodder);
#endif
#ifdef TCP_NODELAY
        fodder = 1;
        setsockopt(1, IPPROTO_TCP, TCP_NODELAY,
                   (char *) &fodder, sizeof fodder);
#endif
        keepalive(0, 0);
        keepalive(1, 0);            
    }
#ifdef HAVE_SRANDOMDEV
    srandomdev();
#elif defined (HAVE_RANDOM)
    srandom((unsigned int) session_start_time ^ (unsigned int) zrand());    
#else
    srand((unsigned int) session_start_time ^ (unsigned int) zrand());
#endif
#ifdef COOKIE
    fortune();
#endif
#if !defined(NO_BANNER) && !defined(BORING_MODE)
    {
        struct tm *t;
        
        if ((t = localtime(&session_start_time)) != NULL) {
            addreply(220, MSG_WELCOME_TIME,
                     t->tm_hour, t->tm_min, (unsigned int) serverport);
        }
    }
#endif
    if (anon_only > 0) {
        addreply_noformat(220, MSG_ANONYMOUS_FTP_ONLY);
    } else if (anon_only < 0) {
        addreply_noformat(220, MSG_NO_ANONYMOUS_LOGIN);
    }
#ifndef NO_BANNER
    if (allowfxp == 2) {
        addreply_noformat(220, MSG_FXP_SUPPORT);
    }
#endif
#ifdef RATIOS
    if (ratio_upload > 0) {
        if (ratio_for_non_anon != 0) {
            addreply_noformat(0, MSG_RATIOS_EVERYONE);
        } else {
            addreply_noformat(0, MSG_RATIOS_ANONYMOUS);
        }
        addreply(0, MSG_RATIOS_RULE, ratio_download, ratio_upload);
    }
#endif
#ifndef NO_BANNER
    if (v6ready != 0 && STORAGE_FAMILY(peer) != AF_INET6) {
        addreply(0, MSG_IPV6_OK);
    }    
    if (idletime >= 120UL) {
        addreply(220, MSG_INFO_IDLE_M, idletime / 60UL);
    } else {
        addreply(220, MSG_INFO_IDLE_S, (unsigned long) idletime);
    }
#endif

    candownload = (signed char) ((maxload <= 0.0) || (load < maxload));

    if (force_passive_ip_s != NULL) {
        struct addrinfo hints, *res;
        
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_addr = NULL;
        if (getaddrinfo(force_passive_ip_s, NULL, &hints, &res) != 0 ||
            res->ai_family != AF_INET ||
            res->ai_addrlen > sizeof force_passive_ip) {
            die(421, LOG_ERR, MSG_ILLEGAL_FORCE_PASSIVE);            
        }
        memcpy(&force_passive_ip, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }
    
#ifdef WITH_PRIVSEP
    if (privsep_init() != 0) {
        die(421, LOG_ERR, "privsep_init");
    }
#endif
    
    parser();
    
    addreply(0, MSG_LOGOUT);
    logfile(LOG_INFO, MSG_LOGOUT);    
    doreply();
#ifdef WITH_BONJOUR
    refreshManager();
#endif
}

static void check_ipv6_support(void)     /* check for ipv6 support in kernel */
{
#ifndef OLD_IP_STACK
    int p;

    if ((p = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)) != -1) {
        (void) close(p);
        v6ready++;
    }
#endif
}

#ifndef NO_STANDALONE
static void updatepidfile(void)
{
    int fd;
    char buf[42];

    if (SNCHECK(snprintf(buf, sizeof buf, "%lu\n",
                         (unsigned long) getpid()), sizeof buf)) {
        return;
    }
    if (unlink(pid_file) != 0 && errno != ENOENT) {
        return;
    }
    if ((fd = open(pid_file, O_CREAT | O_WRONLY | O_TRUNC |
                   O_NOFOLLOW, (mode_t) 0644)) == -1) {
        return;
    }
    if (safe_write(fd, buf, strlen(buf)) != 0) {
        (void) ftruncate(fd, (off_t) 0);
    }
    (void) close(fd);
}

#ifndef NO_STANDALONE
static int closedesc_all(const int closestdin)
{
    int fodder;
    
    if (closestdin != 0) {
        (void) close(0);
        if ((fodder = open("/dev/null", O_RDONLY)) == -1) {
            return -1;
        }
        (void) dup2(fodder, 0);
        if (fodder > 0) {
            (void) close(fodder);
        }
    }
    if ((fodder = open("/dev/null", O_WRONLY)) == -1) {
        return -1;
    }
    (void) dup2(fodder, 1);
    (void) dup2(1, 2);
    if (fodder > 2) {
        (void) close(fodder);
    }
    
    return 0;
}

static void dodaemonize(void)
{
    pid_t child;
    unsigned int i;

    /* Contributed by Jason Lunz - also based on APUI code, see open_max() */
    if (daemonize != 0) {
        if ((child = fork()) == (pid_t) -1) {
            perror(MSG_STANDALONE_FAILED " - fork");
            logfile(LOG_ERR, MSG_STANDALONE_FAILED ": [fork: %s]", strerror(errno));
            return;
        } else if (child != (pid_t) 0) {
            _EXIT(EXIT_SUCCESS);       /* parent exits */
        }         
        if (setsid() == (pid_t) -1) {
            perror(MSG_STANDALONE_FAILED " - setsid");   /* continue anyway */
        }
# ifndef NON_ROOT_FTP
        if (chdir("/") != 0) {
            perror("chdir");
            _EXIT(EXIT_FAILURE);
        }
# endif
        i = open_max();        
        do {
            if (isatty((int) i)) {
                (void) close((int) i);
            }
            i--;
        } while (i > 2U);
        if (closedesc_all(1) != 0) {
            perror(MSG_STANDALONE_FAILED " - /dev/null duplication");
            _EXIT(EXIT_FAILURE);
        }
    }
}
#endif

static void accept_client(const int active_listen_fd) {
    sigset_t set;   
    struct sockaddr_storage sa;
    socklen_t dummy;
    pid_t child;
    int clientfd;   
    
    memset(&sa, 0, sizeof sa);
    dummy = (socklen_t) sizeof sa;  
    if ((clientfd = accept
         (active_listen_fd, (struct sockaddr *) &sa, &dummy)) == -1) {
        return;
    }
    if (STORAGE_FAMILY(sa) != AF_INET && STORAGE_FAMILY(sa) != AF_INET6) {
        (void) close(clientfd);
        return;
    }    
    if (maxusers > 0U && nb_children >= maxusers) {
        char line[1024];
        
        snprintf(line, sizeof line, "421 " MSG_MAX_USERS "\r\n",
                 (unsigned long) maxusers);
        /* No need to check a return value to say 'f*ck' */
        (void) fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL) | O_NONBLOCK);
        (void) write(clientfd, line, strlen(line));
        (void) close(clientfd);
        return;
    }
    if (maxip > 0U) {
        fourinsix(&sa);
        if (iptrack_get(&sa) >= maxip) {
            char line[1024];
            char hbuf[NI_MAXHOST];
            static struct sockaddr_storage old_sa;
            
            (void) fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL) | O_NONBLOCK);
            if (!SNCHECK(snprintf(line, sizeof line,
                                  "421 " MSG_MAX_USERS_IP "\r\n",
                                  (unsigned long) maxip), sizeof line)) {
                (void) write(clientfd, line, strlen(line));
            }
            if (addrcmp(&old_sa, &sa) != 0) {
                old_sa = sa;
                if (getnameinfo((struct sockaddr *) &sa,
                                STORAGE_LEN(sa), hbuf,
                                sizeof hbuf, NULL, (size_t) 0U,
                                NI_NUMERICHOST) == 0) {
                    logfile(LOG_WARNING, MSG_MAX_USERS_IP ": [%s]",
                            (unsigned long) maxip, hbuf);
                }
            }
            (void) close(clientfd);
            return;
        }
    }
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &set, NULL);
    nb_children++;
    child = fork();
    if (child == (pid_t) 0) {
        dup2(clientfd, 0);
        dup2(0, 1);
        if (clientfd > 1) {
            (void) close(clientfd);
        }
        if (listenfd > 1) {
            (void) close(listenfd);
        }
        if (listenfd6 > 1) {
            (void) close(listenfd6);
        }
        if (isatty(2)) {
            (void) close(2);
        }
#ifndef SAVE_DESCRIPTORS
        if (no_syslog == 0) {
            closelog();
            openlog("pure-ftpd", LOG_NDELAY | log_pid, syslog_facility);
        }
#endif
        doit();
        _EXIT(EXIT_SUCCESS);
    } else if (child == (pid_t) -1) {
        if (nb_children > 0U) {
            nb_children--;
        }
    } else {
        if (maxip > 0U) {
            iptrack_add(&sa, child);
        }
    }
    (void) close(clientfd);
    sigprocmask(SIG_UNBLOCK, &set, NULL);   
}

static void standalone_server(void)
{
    int on;
    struct addrinfo hints, *res, *res6;
    fd_set rs;
    int max_fd;

# ifndef NO_INETD
    standalone = 1;
# endif
    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_addr = NULL;
    on = 1;
    if (no_ipv4 == 0 &&
        getaddrinfo(standalone_ip, standalone_port, &hints, &res) == 0) {
        if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1 ||
            setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                       (char *) &on, sizeof on) != 0) {
            int old_errno;
            
            cant_bind:
            old_errno = errno;
            perror(MSG_STANDALONE_FAILED);
            logfile(LOG_ERR, MSG_STANDALONE_FAILED ": [%s]",
                    strerror(old_errno));
            return;
        }
        if (bind(listenfd, res->ai_addr, (socklen_t) res->ai_addrlen) != 0 ||
            listen(listenfd, maxusers > 0U ? 
                   3U + maxusers / 8U : DEFAULT_BACKLOG) != 0) {
            goto cant_bind;
        }
        freeaddrinfo(res);
        set_cloexec_flag(listenfd);     
    }
    if (v6ready != 0) {
        hints.ai_family = AF_INET6;
        if (getaddrinfo(standalone_ip, standalone_port, &hints, &res6) == 0) {
            if ((listenfd6 = socket(AF_INET6,
                                    SOCK_STREAM, IPPROTO_TCP)) == -1 ||
                setsockopt(listenfd6, SOL_SOCKET, SO_REUSEADDR,
                           (char *) &on, sizeof on) != 0) {
                goto cant_bind;
            }           
# if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
            (void) setsockopt(listenfd6, IPPROTO_IPV6, IPV6_V6ONLY,
                              (char *) &on, sizeof on);
# endif
            if (bind(listenfd6, res6->ai_addr,
                     (socklen_t) res6->ai_addrlen) != 0 ||
                listen(listenfd6, maxusers > 0U ? 
                       3U + maxusers / 8U : DEFAULT_BACKLOG) != 0) {
                goto cant_bind;
            }
            freeaddrinfo(res6);
            set_cloexec_flag(listenfd6);
        }
    }
    if (listenfd == -1 && listenfd6 == -1) {
# ifdef EADDRNOTAVAIL
        errno = EADDRNOTAVAIL;
# endif
        goto cant_bind;
    }
    updatepidfile();
    setprocessname("pure-ftpd (SERVER)");
    FD_ZERO(&rs);
    if (listenfd > listenfd6) {
        max_fd = listenfd;
    } else {
        max_fd = listenfd6;
    }
    max_fd++;
    while (stop_server == 0) {      
        safe_fd_set(listenfd, &rs);
        safe_fd_set(listenfd6, &rs);
        if (select(max_fd, &rs, NULL, NULL, NULL) <= 0) {
            if (errno != EINTR) {
                (void) sleep(1);
            }
            continue;
        }
        if (safe_fd_isset(listenfd, &rs)) {
            accept_client(listenfd);
        } 
        if (safe_fd_isset(listenfd6, &rs)) {
            accept_client(listenfd6);
        }
    }
}
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
static struct passwd *fakegetpwnam(const char * const name)
{
    static struct passwd pwd;
        
    (void) name;
    pwd.pw_name = pwd.pw_gecos = pwd.pw_shell = "ftp";
    pwd.pw_passwd = "*";
    pwd.pw_uid = (uid_t) 42U;
    pwd.pw_gid = (gid_t) 42U;
    pwd.pw_dir = WIN32_ANON_DIR;
    
    return &pwd;
}
#endif

int main(int argc, char *argv[])
{
#ifndef NO_GETOPT_LONG
    int option_index = 0;
#endif
    int fodder;
    int bypass_ipv6 = 0;
    struct passwd *pw;

#ifdef PROBE_RANDOM_AT_RUNTIME
    pw_zrand_probe();
#endif    
    
#ifdef HAVE_GETPAGESIZE
    page_size = (size_t) getpagesize();
#elif defined(_SC_PAGESIZE)
    page_size = (size_t) sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
    page_size = (size_t) sysconf(_SC_PAGE_SIZE);
#else
    page_size = (size_t) 4096U;
#endif
    map_size = (size_t) DEFAULT_DL_CHUNK_SIZE & ~(page_size - 1);
    dl_chunk_size = map_size;
    ul_chunk_size = DEFAULT_UL_CHUNK_SIZE;

#ifdef HAVE_SETLOCALE
# ifdef LC_MESSAGES
    (void) setlocale(LC_MESSAGES, MESSAGES_LOCALE);
# endif
# ifdef LC_CTYPE
    (void) setlocale(LC_CTYPE, "");
# endif
# ifdef LC_COLLATE
    (void) setlocale(LC_COLLATE, "");
# endif
#endif    
    
    init_tz();
    
#ifndef SAVE_DESCRIPTORS
    openlog("pure-ftpd", LOG_NDELAY | log_pid, DEFAULT_FACILITY);
#endif

#ifdef USE_CAPABILITIES
    set_initial_caps();
#endif
    set_signals();

    loggedin = 0;

#ifdef BANNER_ENVIRON
# ifdef COOKIE
    {
        const char *a;
        
        if ((a = getenv("BANNER")) != NULL && *a != 0) {
            fortunes_file = strdup(a);
        }
    }
# endif
#endif
    
    while ((fodder =
#ifndef NO_GETOPT_LONG
            getopt_long(argc, argv, GETOPT_OPTIONS, long_options, &option_index)
#else
            getopt(argc, argv, GETOPT_OPTIONS)
#endif
            ) != -1) {
        switch (fodder) {
        case 's': {
            if ((pw = getpwnam("ftp"))) {
                warez = pw->pw_uid;
            } else {
                logfile(LOG_ERR, MSG_NO_FTP_ACCOUNT);
            }
            break;
        }
        case '0': {
            no_truncate = 1;
            break;
        }
        case '4': {
            bypass_ipv6 = 1;
            break;
        }            
        case '6': {
            no_ipv4 = 1;
            break;
        }
#ifdef WITH_RFC2640
        case '8': {
            if ((charset_fs = strdup(optarg)) == NULL) {
                die_mem();
            }
            break;
        }
        case '9': {
            if ((charset_client = strdup(optarg)) == NULL) {
                die_mem();
            }
            break;
        }
#endif 
        case '1': {
            log_pid = LOG_PID;
            break;
        }
#ifndef NO_STANDALONE
        case 'S': {
            char *struck;

            if ((struck = strchr(optarg, ',')) != NULL) {
                *struck = 0;
                if (*optarg != 0) {
                    if ((standalone_ip = strdup(optarg)) == NULL) {
                        die_mem();
                    }
                }
                if (struck[1] != 0) {
                    if ((standalone_port = strdup(struck + 1)) == NULL) {
                        die_mem();
                    }
                }
            } else {
                if ((standalone_port = strdup(optarg)) == NULL) {
                    die_mem();
                }
            }
            break;
        }
#endif
        case 'D': {
            force_ls_a = 1;
            break;
        }
#ifdef THROTTLING
        case 't':
        case 'T': {
            char *struck;
            const char *tr_bw_ul = NULL;
            const char *tr_bw_dl = NULL;

            if ((struck = strchr(optarg, ':')) != NULL) {
                *struck = 0;
                if (*optarg != 0) {
                    tr_bw_ul = optarg;
                }
                if (struck[1] != 0) {
                    tr_bw_dl = &struck[1];
                }
            } else {
                tr_bw_ul = tr_bw_dl = optarg;
            }
            if ((tr_bw_ul == NULL || *tr_bw_ul == 0) &&
                (tr_bw_dl == NULL || *tr_bw_dl == 0)) {
                bad_bw:
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_THROTTLING ": %s" , optarg);
            }
            if (tr_bw_dl != NULL) {
                if ((throttling_bandwidth_dl =
                     strtoul(tr_bw_dl, NULL, 0) * 1024UL) == 0UL) {
                    goto bad_bw;
                }
                dl_chunk_size = throttling_bandwidth_dl & ~(page_size - 1);
                if (dl_chunk_size < page_size) {
                    dl_chunk_size = page_size;
                }
            }
            if (tr_bw_ul != NULL) {
                if ((throttling_bandwidth_ul =
                     strtoul(tr_bw_ul, NULL, 0) * 1024UL) == 0UL) {
                    goto bad_bw;
                }
                ul_chunk_size = throttling_bandwidth_ul;
                if (ul_chunk_size > MAX_UL_CHUNK_SIZE) {
                    ul_chunk_size = MAX_UL_CHUNK_SIZE;
                }
            }
            throttling_delay = 1000000 /
                (throttling_bandwidth_dl | throttling_bandwidth_ul);
            if (fodder == 't') {
                throttling = 1;
            } else {
                throttling = 2;
            }
            break;
        }
#endif
        case 'a': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            chroot_trustedgid = strtoul(nptr, &endptr, 0);
            if (!nptr || !*nptr || !endptr || *endptr) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_TRUSTED_GID ": %s" , optarg);
            }
            userchroot = 1;
            break;
        }
        case 'x': {
            dot_write_ok = 0;
            break;
        }
        case 'X': {
            dot_write_ok = dot_read_ok = 0;
            break;
        }
        case 'z': {
            dot_read_anon_ok = 1;
            break;
        }
        case 'Z': {
            be_customer_proof = 1;
            break;
        }
        case 'A': {
            userchroot = 2;
            break;
        }
        case 'w': {
            allowfxp = 1;
            break;
        }
        case 'W': {
            allowfxp = 2;
            break;
        }
        case 'd': {
            if (logging < 2) {
                logging++;
            }
            break;
        }
        case 'b': {
            broken_client_compat = 1;
            break;
        }
        case 'c': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            maxusers = (unsigned int) strtoul(nptr, &endptr, 0);
            if (!nptr || !*nptr || !endptr || *endptr || !maxusers) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_USER_LIMIT ": %s" , optarg);
            }
            break;
        }
#ifndef NO_STANDALONE
        case 'B': {
            daemonize = 1;
            break;
        }
        case 'C': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            maxip = (unsigned int) strtoul(nptr, &endptr, 0);
            if (!nptr || !*nptr || !endptr || *endptr || !maxip) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_USER_LIMIT ": %s" , optarg);
            }
            break;
        }
#endif
#ifdef PER_USER_LIMITS
        case 'y': {
            int ret;

            ret = sscanf(optarg, "%u:%u", &per_user_max, &per_anon_max);
            if (ret != 2) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_USER_LIMIT ": %s" , optarg);
            }
            break;      
        }       
#endif
#ifdef WITH_TLS
        case 'Y': {            
            if ((enforce_tls_auth = atoi(optarg)) < 0 || enforce_tls_auth > 3) {
                die(421, LOG_ERR, MSG_CONF_ERR ": TLS");
            }
            break;
        }            
#endif
        case 'e': {
            anon_only = 1;
            break;
        }
        case 'E': {
            anon_only = -1;
            break;
        }
#ifdef COOKIE
        case 'F': {
# ifdef BANNER_ENVIRON
            free(fortunes_file);
# endif
            fortunes_file = strdup(optarg);
            break;
        }
#endif
        case 'f': {
            int n = 0;

            if (strcasecmp(optarg, "none") == 0) {
                no_syslog = 1;
                break;
            }
            while (facilitynames[n].c_name &&
                   strcasecmp(facilitynames[n].c_name, optarg) != 0) {
                n++;
            }
            if (facilitynames[n].c_name) {
                syslog_facility = facilitynames[n].c_val;
            } else {
                logfile(LOG_ERR, 
                        MSG_CONF_ERR ": " MSG_ILLEGAL_FACILITY ": %s", optarg);
            }
            break;
        }
        case 'l': {
            const Authentication *auth_list_pnt = auth_list;
            const char *opt = optarg;
            Authentications *new_auth;
            size_t auth_name_len;

            for (;;) {
                auth_name_len = strlen(auth_list_pnt->name);
                if (strncasecmp(opt, auth_list_pnt->name,
                                auth_name_len) == 0) {
                    char *file = NULL;

                    opt += auth_name_len;
                    if (*opt == ':') {
                        opt++;
                        if (*opt != 0) {
                            if ((file = strdup(opt)) == NULL) {
                                die_mem();
                            }
                        }
                    }
                    if (auth_list_pnt->parse != NULL) {
                        auth_list_pnt->parse(file);
                    }
                    if ((new_auth = malloc(sizeof *new_auth)) == NULL) {
                        die_mem();
                    }
                    new_auth->auth = auth_list_pnt;
                    new_auth->conf_file = file;
                    new_auth->next = NULL;
                    if (last_authentications == NULL) {
                        first_authentications = new_auth;
                    } else {
                        last_authentications->next = new_auth;
                    }
                    last_authentications = new_auth;

                    break;
                }
                auth_list_pnt++;
                if (auth_list_pnt->name == NULL) {
                    die(421, LOG_ERR, MSG_AUTH_UNKNOWN ": %s", opt);
                }
            }

            break;
        }
        case 'm': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            maxload = strtod(nptr, &endptr);
            if (!nptr || !*nptr || !endptr || *endptr || maxload <= 0.0) {
                die(421, LOG_ERR, MSG_CONF_ERR ": "
                    MSG_ILLEGAL_LOAD_LIMIT ": %s" , optarg);
            }
            break;
        }
        case 'M': {
            allow_anon_mkdir = 1;
            break;
        }
        case 'N': {
            disallow_passive = 1;
            break;
        }
#if defined(WITH_UPLOAD_SCRIPT)
        case 'o': {
            do_upload_script = 1;
            break;
        }
#endif
#ifdef WITH_ALTLOG
        case 'O': {
            char *optarg_copy;
            char *delpoint;

            if ((optarg_copy = strdup(optarg)) == NULL) {
                die_mem();
            }
            if ((delpoint = strchr(optarg_copy, ALTLOG_DELIMITER)) == NULL) {
                altlog_format = ALTLOG_DEFAULT;
                delpoint = optarg_copy;
            } else {
                const AltLogPrefixes *altlogprefixes_pnt = altlogprefixes;

                *delpoint++ = 0;
                do {
                    if (strcasecmp(optarg_copy,
                                   altlogprefixes_pnt->prefix) == 0) {
                        altlog_format = altlogprefixes_pnt->format;
                        break;
                    }
                    altlogprefixes_pnt++;
                } while (altlogprefixes_pnt->prefix != NULL);
                if (altlog_format == ALTLOG_NONE) {
                    die(421, LOG_ERR,
                        MSG_CONF_ERR ": " MSG_UNKNOWN_ALTLOG ": %s",
                        optarg_copy);
                }
            }
            if (*delpoint != '/') {
                die(421, LOG_ERR,
                    MSG_CONF_ERR ": " MSG_SANITY_FILE_FAILURE,
                    delpoint);
            }
            if ((altlog_filename = strdup(delpoint)) == NULL) {
                die_mem();
            }
            (void) free(optarg_copy);
            break;
        }
#endif
        case 'p': {
            int ret;

            ret = sscanf(optarg, "%u:%u", &firstport, &lastport);
            if (ret != 2 ||
                firstport < 1024U || lastport > 65535U
                || lastport < firstport) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_PORTS_RANGE ": %s" , optarg);
            }
            break;
        }
        case 'L': {
            int ret;

            ret = sscanf(optarg, "%u:%u", &max_ls_files, &max_ls_depth);
            if (ret != 2 ||
                max_ls_files < 1U || max_ls_depth < 1U) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_LS_LIMITS ": %s" , optarg);
            }
            break;
        }
#ifdef QUOTAS
        case 'n': {
            int ret;

            ret = sscanf(optarg, "%llu:%llu",
                         &user_quota_files, &user_quota_size);
            if (ret != 2) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_QUOTA ": %s" , optarg);
            }
            user_quota_size *= (1024ULL * 1024ULL);
            break;
        }
#endif
        case 'P': {
            if ((force_passive_ip_s = strdup(optarg)) == NULL) {
                die_mem();
            }
            break;
        }
#ifdef RATIOS
        case 'q':
        case 'Q': {
            int ret;

            ret = sscanf(optarg, "%u:%u", &ratio_upload, &ratio_download);
            if (ret != 2 ||
                ratio_upload < 1U || ratio_download < 1U) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_RATIO ": %s" , optarg);
            }
            if (fodder == 'Q') {
                ratio_for_non_anon = 1;
            }
            break;
        }
#endif
        case 'r': {
            autorename = 1;
            break;
        }
        case 'R': {
            nochmod = 1;
            break;
        }
        case 'K': {
            keepallfiles = 1;
            break;
        }
#ifndef NO_STANDALONE
        case 'g': {
            if ((pid_file = strdup(optarg)) == NULL) {
                die_mem();
            }
            break;
        }
#endif
        case 'G': {
            disallow_rename = 1;
            break;
        }            
        case 'H': {
            resolve_hostnames = 0;
            break;
        }
        case 'I': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            idletime = strtoul(nptr, &endptr, 0) * 60UL;
            if (idletime <= 0) {
                idletime = DEFAULT_IDLE;
            }
            break;
        }
        case 'i': {
            anon_noupload = 1;
            break;
        }
        case 'j': {
            create_home = 1;
            break;
        }
        case 'k': {
            const char *nptr;
            char *endptr;

            nptr = optarg;
            endptr = NULL;
            maxdiskusagepct = 1.0 - (strtod(nptr, &endptr) / 100.0);
            if (maxdiskusagepct >= 1.0 || maxdiskusagepct < 0.0) {
                maxdiskusagepct = 0.0;
            }
            break;
        }
        case 'u': {
            const char *nptr;
            char *endptr;
            long tmp;

            nptr = optarg;
            endptr = NULL;
            tmp = strtol(nptr, &endptr, 10);
            if (!nptr || !*nptr || !endptr || *endptr || tmp < 0) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_UID_LIMIT ": %s" , optarg);
            }
            useruid = (uid_t) tmp;
            break;
        }
        case 'U': {
            char *optarg_copy;
            char *struck;
            const char *tr_umask = NULL;
            const char *tr_umask_d = NULL;

            if ((optarg_copy = strdup(optarg)) == NULL) {
                die_mem();
            }
            if ((struck = strchr(optarg_copy, ':')) != NULL) {
                *struck = 0;
                if (*optarg_copy != 0) {
                    tr_umask = optarg_copy;
                }
                if (struck[1] != 0) {
                    tr_umask_d = &struck[1];
                }
            } else {
                tr_umask = tr_umask_d = optarg_copy;
            }
            if ((tr_umask == NULL || *tr_umask == 0) &&
                (tr_umask_d == NULL || *tr_umask_d == 0)) {
                bad_umask:
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_UMASK ": %s",
                    optarg_copy);
            }
            if (tr_umask != NULL) {
                if ((u_mask =
                     strtoul(tr_umask, NULL, 8)) > 0777) {
                    goto bad_umask;
                }
            }
            if (tr_umask_d != NULL) {
                if ((u_mask_d =
                     strtoul(tr_umask_d, NULL, 8)) > 0777) {
                    goto bad_umask;
                }
            }
            (void) free(optarg_copy);
            break;
        }
#ifdef WITH_VIRTUAL_HOSTS
        case 'V': {
            if ((trustedip = malloc(sizeof *trustedip)) == NULL) {
                die_mem();
            }
            if (generic_aton(optarg, trustedip) != 0) {
                die(421, LOG_ERR, MSG_CONF_ERR ": " MSG_ILLEGAL_TRUSTED_IP);
            }
            break;
        }
#endif
#ifdef WITH_BONJOUR
        case 'v': {
            char *rdvname;
            char *end;
            
            if ((rdvname = strdup(optarg)) == NULL) {
                die_mem();
            }
            doregistration(rdvname, strtoul(standalone_port, &end, 10));
            break;
        }
#endif
        case 'h': {
#ifndef NON_ROOT_FTP
            if (geteuid() == (uid_t) 0)
#endif
            {
                puts(PACKAGE " v" VERSION VERSION_PRIVSEP "\n");
            }
#ifndef NO_GETOPT_LONG
            {
                const struct option *options = long_options;

                do {
                    printf("-%c\t--%s\t%s\n", options->val, options->name,
                           options->has_arg ? "<opt>" : "");
                    options++;
                } while (options->name != NULL);
            }
#endif
            exit(EXIT_SUCCESS);
        }
        default:
            logfile(LOG_WARNING, MSG_ILLEGAL_OPTION);
        }
    }
#ifdef WITH_RFC2640
    if (charset_fs == NULL) {
        charset_fs = (char *) "utf-8";
    }
    if (charset_client == NULL) {
        charset_client = (char *) "utf-8";
    }
    if (strcasecmp(charset_fs, charset_client) != 0) {
        if ((iconv_fd_fs2client = iconv_open(charset_client, charset_fs))
            == (iconv_t) -1) {
            die(421, LOG_ERR,
                MSG_CONF_ERR ": " MSG_ILLEGAL_CHARSET ": %s/%s",
                charset_fs, charset_client);
        }
        if ((iconv_fd_client2fs = iconv_open(charset_fs, charset_client))
            == (iconv_t) -1) {
            die(421, LOG_ERR,
                MSG_CONF_ERR ": " MSG_ILLEGAL_CHARSET ": %s/%s",
                charset_client, charset_fs);
        }
    }
   if (strcasecmp(charset_fs, "utf-8") != 0) {
       if ((iconv_fd_fs2utf8 = iconv_open("utf-8", charset_fs))
           == (iconv_t) -1) {
            die(421, LOG_ERR,
                MSG_CONF_ERR ": " MSG_ILLEGAL_CHARSET ": %s/utf-8",
                charset_fs);
       }
       if ((iconv_fd_utf82fs = iconv_open(charset_fs, "utf-8"))
           == (iconv_t) -1) {
            die(421, LOG_ERR,
                MSG_CONF_ERR ": " MSG_ILLEGAL_CHARSET ": utf-8/%s",
                charset_fs);
       }
   }
#endif

    if (first_authentications == NULL) {
        if ((first_authentications =
             malloc(sizeof *first_authentications)) == NULL) {
            die_mem();
        }
        first_authentications->auth = DEFAULT_AUTHENTICATION;
        first_authentications->conf_file = NULL;
        first_authentications->next = NULL;
    }
#ifndef NO_STANDALONE
    dodaemonize();
#endif
#ifndef SAVE_DESCRIPTORS
    if (no_syslog == 0 && (log_pid || syslog_facility != DEFAULT_FACILITY)) {
        closelog();
        openlog("pure-ftpd", LOG_NDELAY | log_pid, syslog_facility);
    }
#endif
    (void) umask((mode_t) 0);
    clearargs(argc, argv);
    idletime_noop = (double) idletime * 2.0;
    if (firstport) {
        unsigned int portmax;

        portmax = (lastport - firstport + 1) / 2;
        if (!maxusers || maxusers > portmax) {
            maxusers = portmax;    /* ... so we don't run out of ports */
        }
    }
    if (bypass_ipv6 == 0) {
        check_ipv6_support();
    }
#if defined(WITH_UPLOAD_SCRIPT)
    if (do_upload_script != 0) {
        upload_pipe_open();
    }
#endif
#ifdef WITH_DIRALIASES
    if (init_aliases() != 0) {
        logfile(LOG_ERR, MSG_ALIASES_BROKEN_FILE);
    }
#endif
#ifdef WITH_TLS
    if (enforce_tls_auth > 0) {
        (void) tls_init_library();
    }
#endif
#if !defined(NO_STANDALONE) && !defined(NO_INETD)
    if (check_standalone() != 0) {
        standalone_server();
    } else {
        doit();
    }
#elif !defined(NO_STANDALONE) && defined(NO_INETD)
    standalone_server();
#elif defined(NO_STANDALONE) && !defined(NO_INETD)
    doit();
#else
# error Configuration error
#endif

#ifdef WITH_UPLOAD_SCRIPT
    upload_pipe_close();
#endif
    {
        Authentications *auth_scan = first_authentications;
        Authentications *previous;

        while (auth_scan != NULL) {
            if (auth_scan->auth->exit != NULL) {
                auth_scan->auth->exit();
            }
            free(auth_scan->conf_file);
            previous = auth_scan;
            auth_scan = auth_scan->next;
            free(previous);
        }
    }
    free(trustedip);
#ifdef WITH_RFC2640
    if (iconv_fd_fs2client != NULL) {
        iconv_close(iconv_fd_fs2client);
    }
    if (iconv_fd_fs2utf8 != NULL) {
        iconv_close(iconv_fd_fs2utf8);
    }
    if (iconv_fd_client2fs != NULL) {
        iconv_close(iconv_fd_client2fs);
    }
    if (iconv_fd_utf82fs != NULL) {
        iconv_close(iconv_fd_utf82fs);
    }
#endif
#ifndef NO_STANDALONE
    iptrack_free();    
    unlink(pid_file);
#endif
    closelog();
#ifdef WITH_TLS
    tls_free_library();
#endif
#ifdef FTPWHO
    _EXIT(EXIT_SUCCESS);
#endif

    return 0;
}
