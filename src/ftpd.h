#ifndef __FTPD_H__
#define __FTPD_H__ 1

#ifndef __GNUC__
# ifdef __attribute__
#  undef __attribute__
# endif
# define __attribute__(a)
#endif

#if defined(SYSTEM_QUOTAS) || defined(WITH_PRIVSEP)
# ifdef HAVE_SYS_FSUID_H
#  undef HAVE_SYS_FSUID_H
# endif
#endif

#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
# include <stdarg.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif
#ifdef HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#else
# if HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#elif defined(HAVE_SYS_FCNTL_H)
# include <sys/fcntl.h>
#endif
#ifdef HAVE_IOCTL_H
# include <ioctl.h>
#elif defined(HAVE_SYS_IOCTL_H)
# include <sys/ioctl.h>
#endif
#include <sys/socket.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
# include <sys/mount.h>
#endif
#include <pwd.h>
#include <grp.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SYS_VFS_H
# ifndef STATFS_ALREADY_DEFINED
#  include <sys/vfs.h>
# endif
#endif
#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif
#ifdef HAVE_SYS_LOADAVG_H
# include <sys/loadavg.h>
#endif
#ifdef HAVE_SYS_SYSMP_H
# include <sys/sysmp.h>
#endif
#ifdef HAVE_SYS_SYSGET_H
# include <sys/sysget.h>
#endif
#ifdef HAVE_UTIME_H
# include <utime.h>
#endif
#ifdef WITH_RFC2640
# include <iconv.h>
#endif

#include "mysnprintf.h"

#if !defined(HAVE_GETADDRINFO) || !defined(HAVE_GETNAMEINFO) || (!defined(HAVE_INET_NTOP) && !defined(inet_ntop)) || (!defined(HAVE_INET_PTON) && !defined(inet_pton))
# define OLD_IP_STACK 1
#endif

#include "ipstack.h"

/* We can't have more than one implementation, so if more than one were
 * found, the configure test failed - Use none */
#ifdef VIRTUOZZO
# ifndef DISABLE_SENDFILE
#  define DISABLE_SENDFILE 1
# endif
#endif
/* Solaris now supports both sendfilev() and Linux-like sendfile().
 * Prefer sendfile() if available. */
#if defined(SENDFILEV_SOLARIS) && defined(SENDFILE_LINUX)
# undef SENDFILEV_SOLARIS
#endif
#if defined(DISABLE_SENDFILE) || \
  (defined(SENDFILE_FREEBSD) && (defined(SENDFILE_LINUX) || defined(SENDFILEV_SOLARIS) || defined(SENDFILE_HPUX))) || \
  (defined(SENDFILE_LINUX) && (defined(SENDFILEV_SOLARIS) || defined(SENDFILE_HPUX))) || \
  (defined(SENDFILEV_SOLARIS) && defined(SENDFILE_HPUX))
# undef SENDFILE_FREEBSD
# undef SENDFILE_LINUX
# undef SENDFILE64_LINUX
# undef SENDFILE_HPUX
# undef SENDFILEV_SOLARIS
#endif
#if defined(SENDFILE_FREEBSD) || defined(SENDFILEV_SOLARIS) || \
    defined(SENDFILE_HPUX) || \
    (defined(SENDFILE_LINUX) && defined(SENDFILE64_LINUX))
/* Old Linux kernels/glibcs that can't handle 64-bits sendfile() aren't
 * supported any more. */
#else
# define SENDFILE_NONE
#endif

#ifdef HAVE_SYS_UIO_H
# include <sys/uio.h>
#endif

/*
 * sendfile() is very kernel dependant. It's probable that you have platforms
 * that require specific #include before sys/sendfile.h . So to enhance
 * portability, we only include sys/sendfile.h on operating systems known
 * to be supported
 */
#if (defined(SENDFILE_LINUX) || defined(SENDFILEV_SOLARIS)) && defined(HAVE_SYS_SENDFILE_H)
# include <sys/sendfile.h>
#endif

