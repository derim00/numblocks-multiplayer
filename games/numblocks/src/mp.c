/* Multiplayer, through NumSocial (github.com/derim00/numsocial).
 *
 * The calculator only talks to the NumSocial app on the computer it is
 * plugged into, over USB. The player who opens their world ("Open to LAN")
 * makes their computer the game's server; the others find it in the public
 * list (Multiplayer) and join. The world is made from its seed on every
 * calculator, so only the blocks changed travel: all of them when a player
 * joins, then each change as it happens. Positions go out 5 times a second;
 * each calculator moves its own mobs (they are not shared), and only the
 * Overworld is.
 *
 * Frames: see numsocial's gateway/numblocks_service.py (0x90 to 0xA3). */
#include <math.h>
#include "nb.h"
#include "edits.h"
#pragma GCC optimize("Os")
#define RGB(r, g, b) (uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | (b) >> 3)

enum {
  F_HELLO = 0x90, F_HELLO_OK, F_LIST, F_SERVER, F_LIST_DONE, F_HOST, F_DUMP, F_HOST_OK, F_JOIN, F_WORLD, F_BLOCKS,
  F_READY, F_STATE, F_PLAYERS, F_PLAYER, F_BLOCK, F_CHAT, F_LEAVE, F_END, F_TIME
};
#define PROTOCOL 1
#define ENTRY 11
#define STATE_SIZE 23
#define OUT_MAX 64   /* blocks changed here, sent each tick */

int mp_mode, mp_link;
char mp_name[MP_NAME + 1], mp_status[64], mp_code[12];
MpServer mp_servers[MP_MAX_SERVERS];
int mp_nservers;
bool mp_listing;

static uint8_t rx[3400];       /* a frame from NumSocial (4 KB at most; the biggest are 3.3 KB) */
static uint8_t out[OUT_MAX * ENTRY];
static int n_out;
static bool applying, waiting_world;
static uint32_t ticks;
static int my_id = -1;

typedef struct {
  uint8_t id, here, named, flags;
  char name[MP_NAME + 1];
  Entity e;
  float tx, ty, tz, tyaw, tpitch;
  int16_t lx, ly;   /* where the name goes on the screen */
  bool shown;
} Remote;
static Remote rem[MP_PLAYERS];

/* ---------------------------------------------------------------- bytes */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static float getf(const uint8_t *p) {
  uint32_t v = get32(p);
  float f;
  memcpy(&f, &v, 4);
  return f;
}
static void putf(uint8_t *p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  put32(p, v);
}
static int put_str8(uint8_t *p, const char *s, int max) {
  int n = 0;
  while (s[n] && n < max) n++;
  p[0] = (uint8_t)n;
  memcpy(p + 1, s, (size_t)n);
  return n + 1;
}
/* a str8 at *at into out (max chars), *at moved past it */
static void get_str8(const uint8_t *p, int len, int *at, char *o, int max) {
  int n = *at < len ? p[*at] : 0, k = 0;
  for (; k < n && k < max && *at + 1 + k < len; k++) o[k] = (char)p[*at + 1 + k];
  o[k] = 0;
  *at += 1 + n;
}
static void put_entry(uint8_t *p, int x, int y, int z, int b) {
  put32(p, (uint32_t)x), p[4] = (uint8_t)y, put32(p + 5, (uint32_t)z), p[9] = (uint8_t)b, p[10] = (uint8_t)(b >> 8);
}

/* ---------------------------------------------------------------- the name */
void mp_load_name(void) {
  uint32_t n;
  const uint8_t *d = plat_load("nbmpname", &n);
  if (d && n > 0 && n <= MP_NAME) {
    memcpy(mp_name, d, n), mp_name[n] = 0;
    return;
  }
  /* Player + 3 digits, until they type theirs */
  int k = (int)(plat_millis() % 900) + 100;
  strcpy(mp_name, "Player");
  mp_name[6] = (char)('0' + k / 100), mp_name[7] = (char)('0' + k / 10 % 10), mp_name[8] = (char)('0' + k % 10), mp_name[9] = 0;
}
void mp_save_name(void) {
  if (mp_name[0]) plat_save("nbmpname", mp_name, (uint32_t)strlen(mp_name));
}

