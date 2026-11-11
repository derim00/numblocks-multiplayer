/* NumPlay's NumBlocks (no Nether, no End) opening the world dims.c left saved in the Nether: it is in the
 * Overworld where the player last stood there, holds nothing it does not know, and plays on. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/nb.h"

extern uint32_t host_keys, host_time;
void game_init(void);
bool game_frame(void);
void host_save_dir(const char *d);

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
  host_save_dir("build/dims-saves");
  float ex = 0, ey = 0, ez = 0;
  FILE *f = fopen("build/dims-saves/expect.txt", "r");
  CHECK(f && fscanf(f, "%f %f %f", &ex, &ey, &ez) == 3);
  if (f) fclose(f);
  game_init();
  play_slot = 1, menu_choice = ACT_PLAY;
  host_time += 50;
  game_frame();
  printf("  NumPlay's NumBlocks: at %.1f %.1f %.1f (the Overworld's place %.1f %.1f %.1f)\n", pl.x, pl.y, pl.z, ex, ey, ez);
  CHECK(fabsf(pl.x - ex) < 0.01f && fabsf(pl.z - ez) < 0.01f && pl.y >= ey - 0.01f);
  for (int i = 0; i < 36; i++) CHECK(pl.inv[i].id < I_END);
  bool cobble = false;
  for (int i = 0; i < 36; i++) cobble = cobble || (pl.inv[i].id == B_COBBLESTONE && pl.inv[i].aux == 20);
  CHECK(cobble);
  for (int y = -8; y <= 8; y++)
    for (int x = -8; x <= 8; x++) CHECK(world_get((int)pl.x + x, (int)pl.y + y, (int)pl.z) < B_N);
  for (int t = 0; t < 200; t++) host_time += 50, game_frame();
  CHECK(!pl.dead && pl.y > 0);
  printf("%s\n", fails ? "FAILED" : "all good (a world saved in the Nether, in NumPlay's NumBlocks)");
  return fails != 0;
}