#ifdef HAVE_SYS_FSUID_H
# include <sys/fsuid.h>
#endif
#ifdef HAVE_ALLOCA
# ifdef HAVE_ALLOCA_H
#  include <alloca.h>
# endif
# define ALLOCA(X) alloca(X)
# define ALLOCA_FREE(X) do { } while (0)
#else
# define ALLOCA(X) malloc(X)
# define ALLOCA_FREE(X) free(X)
#endif

#ifdef __FTPD_P_H__
# define SYSLOG_NAMES    1        /* for -f */
#endif
#include <syslog.h>
#ifndef HAVE_SYSLOG_NAMES
# include "syslognames.h"
#endif

#ifdef HAVE_CRYPT_H
# include <crypt.h>
#endif

#ifdef USE_SHADOW
# ifdef HAVE_SHADOW_H
#  include <shadow.h>
# endif
#endif

#if defined(__svr4__) && defined(__sun__) /* Solaris 2 aka SunOS 5 */
# include <kvm.h>
# include <nlist.h>
#endif

#ifdef HAVE_SYS_PSTAT_H
# include <sys/pstat.h>
#endif

#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif

#ifndef O_DIRECTORY
# define O_DIRECTORY 0
#endif

#ifndef MAP_FILE
# define MAP_FILE 0
#endif

/*
 * It's a hell. Some OS (Linux, BSD) have statfs.
 * Other OS (Solaris, Irix) have statvfs and statvfs64.
 * They do the same thing, but with a different syntax.
 */

#if defined(HAVE_STATVFS) || defined(HAVE_STATVFS64)
# ifdef HAVE_STATVFS64
#  define STATFS_STRUCT struct statvfs64
#  define STATFS(PATH, STR) statvfs64(PATH, STR)
#  define FSTATFS(FD, STR) fstatvfs64(FD, STR)
#  define STATFS_TYPE 1
# else
#  define STATFS_STRUCT struct statvfs
#  define STATFS(PATH, STR) statvfs(PATH, STR)
#  define FSTATFS(FD, STR) fstatvfs(FD, STR)
#  define STATFS_TYPE 2
# endif
# define STATFS_BAVAIL(X) ((X).f_bavail)
# define STATFS_BLOCKS(X) ((X).f_blocks)
# define STATFS_BSIZE(X) ((X).f_bsize)
# define STATFS_FRSIZE(X) ((X).f_frsize)
#elif defined(HAVE_STATFS)
# define STATFS_STRUCT struct statfs
# define STATFS(PATH, STR) statfs(PATH, STR)
# define FSTATFS(FD, STR) fstatfs(FD, STR)
# define STATFS_TYPE 3
# define STATFS_BAVAIL(X) ((X).f_bavail)
# define STATFS_BLOCKS(X) ((X).f_blocks)
# define STATFS_FRSIZE(X) ((X).f_bsize)
# define STATFS_BSIZE(X) ((X).f_bsize)
#else
# define STATFS_STRUCT int
# define STATFS(PATH, STR) (-1)
# define FSTATFS(FD, STR) (-1)
# define STATFS_TYPE -1
# define STATFS_BAVAIL(X) (0)
# define STATFS_BLOCKS(X) (0)
# define STATFS_BSIZE(X) (0)
# define STATFS_FRSIZE(X) (0)
#endif

#ifndef errno
extern int errno;
#endif
#ifndef environ
extern char **environ;
#endif

typedef struct AuthResult_ {
    int auth_ok;                       /* 0=no auth/login not found,1=ok,-1=auth failed */
    uid_t uid;
    gid_t gid;
    const char *dir;
    int slow_tilde_expansion;
#ifdef THROTTLING
    unsigned long throttling_bandwidth_ul;
    unsigned long throttling_bandwidth_dl;
    int throttling_ul_changed;
    int throttling_dl_changed;    
#endif
#ifdef QUOTAS
    unsigned long long user_quota_size;
    unsigned long long user_quota_files;
    int quota_size_changed;
    int quota_files_changed;
#endif
#ifdef RATIOS
    unsigned int ratio_upload;
    unsigned int ratio_download;
    int ratio_ul_changed;
    int ratio_dl_changed;
#endif
#ifdef PER_USER_LIMITS
    unsigned int per_user_max;
#endif
} AuthResult;

