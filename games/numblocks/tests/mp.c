/* Multiplayer on a computer: one game per process, each talking to its own NumSocial gateway (as two
 * calculators on two computers would). tests/mp_test.py runs a host and a guest together.
 *
 *   mp host NAME SAVES      opens a new world (seed 4242), puts a gold block by the player, then waits
 *                           for a guest: their diamond block and them standing by
 *   mp join NAME SAVES      finds NAME's game... (any game hosted by "Alice"), joins it, checks the gold
 *                           block, puts a diamond block, walks, says hello
 * NB_NET_PORT: the gateway's port (numtube_gateway.py --tcp PORT). Prints what it checks; exit 0: all well. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
void game_init(void);
bool game_frame(void);
extern bool start_in_world;
extern int64_t start_seed;
extern int start_type;

static double now(void) {
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec + t.tv_usec / 1e6;
}
/* frames for that long (real time; the game's own time moves 50 ms a frame) */
static void run(double seconds) {
  double end = now() + seconds;
  while (now() < end) {
    host_time += 50;
    game_frame();
    plat_sleep(0);
    struct timespec ts = {0, 20 * 1000000};
    nanosleep(&ts, NULL);
  }
}
static bool run_until(bool (*cond)(void), double seconds) {
  double end = now() + seconds;
  while (now() < end) {
    run(0.05);
    if (cond()) return true;
  }
  return false;
}
static bool hosted(void) { return mp_code[0] != 0; }
static bool listed(void) { return !mp_listing; }
static bool in_game(void) { return mp_mode == MP_GUEST && gui == GUI_NONE; }
static int gx, gy, gz;   /* where the host's gold block is */
static bool guest_seen(void) {
  for (int i = 0; i < MP_PLAYERS; i++)
    if (mp_entity(i)) return true;
  return false;
}
static bool diamond_here(void) { return world_get(gx + 2, gy, gz) == B_DIAMOND_BLOCK; }

static int fails;
static void check(bool ok, const char *what) {
  printf("%s %s\n", ok ? "OK  " : "FAIL", what);
  fflush(stdout);
  if (!ok) fails++;
}

int main(int argc, char **argv) {
  if (argc < 4) return 2;
  bool host = !strcmp(argv[1], "host");
  mkdir(argv[3], 0755);
  host_save_dir(argv[3]);
  if (host) start_in_world = true, start_seed = 4242, start_type = WT_DEFAULT;
  game_init();
  strncpy(mp_name, argv[2], MP_NAME);
  opt.keys_seen = KEY_SHEET;
  if (gui == GUI_CONTROLS) gui_menu(GUI_TITLE);
  if (host) {
    run(1);
    gx = (int)floorf(pl.x) + 2, gy = (int)floorf(pl.y), gz = (int)floorf(pl.z);
    world_set(gx, gy, gz, B_GOLD_BLOCK);
    check(mp_host(0, false), "partie ouverte (Open to LAN)");
    check(run_until(hosted, 20), "code de la partie recu");
    printf("CODE %s AT %d %d %d\n", mp_code, gx, gy, gz);
    fflush(stdout);
    check(run_until(guest_seen, 60), "l'hote voit l'invite");
    check(run_until(diamond_here, 30), "le bloc de diamant de l'invite est arrive");
    run(3);
    for (int i = 0; i < MP_PLAYERS; i++) {
      const Entity *e = mp_entity(i);
      int x, y;
      const char *n;
      if (e) printf("  joueur a %.1f %.1f %.1f%s\n", e->x, e->y, e->z, mp_label_at(i, &x, &y, &n) ? "" : "");
    }
    host_shot(argc > 4 ? argv[4] : "build/mp_host.ppm");
    run(2);
    mp_leave();
    run(1);
  } else if (!strcmp(argv[1], "visit")) {
    /* joins the first game opened by someone else than "Mac", walks about, says hello */
    double end = now() + (argc > 4 ? atof(argv[4]) : 600);
    check(mp_connect(), "connecte a NumSocial, en ligne");
    int k = -1;
    while (k < 0 && now() < end) {
      mp_list();
      run_until(listed, 20);
      for (int i = 0; i < mp_nservers; i++)
        if (strcmp(mp_servers[i].host, "Mac")) k = i;
      if (k < 0) run(3);
    }
    if (k < 0) return 1;
    printf("REJOINT %s de %s\n", mp_servers[k].world, mp_servers[k].host);
    fflush(stdout);
    mp_join(k);
    gui_menu(GUI_MPWAIT);
    check(run_until(in_game, 60), "partie rejointe, monde charge");
    mp_say("Salut ! Je suis le bot du Mac");
    while (now() < end && mp_mode == MP_GUEST) {
      host_keys = K_FWD | K_RIGHT;
      run(2);
      host_keys = K_JUMP;
      run(1);
      host_keys = 0;
      run(2);
      for (int i = 0; i < MP_PLAYERS; i++)
        if (mp_entity(i)) printf("vois un joueur a %.1f %.1f %.1f\n", mp_entity(i)->x, mp_entity(i)->y, mp_entity(i)->z);
      fflush(stdout);
    }
    printf("fin: %s\n", mp_status);
    mp_leave();
    run(1);
  } else if (!strcmp(argv[1], "serve")) {
    /* a game open for a while (a calculator joins it): the player turns about, says hello now and then */
    double end = now() + (argc > 4 ? atof(argv[4]) : 600);
    check(mp_host(1, false), "partie ouverte");
    check(run_until(hosted, 20), "code de la partie recu");
    printf("CODE %s\n", mp_code);
    fflush(stdout);
    while (now() < end) {
      host_keys = K_FWD | K_LEFT;
      run(3);
      host_keys = 0;
      run(3);
    }
    mp_leave();
    run(1);
  } else {
    if (argc > 4) gx = atoi(argv[4]), gy = atoi(argv[5]), gz = atoi(argv[6]);
    check(mp_connect(), "connecte a NumSocial, en ligne");
    mp_list();
    check(run_until(listed, 20), "liste des parties recue");
    int k = -1;
    for (int i = 0; i < mp_nservers; i++)
      if (!strcmp(mp_servers[i].host, "Alice")) k = i;
    check(k >= 0, "la partie d'Alice est dans la liste");
    if (getenv("NB_SHOT_LIST")) {
      gui_menu(GUI_SERVERS);
      run(0.3);
      host_shot(getenv("NB_SHOT_LIST"));
    }
    if (k < 0) return 1;
    mp_join(k);
    gui_menu(GUI_MPWAIT);
    check(run_until(in_game, 40), "partie rejointe, monde charge");
    run(2);
    check(world_get(gx, gy, gz) == B_GOLD_BLOCK, "le bloc d'or d'Alice est la");
    check(guest_seen(), "l'invite voit Alice");
    world_set(gx + 2, gy, gz, B_DIAMOND_BLOCK);
    mp_say("salut Alice");
    pl.yaw = 0;
    host_keys = K_FWD;
    run(2);
    host_keys = 0;
    run(8);
  }
  printf(fails ? "ECHECS: %d\n" : "TOUT EST BON\n", fails);
  return fails != 0;
}