/* ---------------------------------------------------------------- the link */
static bool send(int type, const void *p, int n) { return plat_net_send((uint8_t)type, p, (uint16_t)n); }

static void handle(int type, const uint8_t *p, int n);

bool mp_connect(void) {
  if (mp_link == MPL_ONLINE) return true;
  if (!plat_net_available()) {
    mp_link = MPL_NO_FIRMWARE;
    strcpy(mp_status, "Needs the NumSocial firmware");
    return false;
  }
  if (!plat_net_open()) {
    mp_link = MPL_NO_APP;
    strcpy(mp_status, "Open NumSocial on the computer");
    return false;
  }
  uint8_t b[24];
  b[0] = PROTOCOL;
  int k = 1 + put_str8(b + 1, mp_name, MP_NAME);
  send(F_HELLO, b, k);
  /* (the computer reaches the public list: a few seconds at most) */
  uint32_t t0 = plat_millis();
  while (plat_millis() - t0 < 12000) {
    uint8_t type;
    int len = plat_net_poll(&type, rx, sizeof rx);
    if (len < 0) {
      plat_sleep(5);
      continue;
    }
    if (type == F_HELLO_OK) {
      mp_link = len > 0 && rx[0] ? MPL_ONLINE : MPL_OFFLINE;
      if (mp_link == MPL_OFFLINE) strcpy(mp_status, "The computer has no Internet");
      return mp_link == MPL_ONLINE;
    }
    if (type >= F_HELLO) handle(type, rx, len);
  }
  mp_link = MPL_NO_APP;
  strcpy(mp_status, "NumSocial does not answer");
  return false;
}

void mp_list(void) {
  if (mp_link != MPL_ONLINE) return;
  mp_nservers = 0;
  mp_listing = true;
  send(F_LIST, NULL, 0);
}

void mp_join(int i) {
  if (mp_link != MPL_ONLINE || i < 0 || i >= mp_nservers) return;
  uint8_t b[16];
  int k = put_str8(b, mp_servers[i].id, 11);
  memset(rem, 0, sizeof rem);
  my_id = -1;
  waiting_world = true;
  strcpy(mp_status, "Connecting to the server...");
  send(F_JOIN, b, k);
}

/* the host's changed blocks, sent as the game opens */
static int n_dump;
static void dump_one(int x, int y, int z, int b) {
  put_entry(out + n_dump * ENTRY, x, y, z, b);
  if (++n_dump == OUT_MAX) send(F_DUMP, out, n_dump * ENTRY), n_dump = 0;
}

bool mp_host(int mode) {
  if (!mp_connect()) return false;
#if NB_DIMS
  if (dim != DIM_OVERWORLD) {
    strcpy(mp_status, "Open it from the Overworld");
    return false;
  }
#endif
  uint8_t b[48];
  uint64_t seed = (uint64_t)world_seed;
  for (int i = 0; i < 8; i++) b[i] = (uint8_t)(seed >> (8 * i));
  b[8] = world_type, b[9] = (uint8_t)mode;
  put32(b + 10, game_time);
  int k = 14 + put_str8(b + 14, world_name(), 24);
  send(F_HOST, b, k);
  edits_flush();
  n_dump = 0, n_out = 0;
  edits_each(dump_one);
  if (n_dump) send(F_DUMP, out, n_dump * ENTRY);
  send(F_DUMP, NULL, 0);
  memset(rem, 0, sizeof rem);
  my_id = 0;
  strcpy(mp_status, "Opening the game...");
  return true;
}

void mp_leave(void) {
  if (mp_mode == MP_OFF && !waiting_world) return;
  send(F_LEAVE, NULL, 0);
  if (mp_mode == MP_GUEST) plat_remove_prefix("nb0");   /* (a world joined leaves nothing behind) */
  mp_mode = MP_OFF, waiting_world = false, mp_code[0] = 0;
  lan_open = lan_cheats = false;
  memset(rem, 0, sizeof rem);
}