typedef struct PureFileInfo_ {
    char **names_pnt;
    size_t name_offset;
    off_t size;
    time_t mtime;
    mode_t mode;
    nlink_t nlink;
    uid_t uid;
    gid_t gid;
} PureFileInfo;

#define FI_NAME(X) (*((X)->names_pnt) + (X)->name_offset)

typedef enum {
    ALTLOG_NONE, ALTLOG_CLF, ALTLOG_STATS, ALTLOG_W3C, ALTLOG_XFERLOG
} AltLogFormat;

typedef struct AltLogPrefixes_ {
    const char *prefix;
    const AltLogFormat format;
} AltLogPrefixes;

typedef enum {
    CPL_NONE, CPL_CLEAR, CPL_SAFE, CPL_CONFIDENTIAL, CPL_PRIVATE
} ChannelProtectionLevel;

int safe_write(const int fd, const void *buf_, size_t count);
#ifdef WITH_TLS
int secure_safe_write(const void *buf_, size_t count);
#endif
void *aborttransfer(int);
void parser(void);
void stripctrl(char * const buf, size_t len);
void dobanner(const int type);
void douser(const char *name);
void dopass(char *password);
void docwd(const char *dir);
void doretr(char *name);
void dorest(const char *name);
void dodele(char *name);
void dostor(char *name, const int append, const int autorename);
void domkd(char *name);
void dormd(char *name);
void domdtm(const char *name);
void dosize(const char *name);
void doeprt(char *p);
void doport(const char *arg);
void doport2(struct sockaddr_storage a, unsigned int p);
#ifndef MINIMAL
void doesta(void);
void doestp(void);
#endif
void dopasv(int);
void doopts(char *args);
void dochmod(char *name, mode_t mode);
void doutime(char *name, const char * const wanted_time);
void error(int n, const char *msg);
void domode(const char *arg);
void dostru(const char *arg);
void dotype(const char *arg);
void donoop(void);
void dornfr(char *name);
void dornto(char *name);
void dostou(void);
void dofeat(void);
void domlst(const char * const file);
void dositetime(void);
void mappedtov4(struct sockaddr_storage *ss);
#ifndef HAVE_SYS_FSUID_H
void disablesignals(void);
#endif
void getnames(void);
void donlist(char *arg, const int on_ctrlconn, const int opt_l_,
             const int split_args);
void opendata(void);
void closedata(void);
void addreply(const int code, const char * const line, ...)
    __attribute__ ((format(printf, 2, 3)));
void addreply_noformat(const int code, const char * const line);
void doreply(void);
void sighandler(int sig);
void prevent(char *arg);
unsigned int daemons(in_port_t server_port);
void logfile(const int facility, const char *format, ...) 
    __attribute__ ((format(printf, 2, 3)));
void die(const int err, const int priority, const char * const format, ...)
    __attribute__ ((format(printf, 3, 4)));
void die_mem(void);
void setprocessname(const char * const title);
int modernformat(const char *file, char *target, size_t target_size,
                 const char * const prefix);
int sfgets(void);
const char *getgroup(const gid_t gid);
const char *getname(const uid_t uid);
unsigned int zrand(void);
void simplify(char *subdir);
int checkprintable(register const char *s);
void delete_atomic_file(void);
#ifdef WITH_RFC2640
char *charset_fs2client(const char * const string);
#endif

#ifdef HAVE_SYS_FSUID_H
# define usleep2 usleep
#else
void usleep2(const unsigned long microsec);
#endif

int mysnprintf(char *str, size_t size, const char *format, ...);

extern int opt_a, opt_C, opt_d, opt_F, opt_l, opt_R;

#ifndef CONFDIR
# define CONFDIR "/etc"
#endif

