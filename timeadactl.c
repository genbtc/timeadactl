#define _POSIX_C_SOURCE 200809L
/* SPDX-License-Identifier: GPL-3.0-only */

/* One-shot NTP client: query a server and set CLOCK_REALTIME. */
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NTP_LEN 48
#define NTP_EPOCH 2208988800LL
#define NTP_ERA 4294967296.0L
#define SERVER_DEFAULT "pool.ntp.org"
#define PORT_DEFAULT "123"
#define TIMEOUT_MS 3000

struct sample {
    struct timespec t1, t4;
    long double t2, t3;
    unsigned int stratum, version;
};

static long double
as_seconds(const struct timespec *t)
{
    return (long double)t->tv_sec + (long double)t->tv_nsec / 1.0e9L;
}

static void
put_timestamp(uint8_t *p, const struct timespec *t)
{
    uint32_t sec = htonl((uint32_t)((int64_t)t->tv_sec + NTP_EPOCH));
    uint32_t frac = htonl((uint32_t)(((uint64_t)t->tv_nsec << 32) / 1000000000ULL));
    memcpy(p, &sec, 4);
    memcpy(p + 4, &frac, 4);
}

/* Unfold the 32-bit NTP seconds field to the era nearest the local clock. */
static int
get_timestamp(const uint8_t *p, long double reference, long double *value)
{
    uint32_t sec, frac;
    long double era;

    memcpy(&sec, p, 4);
    memcpy(&frac, p + 4, 4);
    sec = ntohl(sec);
    frac = ntohl(frac);
    if (sec == 0 && frac == 0)
        return -1;

    era = floorl((reference + (long double)NTP_EPOCH - sec) / NTP_ERA + 0.5L);
    *value = (long double)sec + era * NTP_ERA - (long double)NTP_EPOCH +
             (long double)frac / NTP_ERA;
    return 0;
}

static int
query(const char *host, const char *port, struct sample *s, char *error, size_t error_size)
{
    struct addrinfo hints = {0}, *list = NULL, *ai;
    int gai;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    gai = getaddrinfo(host, port, &hints, &list);
    if (gai != 0) {
        (void)snprintf(error, error_size, "resolve %s: %s", host, gai_strerror(gai));
        return -1;
    }

    for (ai = list; ai != NULL; ai = ai->ai_next) {
        uint8_t req[NTP_LEN] = {0}, reply[512];
        struct pollfd pfd;
        struct timespec t1, t4;
        ssize_t n;
        int fd, ready;
        unsigned int li, vn, mode, stratum;
        long double reference;

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            (void)snprintf(error, error_size, "socket: %s", strerror(errno));
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
            (void)snprintf(error, error_size, "connect: %s", strerror(errno));
            (void)close(fd);
            continue;
        }
        if (clock_gettime(CLOCK_REALTIME, &t1) < 0) {
            (void)snprintf(error, error_size, "clock_gettime: %s", strerror(errno));
            (void)close(fd);
            break;
        }

        req[0] = 0x23; /* NTPv4, client mode. */
        put_timestamp(req + 40, &t1);
        if (send(fd, req, sizeof(req), 0) != (ssize_t)sizeof(req)) {
            (void)snprintf(error, error_size, "send: %s", strerror(errno));
            (void)close(fd);
            continue;
        }

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        do {
            ready = poll(&pfd, 1, TIMEOUT_MS);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0 || (pfd.revents & POLLIN) == 0) {
            (void)snprintf(error, error_size, "%s",
                           ready == 0 ? "NTP request timed out" :
                           ready < 0 ? strerror(errno): "NTP socket error");
            (void)close(fd);
            continue;
        }

        n = recv(fd, reply, sizeof(reply), 0);
        if (n < NTP_LEN) {
            (void)snprintf(error, error_size, "%s",
                         n < 0 ? strerror(errno) : "short NTP reply");
            (void)close(fd);
            continue;
        }
        if (clock_gettime(CLOCK_REALTIME, &t4) < 0) {
            (void)snprintf(error, error_size, "clock_gettime: %s", strerror(errno));
            (void)close(fd);
            break;
        }
        (void)close(fd);

        li = reply[0] >> 6;
        vn = (reply[0] >> 3) & 7U;
        mode = reply[0] & 7U;
        stratum = reply[1];
        if (mode != 4 || vn < 3 || vn > 4) {
            (void)snprintf(error, error_size, "reply is not an NTPv3/v4 server response");
            continue;
        }
        if (memcmp(reply + 24, req + 40, 8) != 0) {
            (void)snprintf(error, error_size, "reply originate timestamp mismatch");
            continue;
        }
        if (stratum == 0) {
            char kiss[5];
            memcpy(kiss, reply + 12, 4);
            kiss[4] = '\0';
            (void)snprintf(error, error_size, "server refused request (%.4s)", kiss);
            continue;
        }
        if (li == 3 || stratum > 15) {
            (void)snprintf(error, error_size, "server clock is unsynchronized or stratum is invalid");
            continue;
        }

        reference = as_seconds(&t4);
        if (get_timestamp(reply + 32, reference, &s->t2) < 0 ||
            get_timestamp(reply + 40, reference, &s->t3) < 0) {
            (void)snprintf(error, error_size, "server returned a zero timestamp");
            continue;
        }
        s->t1 = t1;
        s->t4 = t4;
        s->stratum = stratum;
        s->version = vn;
        freeaddrinfo(list);
        return 0;
    }

    freeaddrinfo(list);
    return -1;
}