void mp_say(const char *line) {
  send(F_CHAT, line, (int)strlen(line));
}

bool mp_applying(void) { return applying; }

void mp_block_changed(int x, int y, int z, int b) {
  if (mp_mode == MP_OFF || applying) return;
#if NB_DIMS
  if (dim != DIM_OVERWORLD) return;   /* (only the Overworld is shared) */
#endif
  if (n_out == OUT_MAX) send(F_BLOCK, out, n_out * ENTRY), n_out = 0;
  put_entry(out + n_out * ENTRY, x, y, z, b);
  n_out++;
}

/* ---------------------------------------------------------------- the other players */
static Remote *remote(int id, bool make) {
  for (int i = 0; i < MP_PLAYERS; i++)
    if (rem[i].here && rem[i].id == id) return &rem[i];
  if (!make) return NULL;
  for (int i = 0; i < MP_PLAYERS; i++)
    if (!rem[i].here) {
      Remote *r = &rem[i];
      memset(r, 0, sizeof *r);
      r->here = 1, r->id = (uint8_t)id;
      r->e.type = E_PLAYER, r->e.sheared = (uint8_t)(id & 7);   /* (the shirt's colour) */
      strcpy(r->name, "?");
      return r;
    }
  return NULL;
}

const Entity *mp_entity(int i) {
  if (mp_mode == MP_OFF || i < 0 || i >= MP_PLAYERS || !rem[i].here || !rem[i].named) return NULL;
#if NB_DIMS
  if (dim != DIM_OVERWORLD || (rem[i].flags & 8)) return NULL;
#endif
  return &rem[i].e;
}
void mp_label(int i, int x, int y, bool shown) {
  rem[i].lx = (int16_t)x, rem[i].ly = (int16_t)y, rem[i].shown = shown;
}
bool mp_label_at(int i, int *x, int *y, const char **name) {
  if (!mp_entity(i) || !rem[i].shown) return false;
  *x = rem[i].lx, *y = rem[i].ly, *name = rem[i].name;
  return true;
}

static void players_frame(const uint8_t *p, int n) {
  for (int at = 0; at + 1 + STATE_SIZE <= n; at += 1 + STATE_SIZE) {
    int id = p[at];
    if (id == my_id) continue;
    Remote *r = remote(id, true);
    if (!r) continue;
    const uint8_t *s = p + at + 1;
    r->tx = getf(s), r->ty = getf(s + 4), r->tz = getf(s + 8), r->tyaw = getf(s + 12), r->tpitch = getf(s + 16);
    r->flags = s[20];
    if (!r->e.age) r->e.x = r->e.px = r->tx, r->e.y = r->e.py = r->ty, r->e.z = r->e.pz = r->tz, r->e.yaw = r->tyaw;
    r->e.age = 1;
  }
}

/* moves each player towards where they were last said to be, their legs swinging as they go */
static void players_tick(void) {
  for (int i = 0; i < MP_PLAYERS; i++) {
    Remote *r = &rem[i];
    if (!r->here) continue;
    Entity *e = &r->e;
    e->px = e->x, e->py = e->y, e->pz = e->z;
    e->x += (r->tx - e->x) * 0.4f, e->y += (r->ty - e->y) * 0.4f, e->z += (r->tz - e->z) * 0.4f;
    float dy = r->tyaw - e->yaw;
    while (dy > 180) dy -= 360;
    while (dy < -180) dy += 360;
    e->yaw += dy * 0.5f, e->pitch = r->tpitch;
    /* EntityLivingBase.limbSwing */
    float dx = e->x - e->px, dz = e->z - e->pz, d = sqrtf(dx * dx + dz * dz) * 4;
    if (d > 1) d = 1;
    e->limb_amt += (d - e->limb_amt) * 0.4f;
    e->limb += e->limb_amt;
    e->age++;
  }
}