#ifndef VHOST_PATH
# define VHOST_PATH CONFDIR "/pure-ftpd"
#endif

#ifdef WITH_TLS
# ifndef TLS_CERTIFICATE_FILE
#  define TLS_CERTIFICATE_FILE "/etc/ssl/private/pure-ftpd.pem"
# endif
#endif

#define FAKE_SHELL "ftp"

#ifndef PID_FILE
# ifdef NON_ROOT_FTP
#  define PID_FILE CONFDIR "/pure-ftpd.pid"
# else
#  define PID_FILE "/var/run/pure-ftpd.pid"
# endif
#endif

#ifndef UPLOADSCRIPT_PID_FILE
# ifdef NON_ROOT_FTP
#  define UPLOADSCRIPT_PID_FILE CONFDIR "/pure-uploadscript.pid"
# else
#  define UPLOADSCRIPT_PID_FILE "/var/run/pure-uploadscript.pid"
# endif
#endif

#ifndef AUTHD_PID_FILE
# ifdef NON_ROOT_FTP
#  define AUTHD_PID_FILE CONFDIR "/pure-authd.pid"
# else
#  define AUTHD_PID_FILE "/var/run/pure-authd.pid"
# endif
#endif

#ifndef NON_ROOT_FTP
# define DEFAULT_FTP_PORT_S "21"
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
# define DEFAULT_FTP_PORT_S "21"
#else
# define DEFAULT_FTP_PORT_S "2121"
#endif

/*
 * Some operating systems (at least Solaris > 2.7 and FreeBSD) have strange
 * troubles with reusing TCP ports, even when SO_REUSEADDR is enabled. 
 * As a workaround, we try several unassigned privileged ports.
 * The last way is to let the OS assign a port.
 * For egress filtering, you can accept connections from ports <= 20
 * to ports >= 1024.
 */

#define FTP_ACTIVE_SOURCE_PORTS { \
    DEFAULT_FTP_DATA_PORT, 2U, 3U, 4U, 5U, 6U, 10U, 14U, 16U, 0U \
}

#ifndef MAXPATHLEN
# ifdef PATH_MAX
#  define MAXPATHLEN PATH_MAX
# else
#  define MAXPATHLEN 65536U
Warning: neither PATH_MAX nor MAXPATHLEN were found.
Remove these lines if you really want to compile the server, but
the server may be insecure if a wrong value is set here.    
# endif
#endif
#if (MAXPATHLEN) >= (INT_MAX)
Your platform has a very large MAXPATHLEN, we should not trust it.
#endif

#ifndef DEFAULT_MAX_USERS    
# define DEFAULT_MAX_USERS 50
#endif
#ifndef DEFAULT_FTP_DATA_PORT
# define DEFAULT_FTP_DATA_PORT 20
#endif
#ifndef MAX_SYSLOG_LINE
# define MAX_SYSLOG_LINE (MAXPATHLEN + 512U)
#endif
#ifndef DEFAULT_IDLE
# define DEFAULT_IDLE (15UL * 60UL)
#endif
#ifndef MAX_SITE_IDLE
# define MAX_SITE_IDLE (42UL * 60UL)
#endif
#ifndef DEFAULT_MAX_LS_FILES    
# define DEFAULT_MAX_LS_FILES 2000U
#endif
#ifndef DEFAULT_MAX_LS_DEPTH
# define DEFAULT_MAX_LS_DEPTH 5U
#endif
#ifndef GLOB_TIMEOUT
# define GLOB_TIMEOUT 17                   /* Max user time for a 'ls' to complete */
#endif
#ifndef MAX_CPU_TIME
# define MAX_CPU_TIME (4 * 60 * 60)           /* Max allowed CPU time per session */
#endif
#ifndef MAX_SESSION_XFER_IDLE    
# define MAX_SESSION_XFER_IDLE (24 * 60 * 60)   /* Max duration of a transfer */
#endif
#ifndef MAX_USER_LENGTH
# define MAX_USER_LENGTH 127U
#endif