static int
make_timespec(long double value, struct timespec *t)
{
    long double whole;
    long long ns;

    if (!isfinite(value))
        return -1;
    whole = floorl(value);
    t->tv_sec = (time_t)whole;
    if ((long double)t->tv_sec != whole)
        return -1;
    ns = llroundl((value - whole) * 1.0e9L);
    if (ns == 1000000000LL) {
        ++t->tv_sec;
        ns = 0;
    }
    t->tv_nsec = (long)ns;
    return 0;
}

static void
usage(const char *name)
{
    (void)fprintf(stderr,
        "Usage: %s [-n] [-s server] [-p port]\n"
        "  -n          query and display time, but do not set the clock\n"
        "  -s server   NTP server (default: %s)\n"
        "  -p port     UDP port (default: %s)\n"
        "  -h          show this help\n", name, SERVER_DEFAULT, PORT_DEFAULT);
}

int
main(int argc, char **argv)
{
    const char *host = SERVER_DEFAULT;
    char port[6] = PORT_DEFAULT;
    int dry_run = 0, opt;
    unsigned long parsed;
    struct sample s;
    struct timespec target;
    long double t1, t4, offset, delay, corrected;
    char error[256] = "no usable server address";
    struct tm utc;
    char stamp[32];

    static const struct option long_options[] = {
        {"dry-run", no_argument, NULL, 'n'},
        {"server", required_argument, NULL, 's'},
        {"port", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "ns:p:h", long_options, NULL)) != -1) {
        if (opt == 'n')
            dry_run = 1;
        else if (opt == 's')
            host = optarg;
        else if (opt == 'p') {
            char *end = NULL;
            errno = 0;
            parsed = strtoul(optarg, &end, 10);
            if (errno != 0 || end == optarg || *end != '\0' || parsed < 1 || parsed > 65535) {
                (void)fprintf(stderr, "timeadactl: invalid UDP port: %s\n", optarg);
                return EXIT_FAILURE;
            }
            (void)snprintf(port, sizeof(port), "%lu", parsed);
        } else if (opt == 'h') {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (query(host, port, &s, error, sizeof(error)) < 0) {
        (void)fprintf(stderr, "timeadactl: %s\n", error);
        return EXIT_FAILURE;
    }

    t1 = as_seconds(&s.t1);
    t4 = as_seconds(&s.t4);
    offset = ((s.t2 - t1) + (s.t3 - t4)) / 2.0L;
    delay = (t4 - t1) - (s.t3 - s.t2);
    corrected = t4 + offset;
    if (make_timespec(corrected, &target) < 0 ||
        gmtime_r(&target.tv_sec, &utc) == NULL ||
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &utc) == 0) {
        (void)fprintf(stderr, "timeadactl: invalid calculated time\n");
        return EXIT_FAILURE;
    }

    (void)printf("NTP server: %s (NTPv%u, stratum %u)\n", host, s.version, s.stratum);
    (void)printf("Offset:    %+.6Lf seconds\n", offset);
    (void)printf("Delay:      %.6Lf seconds\n", delay);
    (void)printf("UTC time:   %s.%09ldZ\n", stamp, target.tv_nsec);
    if (dry_run) {
        (void)puts("Dry run: system clock was not changed.");
        return EXIT_SUCCESS;
    }
    if (clock_settime(CLOCK_REALTIME, &target) < 0) {
        (void)fprintf(stderr, "timeadactl: clock_settime: %s (requires root or CAP_SYS_TIME)\n",
                      strerror(errno));
        return EXIT_FAILURE;
    }
    (void)puts("System clock updated.");
    return EXIT_SUCCESS;
}
