/* Multiplayer through NumSocial (github.com/derim00/numsocial): the calculator
 * talks to the NumSocial app on its computer over USB; the computer of the
 * player who opens their world is the game's server, and computers find each
 * other through a public list (see gateway/numblocks_service.py there). */
#ifndef NB_MP_H
#define NB_MP_H
#include <stdbool.h>
#include <stdint.h>
/* (included at the end of nb.h: Entity is known) */

enum { MP_OFF, MP_HOST, MP_GUEST };
extern int mp_mode;          /* MP_OFF: single player */
/* the link to NumSocial */
enum { MPL_NONE, MPL_NO_FIRMWARE, MPL_NO_APP, MPL_OFFLINE, MPL_ONLINE };
extern int mp_link;
#define MP_NAME 16
extern char mp_name[MP_NAME + 1];   /* the player's name, as others see it */
extern char mp_status[64];          /* what the multiplayer screens say */
extern char mp_code[12];            /* the open game's code */

typedef struct {
  char id[12], world[25], host[MP_NAME + 1];
  uint8_t players, max, mode;
} MpServer;
#define MP_MAX_SERVERS 12
extern MpServer mp_servers[MP_MAX_SERVERS];
extern int mp_nservers;
extern bool mp_listing;   /* a list was asked for and is on its way */

void mp_load_name(void);
void mp_save_name(void);
bool mp_connect(void);        /* to NumSocial (blocking; Back cancels): true when online */
void mp_list(void);           /* asks for the open games */
void mp_join(int i);          /* joins mp_servers[i] */
bool mp_host(int mode, bool cheats);   /* asks to open this world to others (mode: theirs); chat says when */
void mp_leave(void);          /* leaves (or closes) the game */
void mp_frame(void);          /* each frame: what NumSocial sent */
void mp_tick(void);           /* 20 a second in a world: positions out, players moved */
void mp_say(const char *line);
bool mp_applying(void);       /* (a block from the others is being set: not to send back) */
void mp_block_changed(int x, int y, int z, int b);   /* (world.c) */

/* the other players, drawn as entities (render.c), their names over them (gui.c) */
#define MP_PLAYERS 8
const Entity *mp_entity(int i);   /* i < MP_PLAYERS; NULL: nobody */
void mp_label(int i, int x, int y, bool shown);
bool mp_label_at(int i, int *x, int *y, const char **name);
int mp_skin_texel(int k, int u, int v);   /* the players' skin (k: its colours) */
#endif