#ifdef LOG_FTP
# define DEFAULT_FACILITY LOG_FTP
#else
# define DEFAULT_FACILITY LOG_LOCAL2
#endif

#if defined(WITH_LDAP) || defined(WITH_MYSQL) || defined(WITH_PGSQL)
# define MAX_DATA_SIZE (16 * 1024 * 1024)       /* Max memory usage - SQL/LDAP need more */
#else
# define MAX_DATA_SIZE (8 * 1024 * 1024)       /* Max memory usage */
#endif

#if CONF_TCP_SO_RCVBUF < 65536
# undef CONF_TCP_SO_RCVBUF
# define CONF_TCP_SO_RCVBUF 65536
#endif
#if CONF_TCP_SO_SNDBUF < 65536
# undef CONF_TCP_SO_SNDBUF
# define CONF_TCP_SO_SNDBUF 65536
#endif
    
#define DEFAULT_DL_CHUNK_SIZE (1 << 28)
#define DEFAULT_UL_CHUNK_SIZE ((CONF_TCP_SO_RCVBUF) * 2)
#define MAX_UL_CHUNK_SIZE ((CONF_TCP_SO_RCVBUF) * 2)
#define MAX_THROTTLING_DELAY 42           /* Maximum throttling compensation */

#define VHOST_PREFIX_MAX_LEN 64    

#define PUREFTPD_TMPFILE_PREFIX ".pureftpd-"    
#define ATOMIC_PREFIX_PREFIX PUREFTPD_TMPFILE_PREFIX "upload."

#ifdef TCP_CORK  
# ifdef DISABLE_CORK  
#  undef TCP_CORK
# endif
#endif
#ifdef TCP_CORK
# define CORK_ON(SK) do { int optval = 1; setsockopt(SK, SOL_TCP, TCP_CORK, \
  &optval, sizeof optval); } while(0)
# define CORK_OFF(SK) do { int optval = 0; setsockopt(SK, SOL_TCP, TCP_CORK, \
  &optval, sizeof optval); } while(0)
#else
# define CORK_ON(SK) do { } while(0)
# define CORK_OFF(SK) do { } while(0)
#endif

#define STORAGE_PORT(X)  (*storage_port(&(X)))
#define STORAGE_PORT6(X) (*storage_port6(&(X)))
#define STORAGE_SIN_ADDR(X) (storage_sin_addr(&(X))->s_addr)
#define STORAGE_SIN_ADDR6(X) (storage_sin_addr6(&(X))->s6_addr)
#define STORAGE_SIN_ADDR6_NF(X) (*(storage_sin_addr6(&(X))))

#ifdef HAVE_SS_LEN
# define STORAGE_LEN(X) ((X).ss_len)
# define SET_STORAGE_LEN(X, Y) do { STORAGE_LEN(X) = (Y); } while(0)
#elif defined(HAVE___SS_LEN)
# define STORAGE_LEN(X) ((X).__ss_len)
# define SET_STORAGE_LEN(X, Y) do { STORAGE_LEN(X) = (Y); } while(0)
#else
# define STORAGE_LEN(X) (STORAGE_FAMILY(X) == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6))
# define SET_STORAGE_LEN(X, Y) (void) 0
#endif

#ifdef HAVE___SS_FAMILY
# define STORAGE_FAMILY(X) ((X).__ss_family)
#else
# define STORAGE_FAMILY(X) ((X).ss_family)
#endif

#ifndef SOL_IP
# define SOL_IP IPPROTO_IP
#endif
#ifndef SOL_TCP
# define SOL_TCP IPPROTO_TCP
#endif

#ifndef INADDR_NONE
# define INADDR_NONE 0
#endif

#if !defined(O_NDELAY) && defined(O_NONBLOCK)
# define O_NDELAY O_NONBLOCK
#endif

#ifndef FNDELAY
# define FNDELAY O_NDELAY
#endif

#ifndef MAP_FAILED
# define MAP_FAILED ((void *) -1)
#endif

