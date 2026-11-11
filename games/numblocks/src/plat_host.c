/* A computer: the screen goes to a frame buffer that tests save as images,
 * keys come from a script, time is simulated (tests/play.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nb.h"

uint16_t host_fb[SCREEN_W * SCREEN_H];
uint32_t host_time;               /* ms */
uint32_t host_keys;

uint32_t plat_keys(void) { return host_keys; }
uint64_t host_raw;                 /* (tests: keys as the calculator numbers them) */
uint64_t plat_scan(void) { return host_raw; }
uint32_t plat_millis(void) { return host_time; }
#include <unistd.h>
static int net_fd;
void plat_sleep(uint32_t ms) {
  host_time += ms;
  if (net_fd > 0) usleep(ms * 1000);   /* (talking to a real gateway: real time) */
}
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if ((unsigned)(x + c) < SCREEN_W && (unsigned)(y + r) < SCREEN_H) host_fb[(y + r) * SCREEN_W + x + c] = px[r * w + c];
}

void plat_pull(int x, int y, int w, int h, uint16_t *px) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      px[r * w + c] = (unsigned)(x + c) < SCREEN_W && (unsigned)(y + r) < SCREEN_H ? host_fb[(y + r) * SCREEN_W + x + c] : 0;
}

static char save_dir[256] = "build/host-saves";
void host_save_dir(const char *d) { snprintf(save_dir, sizeof save_dir, "%s", d); }
bool plat_save(const char *name, const void *data, uint32_t len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "wb");
  if (!f) return false;
  fwrite(data, 1, len, f);
  fclose(f);
  return true;
}
static uint8_t loaded[65536];
const uint8_t *plat_load(const char *name, uint32_t *len) {
  char p[512];
  snprintf(p, sizeof p, "%s/%s", save_dir, name);
  FILE *f = fopen(p, "rb");
  if (!f) return NULL;
  *len = (uint32_t)fread(loaded, 1, sizeof loaded, f);
  fclose(f);
  return loaded;
}
#include <dirent.h>
static uint8_t *open_buf;
static uint32_t open_len;
uint8_t *plat_save_open(const char *name, uint32_t len) {
  (void)name;
  free(open_buf);
  open_buf = calloc(1, len ? len : 1), open_len = len;
  return open_buf;
}
void plat_save_close(const char *name) { plat_save(name, open_buf, open_len); }
uint8_t *plat_save_grow(const char *name, uint32_t len) {
  uint32_t n = 0;
  const uint8_t *d = plat_load(name, &n);
  free(open_buf);
  open_buf = calloc(1, len ? len : 1), open_len = len;
  if (d) memcpy(open_buf, d, n < len ? n : len);
  return open_buf;
}
void plat_save_done(const char *name, uint32_t len) { plat_save(name, open_buf, len < open_len ? len : open_len); }
bool plat_record(int i, char *name, int max) {
  DIR *d = opendir(save_dir);
  if (!d) return false;
  struct dirent *e;
  bool found = false;
  while ((e = readdir(d)))
    if (e->d_name[0] != '.' && i-- == 0) {
      snprintf(name, (size_t)max, "%s", e->d_name);
      found = true;
      break;
    }
  closedir(d);
  return found;
}
bool host_launcher;
bool plat_in_launcher(void) { return host_launcher; }
void plat_remove_prefix(const char *prefix) {
  DIR *d = opendir(save_dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)))
    if (!strncmp(e->d_name, prefix, strlen(prefix))) {
      char p[600];
      snprintf(p, sizeof p, "%s/%s", save_dir, e->d_name);
      remove(p);
    }
  closedir(d);
}
uint32_t plat_storage_free(void) { return 30000; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

void host_shot(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    uint16_t c = host_fb[i];
    uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

/* ---------------------------------------------------------------- the link to NumSocial, on a computer
 * (tests): TCP to the gateway's simulator port (numtube_gateway.py --tcp PORT), its TMIA frames (magic,
 * version, type, length, payload, CRC-16) */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static uint8_t net_in[8192];
static int net_n;
static uint16_t crc16(const uint8_t *d, int n) {
  uint16_t c = 0xFFFF;
  for (int i = 0; i < n; i++) {
    c ^= (uint16_t)(d[i] << 8);
    for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}
bool plat_net_available(void) { return getenv("NB_NET_PORT") != NULL; }
bool plat_net_open(void) {
  if (net_fd > 0) return true;
  const char *port = getenv("NB_NET_PORT");
  if (!port) return false;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET, a.sin_port = htons((uint16_t)atoi(port)), a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
    close(fd);
    return false;
  }
  fcntl(fd, F_SETFL, O_NONBLOCK);
  net_fd = fd, net_n = 0;
  /* the handshake the firmware's link makes: Hello, then the gateway's HelloAck */
  plat_net_send(0x0A, NULL, 0);
  return true;
}
bool plat_net_send(uint8_t type, const void *payload, uint16_t length) {
  if (net_fd <= 0) return false;
  uint8_t f[8 + 4096 + 2];
  f[0] = 'T', f[1] = 'M', f[2] = 'I', f[3] = 'A', f[4] = 1, f[5] = type, f[6] = (uint8_t)length, f[7] = (uint8_t)(length >> 8);
  if (length) memcpy(f + 8, payload, length);
  uint16_t c = crc16(f, 8 + length);
  f[8 + length] = (uint8_t)c, f[9 + length] = (uint8_t)(c >> 8);
  int n = 10 + length, at = 0;
  while (at < n) {
    ssize_t w = write(net_fd, f + at, (size_t)(n - at));
    if (w < 0 && errno == EAGAIN) continue;
    if (w <= 0) return false;
    at += (int)w;
  }
  return true;
}
int plat_net_poll(uint8_t *type, void *buffer, uint16_t capacity) {
  if (net_fd <= 0) return -1;
  for (;;) {
    if (net_n >= 10) {
      if (memcmp(net_in, "TMIA", 4)) {   /* (out of step: drop a byte) */
        memmove(net_in, net_in + 1, (size_t)--net_n);
        continue;
      }
      int len = net_in[6] | net_in[7] << 8;
      if (net_n >= 10 + len) {
        *type = net_in[5];
        int k = len < capacity ? len : capacity;
        memcpy(buffer, net_in + 8, (size_t)k);
        memmove(net_in, net_in + 10 + len, (size_t)(net_n - 10 - len));
        net_n -= 10 + len;
        if (*type == 0x0B || *type == 0x0A) continue;   /* (HelloAck: the link's own) */
        return k;
      }
    }
    if (net_n == (int)sizeof net_in) net_n = 0;
    ssize_t r = read(net_fd, net_in + net_n, sizeof net_in - (size_t)net_n);
    if (r <= 0) return -1;
    net_n += (int)r;
  }
}
void plat_net_close(void) {
  if (net_fd > 0) close(net_fd);
  net_fd = 0;
}
bool plat_net_plugged(void) { return true; }