/* ---------------------------------------------------------------- frames from NumSocial */
static void end_game(const char *why) {
  strncpy(mp_status, why, sizeof mp_status - 1);
  mp_status[sizeof mp_status - 1] = 0;
  bool guest = mp_mode == MP_GUEST || waiting_world;
  if (mp_mode == MP_GUEST) plat_remove_prefix("nb0");
  mp_mode = MP_OFF, waiting_world = false, mp_code[0] = 0;
  memset(rem, 0, sizeof rem);
  if (guest) menu_choice = ACT_MP_END;
  else chat_add(mp_status, CHAT_RED);
}

static void handle(int type, const uint8_t *p, int n) {
  switch (type) {
    case F_SERVER: {
      if (n < 1 || p[0] >= MP_MAX_SERVERS) break;
      MpServer *s = &mp_servers[p[0]];
      int at = 1;
      get_str8(p, n, &at, s->id, 11);
      get_str8(p, n, &at, s->world, 24);
      get_str8(p, n, &at, s->host, MP_NAME);
      s->players = at < n ? p[at] : 0, s->max = at + 1 < n ? p[at + 1] : 0, s->mode = at + 2 < n ? p[at + 2] : 0;
      if (p[0] >= mp_nservers) mp_nservers = p[0] + 1;
      break;
    }
    case F_LIST_DONE:
      mp_listing = false;
      mp_nservers = n > 0 && p[0] < MP_MAX_SERVERS ? p[0] : mp_nservers;
      break;
    case F_HOST_OK: {
      int at = 0;
      get_str8(p, n, &at, mp_code, 11);
      if (mp_code[0]) {
        mp_mode = MP_HOST;
        char line[48] = "Local game hosted, code ";
        strcat(line, mp_code);
        chat_add(line, CHAT_GRAY);
        chat_add("Others join it from Multiplayer", CHAT_GRAY);
      } else {
        lan_open = lan_cheats = false;
        char line[64] = "Could not open the game: ";
        int k = (int)strlen(line);
        for (int i = at; i < n && k < 63; i++) line[k++] = (char)p[i];
        line[k] = 0;
        chat_add(line, CHAT_RED);
      }
      break;
    }
    case F_WORLD: {
      if (!waiting_world || n < 27) break;
      uint64_t seed = 0;
      for (int i = 0; i < 8; i++) seed |= (uint64_t)p[i] << (8 * i);
      my_id = p[14];
      mp_world_begin((int64_t)seed, p[8], p[9], get32(p + 10), getf(p + 15), getf(p + 19), getf(p + 23));
      mp_mode = MP_GUEST;
      strcpy(mp_status, "Downloading terrain...");
      break;
    }
    case F_BLOCKS:
      if (mp_mode == MP_OFF) break;
      applying = true;
      for (int at = 0; at + ENTRY <= n; at += ENTRY)
        world_set_remote((int)get32(p + at), p[at + 4], (int)get32(p + at + 5), p[at + 9] | p[at + 10] << 8);
      applying = false;
      break;
    case F_READY:
      if (waiting_world && mp_mode == MP_GUEST) waiting_world = false, menu_choice = ACT_MP_PLAY;
      break;
    case F_PLAYERS:
      if (mp_mode != MP_OFF) players_frame(p, n);
      break;
    case F_PLAYER: {
      if (n < 2 || mp_mode == MP_OFF) break;
      Remote *r = remote(p[0], p[1] != 0);
      if (!r) break;
      if (!p[1]) {
        r->here = 0;
        break;
      }
      int at = 2;
      get_str8(p, n, &at, r->name, MP_NAME);
      r->named = 1;
      break;
    }
    case F_CHAT: {
      char line[104];
      int k = n < 103 ? n : 103;
      memcpy(line, p, (size_t)k), line[k] = 0;
      chat_add(line, CHAT_WHITE);
      break;
    }
    case F_TIME:
      if (mp_mode == MP_GUEST && n >= 4) game_time = get32(p);
      break;
    case F_END: {
      char why[64];
      int k = n - 1 < 63 ? n - 1 : 63;
      if (k < 0) k = 0;
      memcpy(why, p + 1, (size_t)k), why[k] = 0;
      end_game(why[0] ? why : "Disconnected");
      break;
    }
  }
}

