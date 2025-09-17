#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
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

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { 
    (void)sig; 
    g_stop = 1; 
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s -p <port> -d <dest_dir> [-o]\n", prog);
    fprintf(stderr, "  -p: TCP port to listen on\n");
    fprintf(stderr, "  -d: destination directory to save files\n");
    fprintf(stderr, "  -o: overwrite existing files (default: fail if exists)\n");
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (errno != ENOENT) 
        return -1;
    return mkdir(path, 0755);
}

static int handle_client(int cfd, const char *dest_dir, bool overwrite) {
    char magic[LANDROP_MAGIC_LEN];
    if (read_full(cfd, magic, LANDROP_MAGIC_LEN) < 0) {
        perror("read magic");
        return -1;
    }
    if (memcmp(magic, LANDROP_MAGIC, LANDROP_MAGIC_LEN) != 0) {
        fprintf(stderr, "Invalid magic from client\n");
        return -1;
    }
    uint64_t be_size;
    uint16_t be_namelen;
    if (read_full(cfd, &be_size, sizeof(be_size)) < 0) { 
        perror("read size"); 
        return -1; 
    }
    if (read_full(cfd, &be_namelen, sizeof(be_namelen)) < 0) { 
        perror("read name len"); 
        return -1; 
    }
    uint64_t filesize = be64_to_host(be_size);
    uint16_t namelen = ntohs(be_namelen);
    if (namelen == 0 || namelen > 4096) {
        fprintf(stderr, "Bad name length: %u\n", namelen);
        return -1;
    }
    char *name = (char *)malloc((size_t)namelen + 1);
    if (!name) { perror("malloc name"); 
        return -1; 
    }
    if (read_full(cfd, name, namelen) < 0) {
        perror("read name");
        free(name);
        return -1;
    }
    name[namelen] = '\0';

    char sname[4096];
    if (sanitize_filename(name, sname, sizeof(sname)) != 0) {
        fprintf(stderr, "Invalid filename\n");
        free(name);
        return -1;
    }
    free(name);

    char path[8192];
    if (snprintf(path, sizeof(path), "%s/%s", dest_dir, sname) >= (int)sizeof(path)) {
        fprintf(stderr, "Destination path too long\n");
        return -1;
    }

    int flags = O_CREAT | O_WRONLY | (overwrite ? O_TRUNC : O_EXCL);
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        perror("open dest file");
        unsigned char status = 2; // cannot open / exists
        (void)write_full(cfd, &status, 1);
        return -1;
    }

    const size_t BUF_SZ = 64 * 1024;
    char *buf = (char *)malloc(BUF_SZ);
    if (!buf) { perror("malloc buf"); close(fd); return -1; }

    uint64_t left = filesize;
    uint64_t received = 0;
    struct timespec ts0, ts_last; clock_gettime(CLOCK_MONOTONIC, &ts0); 
    ts_last = ts0;
    while (left > 0) {
        size_t chunk = left > BUF_SZ ? BUF_SZ : (size_t)left;
        if (read_full(cfd, buf, chunk) < 0) {
            perror("read file data");
            free(buf); close(fd);
            unlink(path);
            return -1;
        }
        if (write_full(fd, buf, chunk) < 0) {
            perror("write file data");
            free(buf); close(fd);
            unlink(path);
            return -1;
        }
        left -= chunk;
        received += chunk;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        double elapsed = (ts.tv_sec - ts0.tv_sec) + (ts.tv_nsec - ts0.tv_nsec)/1e9;
        double since_last = (ts.tv_sec - ts_last.tv_sec) + (ts.tv_nsec - ts_last.tv_nsec)/1e9;
        if (since_last >= 0.2 || left == 0) {
            double pct = filesize ? (100.0 * (double)received / (double)filesize) : 100.0;
            int barw = 40; 
            int fill = (int)(pct/100.0*barw + 0.5); 
            if (fill>barw) 
                fill=barw;

            char bar[41]; 
            for (int i=0;i<barw;++i) 
                bar[i] = i<fill ? '=' : ' '; 
            
            bar[barw]='\0';
            double bps = elapsed>0 ? (double)received/elapsed : 0.0;
            const char *u[] = {"B","KiB","MiB","GiB","TiB"};
            char spd[32]; 
            double v=bps; 
            int ui=0; 
            while(v>=1024.0 && ui<4){
                v/=1024.0;
                ui++;
            }

            snprintf(spd, sizeof(spd), "%.1f %s/s", v, u[ui]);
            // Minimal human sizes for received/total
            char rstr[32], tstr[32];
            double vr=(double)received; 
            int ur=0; 
            while(vr>=1024.0 && ur<4){
                vr/=1024.0;
                ur++;
            }
            snprintf(rstr,sizeof(rstr),"%.1f %s",vr,u[ur]);
            double vt=(double)filesize; 
            int ut=0; 
            while(vt>=1024.0 && ut<4){
                vt/=1024.0;
                ut++;
            }
            snprintf(tstr,sizeof(tstr),"%.1f %s",vt,u[ut]);
            fprintf(stderr, "\r[%-40s] %6.2f%%  %s  %s/%s", bar, pct, spd, rstr, tstr);
            fflush(stderr);
            ts_last = ts;
        }
    }

    free(buf);
    if (close(fd) < 0) { 
        perror("close dest"); 
    }

    unsigned char status = 0;
    if (write_full(cfd, &status, 1) < 0) {
        perror("send status");
        return -1;
    }
    fprintf(stderr, "\nReceived %s (%lu bytes)\n", sname, (unsigned long)filesize);
    return 0;
}

int main(int argc, char **argv) {
    int port = -1;
    const char *dest_dir = NULL;
    bool overwrite = false;
    int opt;
    while ((opt = getopt(argc, argv, "p:d:oh")) != -1) {
        switch (opt) {
            case 'p': port = atoi(optarg); break;
            case 'd': dest_dir = optarg; break;
            case 'o': overwrite = true; break;
            case 'h': default: usage(argv[0]); return opt=='h'?0:1;
        }
    }
    if (port <= 0 || port > 65535 || !dest_dir) {
        usage(argv[0]);
        return 1;
    }

    if (ensure_dir(dest_dir) != 0) {
        perror("ensure dest dir");
        return 1;
    }

    signal(SIGINT, on_sigint);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { 
        perror("socket"); 
        return 1; 
    }

    int yes = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt REUSEADDR");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return 1;
    }
    if (listen(sfd, 16) < 0) {
        perror("listen");
        close(sfd);
        return 1;
    }

    fprintf(stderr, "landropd listening on port %d, saving to %s\n", port, dest_dir);

    while (!g_stop) {
        struct sockaddr_in caddr; socklen_t clen = sizeof(caddr);
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(cfd, dest_dir, overwrite);
        close(cfd);
    }

    close(sfd);
    fprintf(stderr, "landropd stopped\n");
    return 0;
}
