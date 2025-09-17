#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include "common.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s -h <host> -p <port> -f <file> [-n <remote_name>]\n", prog);
}

static int connect_to(const char *host, const char *port) {
    struct addrinfo hints; memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }
    int cfd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        cfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (cfd < 0) 
            continue;
        if (connect(cfd, ai->ai_addr, ai->ai_addrlen) == 0) {
            freeaddrinfo(res);
            return cfd;
        }
        close(cfd);
        cfd = -1;
    }
    freeaddrinfo(res);
    return -1;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void human_bytes(double v, char *out, size_t n) {
    const char *u[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    while (v >= 1024.0 && i < 4) { 
        v /= 1024.0; 
        i++; 
    }
    snprintf(out, n, "%.1f %s", v, u[i]);
}

static void print_progress(uint64_t done, uint64_t total, double elapsed) {
    double pct = total ? (100.0 * (double)done / (double)total) : 100.0;
    int barw = 40;
    int fill = (int)(pct / 100.0 * barw + 0.5);
    if (fill > barw) 
        fill = barw;

    char bar[41];
    for (int i = 0; i < barw; ++i) 
        bar[i] = (i < fill) ? '=' : ' ';
    bar[barw] = '\0';
    double bps = elapsed > 0 ? (double)done / elapsed : 0.0;
    char spd[32], dstr[32], tstr[32];
    human_bytes(bps, spd, sizeof(spd));
    human_bytes((double)done, dstr, sizeof(dstr));
    human_bytes((double)total, tstr, sizeof(tstr));
    fprintf(stderr, "\r[%-40s] %6.2f%%  %s/s  %s/%s", bar, pct, spd, dstr, tstr);
    fflush(stderr);
}

int main(int argc, char **argv) {
    const char *host = NULL;
    const char *port_str = NULL;
    const char *file = NULL;
    const char *remote_name = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "h:p:f:n:")) != -1) {
        switch (opt) {
            case 'h': host = optarg; break;
            case 'p': port_str = optarg; break;
            case 'f': file = optarg; break;
            case 'n': remote_name = optarg; break;
            default: usage(argv[0]); return 1;
        }
    }
    if (!host || !port_str || !file) { 
        usage(argv[0]); 
        return 1; 
    }

    struct stat st;
    if (stat(file, &st) != 0) { 
        perror("stat file"); 
        return 1; 
    }
    if (!S_ISREG(st.st_mode)) { 
        fprintf(stderr, "Not a regular file\n"); 
        return 1; }
    uint64_t filesize = (uint64_t)st.st_size;

    const char *base = remote_name ? remote_name : path_basename(file);
    char sname[4096];
    if (sanitize_filename(base, sname, sizeof(sname)) != 0) {
        fprintf(stderr, "Invalid remote name\n");
        return 1;
    }
    size_t name_len = strlen(sname);
    if (name_len == 0 || name_len > 4096) {
        fprintf(stderr, "Remote name too long\n");
        return 1;
    }

    int cfd = connect_to(host, port_str);
    if (cfd < 0) { perror("connect"); 
        return 1; 
    }

    // Send header
    if (write_full(cfd, LANDROP_MAGIC, LANDROP_MAGIC_LEN) < 0) { 
        perror("send magic"); 
        close(cfd); 
        return 1; 
    }

    uint64_t be_size = host_to_be64(filesize);
    uint16_t be_namelen = htons((uint16_t)name_len);
    if (write_full(cfd, &be_size, sizeof(be_size)) < 0) { 
        perror("send size"); 
        close(cfd); 
        return 1; 
    }

    if (write_full(cfd, &be_namelen, sizeof(be_namelen)) < 0) { 
        perror("send name len"); 
        close(cfd); 
        return 1; 
    }

    if (write_full(cfd, sname, name_len) < 0) { 
        perror("send name"); 
        close(cfd); 
        return 1; 
    }

    // Send file content
    FILE *fp = fopen(file, "rb");
    if (!fp) { 
        perror("fopen file"); 
        close(cfd); 
        return 1; 
    }

    const size_t BUF_SZ = 64 * 1024;
    char *buf = (char *)malloc(BUF_SZ);
    if (!buf) { 
        perror("malloc"); 
        fclose(fp); 
        close(cfd); 
        return 1; 
    }

    size_t r;
    uint64_t sent = 0;
    double t0 = now_sec();
    double last = t0;
    while ((r = fread(buf, 1, BUF_SZ, fp)) > 0) {
        if (write_full(cfd, buf, r) < 0) { 
            perror("send data"); 
            free(buf); 
            fclose(fp); 
            close(cfd); 
            return 1; 
        }
        sent += (uint64_t)r;
        double t = now_sec();
        if (t - last >= 0.1 || sent == filesize) {
            print_progress(sent, filesize, t - t0);
            last = t;
        }
    }
    if (ferror(fp)) { 
        perror("fread"); 
        free(buf); 
        fclose(fp); 
        close(cfd); 
        return 1; 
    }
    free(buf);
    fclose(fp);

    // Receive 1-byte status
    unsigned char status;
    if (read_full(cfd, &status, 1) < 0) { 
        perror("recv status"); 
        close(cfd); 
        return 1; 
    }
    close(cfd);
    if (status != 0) {
        fprintf(stderr, "\n");
        fprintf(stderr, "Server reported error (code %u)\n", (unsigned)status);
        return 1;
    }
    fprintf(stderr, "\nFile sent successfully (%s, %lu bytes)\n", sname, (unsigned long)filesize);
    return 0;
}