void mp_frame(void) {
  if (mp_link != MPL_ONLINE) return;
  for (int k = 0; k < 64; k++) {
    uint8_t type;
    int n = plat_net_poll(&type, rx, sizeof rx);
    if (n < 0) break;
    handle(type, rx, n);
  }
  if (!plat_net_plugged()) {
    mp_link = MPL_NONE;
    plat_net_close();
    if (mp_mode != MP_OFF || waiting_world) end_game("The calculator was unplugged");
  }
}

void mp_tick(void) {
  if (mp_mode == MP_OFF) return;
  ticks++;
  if (n_out) send(F_BLOCK, out, n_out * ENTRY), n_out = 0;
  if (ticks % 4 == 0) {
    uint8_t s[STATE_SIZE];
    putf(s, pl.x), putf(s + 4, pl.y), putf(s + 8, pl.z), putf(s + 12, pl.yaw), putf(s + 16, pl.pitch);
    uint8_t f = (uint8_t)((pl.sneaking ? 1 : 0) | (pl.dead ? 4 : 0));
#if NB_DIMS
    if (dim != DIM_OVERWORLD) f |= 8;
#endif
    s[20] = f;
    s[21] = (uint8_t)held()->id, s[22] = (uint8_t)(held()->id >> 8);
    send(F_STATE, s, STATE_SIZE);
  }
  if (mp_mode == MP_HOST && ticks % 100 == 0) {
    uint8_t t[4];
    put32(t, game_time);
    send(F_TIME, t, 4);
  }
  players_tick();
}

/* ---------------------------------------------------------------- the players' skin
 * 64 x 32 as Minecraft lays a skin out, drawn here: hair, a face, a shirt of the
 * player's colour, blue trousers, grey shoes */
int mp_skin_texel(int k, int u, int v) {
  static const uint16_t shirt[8] = {RGB(0x00, 0xA8, 0xA8), RGB(0xC8, 0x30, 0x30), RGB(0x40, 0xA0, 0x30), RGB(0xE0, 0xC0, 0x20),
                                    RGB(0x80, 0x40, 0xB0), RGB(0xE0, 0x80, 0x20), RGB(0xE8, 0xE8, 0xE8), RGB(0xE0, 0x70, 0xA0)};
  const uint16_t skin = RGB(0xC8, 0x96, 0x78), hair = RGB(0x40, 0x28, 0x18), pants = RGB(0x30, 0x30, 0x90),
                 shoes = RGB(0x60, 0x60, 0x60), eye = RGB(0x30, 0x30, 0x90), white = RGB(0xFF, 0xFF, 0xFF);
  if ((unsigned)u >= 64 || (unsigned)v >= 32) return -1;
  if (v < 16) {
    if (u >= 32) return -1;                       /* (the hat layer: none) */
    if (v < 8) return u >= 8 && u < 16 ? hair : skin;   /* top: hair; bottom: chin */
    int f = u / 8, x = u % 8, y = v - 8;           /* faces: right, front, left, back */
    if (f == 3 || y < 2 || (f != 1 && y < 4 && (f == 0 ? x < 6 : x > 1))) return hair;
    if (f == 1) {
      if (y == 4 && (x == 1 || x == 6)) return white;
      if (y == 4 && (x == 2 || x == 5)) return eye;
      if (y == 6 && x >= 3 && x <= 4) return RGB(0x80, 0x50, 0x40);
    }
    return skin;
  }
  int y = v - 16;
  if (u < 16) return y >= 13 ? shoes : pants;     /* legs */
  if (u < 40) return shirt[k & 7];                /* body */
  if (u < 56) return y < 8 ? shirt[k & 7] : skin; /* arms: sleeves, then hands */
  return -1;
}