#ifndef HAVE_STRTOULL
# ifdef HAVE_STRTOQ
#  define strtoull(X, Y, Z) strtoq(X, Y, Z)
# else
#  define strtoull(X, Y, Z) strtoul(X, Y, Z)
# endif
#endif

#ifndef ULONG_LONG_MAX
# define ULONG_LONG_MAX (1ULL << 63)
#endif

#ifdef HAVE_DIRENT_H
# include <dirent.h>
#endif
#ifdef HAVE_SYS_NDIR_H
# include <sys/ndir.h>
#endif
#ifdef HAVE_NDIR_H
# include <ndir.h>
#endif
       
#ifdef STAT_MACROS_BROKEN
# undef S_ISBLK
# undef S_ISCHR
# undef S_ISDIR
# undef S_ISFIFO
# undef S_ISLNK
# undef S_ISMPB
# undef S_ISMPC
# undef S_ISNWK
# undef S_ISREG
# undef S_ISSOCK
#endif                            /* STAT_MACROS_BROKEN.  */
    
#ifndef S_IFMT
# define S_IFMT 0170000
#endif
#if !defined(S_ISBLK) && defined(S_IFBLK)
# define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif
#if !defined(S_ISCHR) && defined(S_IFCHR)
# define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif
#if !defined(S_ISDIR) && defined(S_IFDIR)
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#if !defined(S_ISREG) && defined(S_IFREG)
# define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined(S_ISFIFO) && defined(S_IFIFO)
# define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#endif
#if !defined(S_ISLNK) && defined(S_IFLNK)
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#if !defined(S_ISSOCK) && defined(S_IFSOCK)
# define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif
#if !defined(S_ISMPB) && defined(S_IFMPB)    /* V7 */
# define S_ISMPB(m) (((m) & S_IFMT) == S_IFMPB)
# define S_ISMPC(m) (((m) & S_IFMT) == S_IFMPC)
#endif
#if !defined(S_ISNWK) && defined(S_IFNWK)    /* HP/UX */
# define S_ISNWK(m) (((m) & S_IFMT) == S_IFNWK)
#endif
    
#ifndef S_IEXEC
# define S_IEXEC S_IXUSR
#endif
    
#ifndef S_IXUSR
# define S_IXUSR S_IEXEC
#endif
#ifndef S_IXGRP
# define S_IXGRP (S_IEXEC >> 3)
#endif
#ifndef S_IXOTH
# define S_IXOTH (S_IEXEC >> 6)
#endif
#ifndef S_IXUGO
# define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#endif

#ifdef DISABLE_UNICODE_CONTROL_CHARS
# define ISCTRLCODE(X) ((X) == 0x7f || !(((unsigned char) (X)) & 0x60))
#else
# define ISCTRLCODE(X) ((X) == 0x7f || ((unsigned char) (X)) < 32U)
#endif
    
#ifndef HAVE_MUNMAP
# define munmap(A, B) (0)
#endif

#ifndef HAVE_GETHOSTNAME
# define gethostname(A, B) (-1)
#endif

#ifndef HAVE_SETEUID
# ifdef HAVE_SETREUID
#  define seteuid(X) setreuid(-1, (X))
# elif defined(HAVE_SETRESUID)
#  define seteuid(X) setresuid(-1, (X), -1)
# else
#  define seteuid(X) (-1)
# endif
#endif
#ifndef HAVE_SETEGID
# ifdef HAVE_SETREGID
#  define setegid(X) setregid(-1, (X))
# elif defined(HAVE_SETRESGID)
#  define setegid(X) setresgid(-1, (X), -1)
# else
#  define setegid(X) (-1)
# endif
#endif
    
#define CRLF "\r\n"

#ifdef WITH_DMALLOC
# define _exit(X) exit(X)
#endif

#ifdef FTPWHO
# define _EXIT(X) do { delete_atomic_file(); ftpwho_exit(X); } while(0)
#else
# define _EXIT(X) do { delete_atomic_file(); _exit(X); } while(0)
#endif

#include "bsd-realpath.h"    
#include "fakechroot.h"
        
#endif
