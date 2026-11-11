/* NumBlocks.nwa only (NB_DIMS), included by gen_dims.h: strongholds (MapGenStronghold,
 * StructureStrongholdPieces). The three of a world are placed as 1.8.8 places them (a ring around the
 * world's middle, moved into a biome above the sea), laid out piece by piece from the same random, and
 * built into each chunk they cross after its populations, by the population boxes that hold them.
 *
 * What 1.8.8 draws from the population's random as it builds (which stone bricks are mossy or cracked,
 * the cobwebs, the torches, the eyes already in the portal's frames) is drawn here from the block's place,
 * so each block comes out the same whichever rows of its chunk are made. The liquid test that makes 1.8.8
 * leave a piece out looks at the terrain as made (the sea), from the chunk summaries. Silverfish are not
 * in NumBlocks: their stone bricks are stone bricks.
 *
 * The layout is made again in the scratch union U for each chunk inside a stronghold's box (a few hundred
 * pieces' tries); where the three are, their boxes and their stairs down are kept (ring). */

enum { SP_STRAIGHT, SP_PRISON, SP_LEFT, SP_RIGHT, SP_ROOM, SP_STAIRS_STRAIGHT, SP_STAIRS, SP_CROSSING, SP_CHEST,
       SP_LIBRARY, SP_PORTAL, SP_N, SP_CORRIDOR = SP_N, SP_START };
enum { DOOR_OPENING, DOOR_WOOD, DOOR_GRATES, DOOR_IRON };

/* PieceWeight: weight, most placed (0: any) */
static const uint8_t sw_weight[SP_N] = {40, 5, 20, 20, 10, 5, 5, 5, 5, 10, 20};
static const uint8_t sw_max[SP_N] = {0, 5, 0, 0, 6, 5, 5, 4, 4, 2, 1};
/* each piece's box: offset and size along its facing (a library tries 11 high, then 6) */
static const int8_t sp_box[SP_N][6] = {
    {-1, -1, 0, 5, 5, 7},   {-1, -1, 0, 9, 5, 11},  {-1, -1, 0, 5, 5, 5},   {-1, -1, 0, 5, 5, 5},
    {-4, -1, 0, 11, 7, 11}, {-1, -7, 0, 5, 11, 8},  {-1, -7, 0, 5, 11, 5},  {-4, -3, 0, 10, 9, 11},
    {-1, -1, 0, 5, 5, 7},   {-4, -1, 0, 14, 11, 15}, {-4, -1, 0, 11, 8, 16}};

/* a piece, packed (a layout may have 400): its corner, size, type and facing; flag holds its door (bits
 * 0-1), then a straight's two side ways (4, 8), a room's kind (bits 2-4), a crossing's four ways (4, 8, 16,
 * 32) */
typedef struct {
    int16_t x0, y0, z0;
    uint8_t sxz, sy, tf, flag;   /* (x size - 1) | (z size - 1) << 4, y size - 1; type << 2 | facing */
    uint8_t depth, pad;          /* (getComponentType) */
} SPiece;
#define SMAX 540
typedef struct {
    int n, npend, last, forced, portal;   /* pieces; waiting to build; the last weight placed; the next
                                           * piece's type if set (strongComponentType); a portal room made */
    uint8_t wl[SP_N], wn, cnt[SP_N];       /* the weights left (types), how many each placed */
    int16_t bb[6];                         /* all of it */
    SPiece p[SMAX];
    uint16_t pend[SMAX];
} Hold;
#define SH ((Hold *)(void *)&U)
_Static_assert(sizeof(Hold) <= sizeof U, "a stronghold's layout fits the scratch union");
/* piece i as an FPiece (gen_fortress.h's boxes and turns) */
static FPiece sp_at(int i) {
    const SPiece *s = &SH->p[i];
    FPiece p;
    p.x0 = s->x0, p.y0 = s->y0, p.z0 = s->z0;
    p.x1 = (int16_t)(s->x0 + (s->sxz & 15)), p.y1 = (int16_t)(s->y0 + s->sy), p.z1 = (int16_t)(s->z0 + (s->sxz >> 4));
    p.type = (uint8_t)(s->tf >> 2), p.face = s->tf & 3, p.flag = s->flag, p.depth = s->depth, p.seed = 0;
    return p;
}

/* where the three are: their start chunks, boxes, and the middle of their first piece (where an eye of
 * ender flies once the stronghold is made), placed one by one (stage) from the ring's random */
static struct {
    i64 seed;
    JRand r;
    double d0;
    uint8_t valid, stage, seen;   /* seen: those whose start 1.8.8 has made (a chunk within 8 of it was) */
    int16_t cx[3], cz[3], bb[3][6], mid[3][3];
} ring;

uint8_t *gen_hi;   /* (world.c) the ninth bits of the slab gen_slab fills, a bit a cell (none: NULL) */

static double jr_double(JRand *r) { return (double)jr_double_bits(r) * (1.0 / 9007199254740992.0); }

static int sh_meets(const int16_t *b) {   /* StructureComponent.findIntersecting: the first piece b meets */
    const Hold *h = SH;
    for (int i = 0; i < h->n; i++) {
        const SPiece *s = &h->p[i];
        if (b[3] >= s->x0 && b[0] <= s->x0 + (s->sxz & 15) && b[5] >= s->z0 && b[2] <= s->z0 + (s->sxz >> 4) &&
            b[4] >= s->y0 && b[1] <= s->y0 + s->sy)
            return i;
    }
    return -1;
}
static int sh_add(const int16_t *b, int type, int face, int depth) {
    Hold *h = SH;
    if (h->n >= SMAX) return -1;
    SPiece *p = &h->p[h->n];
    p->x0 = b[0], p->y0 = b[1], p->z0 = b[2];
    p->sxz = (uint8_t)((b[3] - b[0]) | (b[5] - b[2]) << 4), p->sy = (uint8_t)(b[4] - b[1]);
    p->tf = (uint8_t)(type << 2 | face), p->depth = (uint8_t)depth, p->flag = 0, p->pad = 0;
    return h->n++;
}

/* func_175954_a: a piece of a type there if its box is above y 10 and free, made with its draws; -1 */
static int sp_make(JRand *r, int t, int x, int y, int z, int face, int depth) {
    int16_t b[6];
    box_for(x, y, z, sp_box[t], face, b);
    if (b[1] <= 10 || sh_meets(b) >= 0) {
        static const int8_t small[6] = {-4, -1, 0, 14, 6, 15};
        if (t != SP_LIBRARY) return -1;
        box_for(x, y, z, small, face, b);
        if (b[1] <= 10 || sh_meets(b) >= 0) return -1;
    }
    /* (RightTurn has no factory of its own: LeftTurn's, inherited, makes a left turn) */
    int i = sh_add(b, t == SP_RIGHT ? SP_LEFT : t, face, depth);
    if (i < 0) return -1;
    SPiece *p = &SH->p[i];
    if (t != SP_PORTAL) {   /* getRandomDoor */
        int k = jr_int(r, 5);
        p->flag = (uint8_t)(k < 2 ? DOOR_OPENING : k - 1);
    }
    if (t == SP_STRAIGHT) {
        int ex = jr_int(r, 2) == 0, ez = jr_int(r, 2) == 0;
        p->flag |= (uint8_t)(ex << 2 | ez << 3);
    } else if (t == SP_ROOM) {
        p->flag |= (uint8_t)(jr_int(r, 5) << 2);
    } else if (t == SP_CROSSING) {
        int a = jr_bool(r), c = jr_bool(r), d = jr_bool(r), e = jr_int(r, 3) > 0;
        p->flag |= (uint8_t)(a << 2 | c << 3 | d << 4 | e << 5);
    }
    return i;
}

/* func_175955_b: the forced type, else a piece of the weights left (five tries), else a corridor up to what
 * is in the way; -1 */
static int sp_pick(JRand *r, int x, int y, int z, int face, int depth) {
    Hold *h = SH;
    int total = 0, any = 0;
    for (int i = 0; i < h->wn; i++) {
        int t = h->wl[i];
        if (sw_max[t] > 0 && h->cnt[t] < sw_max[t]) any = 1;
        total += sw_weight[t];
    }
    if (!any) return -1;
    if (h->forced >= 0) {
        int t = h->forced;
        h->forced = -1;
        int p = sp_make(r, t, x, y, z, face, depth);
        if (p >= 0) return p;
    }
    for (int j = 0; j < 5; j++) {
        int k = jr_int(r, total);
        for (int i = 0; i < h->wn; i++) {
            int t = h->wl[i];
            k -= sw_weight[t];
            if (k < 0) {
                if (!(sw_max[t] == 0 || h->cnt[t] < sw_max[t]) || (t == SP_LIBRARY && depth <= 4) ||
                    (t == SP_PORTAL && depth <= 5) || t == h->last)
                    break;
                int p = sp_make(r, t, x, y, z, face, depth);
                if (p >= 0) {
                    h->cnt[t]++;
                    h->last = t;
                    if (!(sw_max[t] == 0 || h->cnt[t] < sw_max[t])) {
                        memmove(&h->wl[i], &h->wl[i + 1], (size_t)(h->wn - i - 1));
                        h->wn--;
                    }
                    return p;
                }
            }
        }
    }
    /* Corridor.func_175869_a: 4 long if it runs into a piece at its height, shortened to end before it */
    int8_t cb[6] = {-1, -1, 0, 5, 5, 4};
    int16_t b[6];
    box_for(x, y, z, cb, face, b);
    int o = sh_meets(b);
    if (o < 0) return -1;
    FPiece q = sp_at(o);
    if (q.y0 != b[1]) return -1;
    for (int j = 3; j >= 1; j--) {
        cb[5] = (int8_t)(j - 1);
        box_for(x, y, z, cb, face, b);
        if (!boxes_meet(b, &q)) {
            cb[5] = (int8_t)j;
            box_for(x, y, z, cb, face, b);
            return b[1] > 1 ? sh_add(b, SP_CORRIDOR, face, depth) : -1;
        }
    }
    return -1;
}

/* func_175953_c: the next piece there (none past depth 50 or 112 from the start), waiting to be built */
static void sp_next(JRand *r, int x, int y, int z, int face, int depth) {
    Hold *h = SH;
    if (depth > 50 || iabs(x - h->p[0].x0) > 112 || iabs(z - h->p[0].z0) > 112) return;
    int p = sp_pick(r, x, y, z, face, depth + 1);
    if (p >= 0 && h->npend < SMAX) h->pend[h->npend++] = (uint16_t)p;
}
/* getNextComponentNormal (across, up), X (to the left: up, across) and Z (to the right) */
static void sp_normal(JRand *r, int pi, int lat, int dy) {
    FPiece p = sp_at(pi);
    switch (p.face) {
    case F_N: sp_next(r, p.x0 + lat, p.y0 + dy, p.z0 - 1, F_N, p.depth); break;
    case F_S: sp_next(r, p.x0 + lat, p.y0 + dy, p.z1 + 1, F_S, p.depth); break;
    case F_W: sp_next(r, p.x0 - 1, p.y0 + dy, p.z0 + lat, F_W, p.depth); break;
    default: sp_next(r, p.x1 + 1, p.y0 + dy, p.z0 + lat, F_E, p.depth); break;
    }
}
static void sp_left(JRand *r, int pi, int dy, int lat) {
    FPiece p = sp_at(pi);
    if (p.face == F_N || p.face == F_S) sp_next(r, p.x0 - 1, p.y0 + dy, p.z0 + lat, F_W, p.depth);
    else sp_next(r, p.x0 + lat, p.y0 + dy, p.z0 - 1, F_N, p.depth);
}
static void sp_right(JRand *r, int pi, int dy, int lat) {
    FPiece p = sp_at(pi);
    if (p.face == F_N || p.face == F_S) sp_next(r, p.x1 + 1, p.y0 + dy, p.z0 + lat, F_E, p.depth);
    else sp_next(r, p.x0 + lat, p.y0 + dy, p.z1 + 1, F_S, p.depth);
}
/* buildComponent */
static void sp_build(JRand *r, int pi) {
    FPiece p = sp_at(pi);
    int f = p.face, fl = p.flag;
    switch (p.type) {
    case SP_START: SH->forced = SP_CROSSING; /* fall through */
    case SP_STAIRS:
    case SP_PRISON:
    case SP_STAIRS_STRAIGHT:
    case SP_CHEST: sp_normal(r, pi, 1, 1); break;
    case SP_STRAIGHT:
        sp_normal(r, pi, 1, 1);
        if (fl & 4) sp_left(r, pi, 1, 2);
        if (fl & 8) sp_right(r, pi, 1, 2);
        break;
    case SP_LEFT:
        if (f != F_N && f != F_E) sp_right(r, pi, 1, 1);
        else sp_left(r, pi, 1, 1);
        break;
    case SP_RIGHT:
        if (f != F_N && f != F_E) sp_left(r, pi, 1, 1);
        else sp_right(r, pi, 1, 1);
        break;
    case SP_ROOM: sp_normal(r, pi, 4, 1), sp_left(r, pi, 1, 4), sp_right(r, pi, 1, 4); break;
    case SP_CROSSING: {
        int i = 3, j = 5;
        if (f == F_W || f == F_N) i = 5, j = 3;
        sp_normal(r, pi, 5, 1);
        if (fl & 4) sp_left(r, pi, i, 1);
        if (fl & 8) sp_left(r, pi, j, 7);
        if (fl & 16) sp_right(r, pi, i, 1);
        if (fl & 32) sp_right(r, pi, j, 7);
        break;
    }
    case SP_PORTAL: SH->portal = 1; break;
    }
}

/* MapGenStronghold.getStructureStart for stronghold s: Starts from the chunk's MapGenBase random until one
 * has a portal room, its box, and markAvailableHeight(10): its top below the sea at random */
static void sh_layout(int s) {
    Hold *h = SH;
    JRand r;
    int cx = ring.cx[s], cz = ring.cz[s];
    jr_seed(&r, (i64)((u64)(i64)cx * (u64)cave_mul_x) ^ (i64)((u64)(i64)cz * (u64)cave_mul_z) ^ g_seed);
    jr_next(&r, 32);
    do {
        h->n = h->npend = 0, h->last = h->forced = -1, h->portal = 0;
        h->wn = SP_N;
        for (int i = 0; i < SP_N; i++) h->wl[i] = (uint8_t)i, h->cnt[i] = 0;
        int x = cx * 16 + 2, z = cz * 16 + 2;
        int16_t b[6] = {(int16_t)x, 64, (int16_t)z, (int16_t)(x + 4), 74, (int16_t)(z + 4)};
        sh_add(b, SP_START, jr_int(&r, 4), 0);
        sp_build(&r, 0);
        while (h->npend) {
            int i = jr_int(&r, h->npend), p = h->pend[i];
            memmove(&h->pend[i], &h->pend[i + 1], (size_t)(h->npend - i - 1) * sizeof h->pend[0]);
            h->npend--;
            sp_build(&r, p);
        }
        int16_t *bb = h->bb;
        bb[0] = bb[1] = bb[2] = 32767, bb[3] = bb[4] = bb[5] = -32768;
        for (int i = 0; i < h->n; i++) {
            FPiece p = sp_at(i);
            if (p.x0 < bb[0]) bb[0] = p.x0;
            if (p.y0 < bb[1]) bb[1] = p.y0;
            if (p.z0 < bb[2]) bb[2] = p.z0;
            if (p.x1 > bb[3]) bb[3] = p.x1;
            if (p.y1 > bb[4]) bb[4] = p.y1;
            if (p.z1 > bb[5]) bb[5] = p.z1;
        }
        int top = 63 - 10, j = bb[4] - bb[1] + 2;
        if (j < top) j += jr_int(&r, top - j);
        int k = j - bb[4];
        bb[1] += k, bb[4] += k;
        for (int i = 0; i < h->n; i++) h->p[i].y0 = (int16_t)(h->p[i].y0 + k);
    } while (!h->portal);
}

/* WorldChunkManager.findBiomePosition(x, z, range, biomes above the sea): one of the 1:4 cells of such a
 * biome within range, at random (as gen_spawn) */
static int sh_biome(int x, int z, int range, JRand *r, int *ox, int *oz) {
    int i0 = (x - range) >> 2, j0 = (z - range) >> 2, w = ((x + range) >> 2) - i0 + 1, hh = ((z + range) >> 2) - j0 + 1;
    uint8_t *rows = (uint8_t *)U.lay + sizeof(i32) * LAY_INTS; /* 8 rows, after the layer scratch */
    int found = 0, k1 = 0;
    for (int tz = 0; tz < hh; tz += 8) {
        int th = hh - tz < 8 ? hh - tz : 8;
        for (int tx = 0; tx < w; tx += 43) {
            int tw = w - tx < 43 ? w - tx : 43;
            i32 *rm = U.lay;
            lay_mem = U.lay + 43 * 8;
            lay_cap = LAY_INTS - 43 * 8;
            lay_top = 0;
            layers_rivermix(i0 + tx, j0 + tz, tw, th, rm);
            for (int j = 0; j < th; j++)
                for (int i = 0; i < tw; i++) rows[j * w + tx + i] = (uint8_t)rm[j * tw + i];
        }
        for (int j = 0; j < th; j++)
            for (int i = 0; i < w; i++) {
                int id = rows[j * w + i];
                if (bio_exists(id) && bio(id)->depth > 0.0f && (!found || jr_int(r, k1 + 1) == 0)) {
                    *ox = (i0 + i) * 4, *oz = (j0 + tz + j) * 4;
                    found = 1, k1++;
                }
            }
    }
    return found;
}

/* MapGenStronghold.canSpawnStructureAtCoords's first call, one stronghold at a time: 0 when all are placed */
static int sh_ring_step(void) {
    if (!ring.valid || ring.seed != g_seed) {
        ring.valid = 1, ring.seed = g_seed, ring.stage = 0, ring.seen = 0;
        jr_seed(&ring.r, g_seed);
        ring.d0 = jr_double(&ring.r) * 3.141592653589793 * 2.0;
    }
    int j = ring.stage;
    if (j >= 3) return 0;
    /* (1.25 i + random) 32 i chunks away, i staying 1 for three */
    double d1 = (1.25 + jr_double(&ring.r)) * 32.0;
    int k = (int)floor(cos(ring.d0) * d1 + 0.5), l = (int)floor(sin(ring.d0) * d1 + 0.5), bx, bz;
    if (sh_biome(k * 16 + 8, l * 16 + 8, 112, &ring.r, &bx, &bz)) k = bx >> 4, l = bz >> 4;
    ring.cx[j] = (int16_t)k, ring.cz[j] = (int16_t)l;
    ring.d0 += 3.141592653589793 * 2.0 / 3.0;
    sh_layout(j);
    memcpy(ring.bb[j], SH->bb, sizeof ring.bb[j]);
    FPiece p = sp_at(0);   /* getBoundingBoxCenter */
    ring.mid[j][0] = (int16_t)(p.x0 + (p.x1 - p.x0 + 1) / 2);
    ring.mid[j][1] = (int16_t)(p.y0 + (p.y1 - p.y0 + 1) / 2);
    ring.mid[j][2] = (int16_t)(p.z0 + (p.z1 - p.z0 + 1) / 2);
    ring.stage++;
    return 1;
}
static void sh_ring(void) {
    while (sh_ring_step()) {}
}
/* can a stronghold reach chunk (cx, cz)? Not within 240 of the world's middle: the nearest start is 40
 * chunks out, less 115 blocks for the biome, and its pieces end within 128 of it */
static int sh_far(int cx, int cz) {
    i64 x = cx * 16 + 8, z = cz * 16 + 8;
    return !g_flat && !gd_dim && x * x + z * z > 240 * 240;
}

/* ---- building a piece into the slab of chunk C: its blocks, 9 bits (gen_hi), where 1.8.8's population
 * box (inside C) lets it */
typedef struct {
    const FPiece *p;
    int x0, z0, x1, z1;   /* the part of the population's box in C (world) */
    int y0, h;            /* the slab's rows */
    uint8_t *out;
} SB;
static int sb_cell(const SB *b, int x, int y, int z) {
    const FPiece *p = b->p;
    int wx = fb_x(p, x, z), wy = p->y0 + y, wz = fb_z(p, x, z), ly = wy - b->y0;
    if (wx < b->x0 || wx > b->x1 || wz < b->z0 || wz > b->z1 || wy < 1 || ly < 0 || ly >= b->h) return -1;
    return (ly * 16 + (wz & 15)) * 16 + (wx & 15);
}
static int sb_get(const SB *b, int c) { return b->out[c] | (gen_hi ? (gen_hi[c >> 3] >> (c & 7) & 1) << 8 : 0); }
static void sb_put(const SB *b, int c, int v) {
    if (!gen_hi) {
        if (v <= 255) b->out[c] = (uint8_t)v;
        return;
    }
    b->out[c] = (uint8_t)v;
    uint8_t m = (uint8_t)(1 << (c & 7));
    if (v > 255) gen_hi[c >> 3] |= m;
    else gen_hi[c >> 3] &= (uint8_t)~m;
}
static void sb_set(const SB *b, int x, int y, int z, int v) {
    int c = sb_cell(b, x, y, z);
    if (c >= 0) sb_put(b, c, v);
}
/* the population's random, here from the block's place */
static float sb_rnd(const SB *b, int x, int y, int z, u32 salt) {
    u32 h = (u32)g_seed ^ (u32)((u64)g_seed >> 32) ^ salt * 0x9E3779B9u;
    h ^= (u32)fb_x(b->p, x, z) * 0x85EBCA6Bu;
    h = (h << 13 | h >> 19) * 5 + 0xE6546B64u;
    h ^= (u32)fb_z(b->p, x, z) * 0xC2B2AE35u;
    h = (h << 13 | h >> 19) * 5 + 0xE6546B64u;
    h ^= (u32)(b->p->y0 + y) * 0x27D4EB2Fu;
    h ^= h >> 16, h *= 0x85EBCA6Bu, h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}
/* fillWithBlocks (the same block on the edges and inside) */
static void sb_fill(const SB *b, int x0, int y0, int z0, int x1, int y1, int z1, int v) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) sb_set(b, x, y, z, v);
}
/* fillWithRandomizedBlocks with the Stones: stone bricks on the edges (cracked one in five, mossy three in
 * ten), air inside; with existing, only over what is not air */
static void sb_stones(const SB *b, int x0, int y0, int z0, int x1, int y1, int z1, int existing) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) {
                int c = sb_cell(b, x, y, z);
                if (c < 0 || (existing && sb_get(b, c) == B_AIR)) continue;
                int v = B_AIR;
                if (y == y0 || y == y1 || x == x0 || x == x1 || z == z0 || z == z1) {
                    float f = sb_rnd(b, x, y, z, 1);
                    v = f < 0.2f ? B_CRACKED_STONE_BRICKS : f < 0.5f ? B_MOSSY_STONE_BRICKS : B_STONE_BRICKS;
                }
                sb_put(b, c, v);
            }
}
/* randomlyPlaceBlock; func_175805_a (cobwebs) */
static void sb_maybe(const SB *b, float chance, int x, int y, int z, int v) {
    if (sb_rnd(b, x, y, z, 2) < chance) sb_set(b, x, y, z, v);
}
static void sb_webs(const SB *b, float chance, int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++)
                if (sb_rnd(b, x, y, z, 3) <= chance) sb_set(b, x, y, z, B_COBWEB);
}
/* getMetadataWithOffset for stairs (0 east, 1 west, 2 south, 3 north) and buttons (1 east, 2 west, 3 south,
 * 4 north), as the piece faces */
static int sb_stairs(int face, int m) {
    static const uint8_t rot[4][4] = {{0, 1, 2, 3}, {2, 3, 1, 0}, {0, 1, 3, 2}, {2, 3, 0, 1}};   /* N, E, S, W */
    return rot[face][m];
}
static int sb_button(int face, int m) {
    static const uint8_t rot[4][5] = {{0, 1, 2, 3, 4}, {0, 4, 3, 2, 1}, {0, 1, 2, 4, 3}, {0, 4, 3, 1, 2}};
    static const uint16_t blk[5] = {B_STONE_BUTTON_N, B_STONE_BUTTON_E, B_STONE_BUTTON_W, B_STONE_BUTTON_S,
                                    B_STONE_BUTTON_N};
    return blk[rot[face][m]];
}
/* Stronghold.placeDoor (the doors are set as they are, unturned: 1.8.8 does not turn them) */
static void sb_door(const SB *b, int d, int x, int y, int z) {
    if (d == DOOR_OPENING) {
        sb_fill(b, x, y, z, x + 2, y + 2, z, B_AIR);
        return;
    }
    int wall = d == DOOR_GRATES ? B_IRON_BARS : B_STONE_BRICKS;
    if (d == DOOR_GRATES) sb_set(b, x + 1, y, z, B_AIR), sb_set(b, x + 1, y + 1, z, B_AIR);
    sb_set(b, x, y, z, wall), sb_set(b, x, y + 1, z, wall), sb_set(b, x, y + 2, z, wall);
    sb_set(b, x + 1, y + 2, z, wall), sb_set(b, x + 2, y + 2, z, wall), sb_set(b, x + 2, y + 1, z, wall);
    sb_set(b, x + 2, y, z, wall);
    if (d == DOOR_WOOD) sb_set(b, x + 1, y, z, B_DOOR_OAK_LOWER), sb_set(b, x + 1, y + 1, z, B_DOOR_OAK_UPPER);
    if (d == DOOR_IRON) {
        sb_set(b, x + 1, y, z, B_IRON_DOOR_LOWER_E), sb_set(b, x + 1, y + 1, z, B_IRON_DOOR_UPPER);
        sb_set(b, x + 2, y + 1, z + 1, sb_button(b->p->face, 4));
        sb_set(b, x + 2, y + 1, z - 1, sb_button(b->p->face, 3));
    }
}
#define SBK B_STONE_BRICKS
/* addComponentParts */
static void sp_parts(const SB *b) {
    const FPiece *p = b->p;
    int d = p->flag & 3, fl = p->flag, f = p->face;
    switch (p->type) {
    case SP_START:
    case SP_STAIRS: {
        /* a spiral of bricks and slabs, from the door up to the door down */
        static const int8_t st[17][4] = {{2, 6, 1, 0}, {1, 5, 1, 0}, {1, 6, 1, 1}, {1, 5, 2, 0}, {1, 4, 3, 0}, {1, 5, 3, 1},
                                         {2, 4, 3, 0}, {3, 3, 3, 0}, {3, 4, 3, 1}, {3, 3, 2, 0}, {3, 2, 1, 0}, {3, 3, 1, 1},
                                         {2, 2, 1, 0}, {1, 1, 1, 0}, {1, 2, 1, 1}, {1, 1, 2, 0}, {1, 1, 3, 1}};
        sb_stones(b, 0, 0, 0, 4, 10, 4, 1);
        sb_door(b, d, 1, 7, 0);
        sb_door(b, DOOR_OPENING, 1, 1, 4);
        for (int i = 0; i < 17; i++) sb_set(b, st[i][0], st[i][1], st[i][2], st[i][3] ? B_STONE_SLAB : SBK);
        break;
    }
    case SP_STRAIGHT:
        sb_stones(b, 0, 0, 0, 4, 4, 6, 1);
        sb_door(b, d, 1, 1, 0);
        sb_door(b, DOOR_OPENING, 1, 1, 6);
        sb_maybe(b, 0.1f, 1, 2, 1, B_TORCH_E);
        sb_maybe(b, 0.1f, 3, 2, 1, B_TORCH_W);
        sb_maybe(b, 0.1f, 1, 2, 5, B_TORCH_E);
        sb_maybe(b, 0.1f, 3, 2, 5, B_TORCH_W);
        if (fl & 4) sb_fill(b, 0, 1, 2, 0, 3, 4, B_AIR);
        if (fl & 8) sb_fill(b, 4, 1, 2, 4, 3, 4, B_AIR);
        break;
    case SP_PRISON: {
        int m = f == F_W ? 0 : f == F_E ? 2 : 3;   /* (getMetadataWithOffset(iron_door, 3)) */
        sb_stones(b, 0, 0, 0, 8, 4, 10, 1);
        sb_door(b, d, 1, 1, 0);
        sb_fill(b, 1, 1, 10, 3, 3, 10, B_AIR);
        sb_stones(b, 4, 1, 1, 4, 3, 1, 0);
        sb_stones(b, 4, 1, 3, 4, 3, 3, 0);
        sb_stones(b, 4, 1, 7, 4, 3, 7, 0);
        sb_stones(b, 4, 1, 9, 4, 3, 9, 0);
        sb_fill(b, 4, 1, 4, 4, 3, 6, B_IRON_BARS);
        sb_fill(b, 5, 1, 5, 7, 3, 5, B_IRON_BARS);
        sb_set(b, 4, 3, 2, B_IRON_BARS);
        sb_set(b, 4, 3, 8, B_IRON_BARS);
        sb_set(b, 4, 1, 2, B_IRON_DOOR_LOWER_E + m), sb_set(b, 4, 2, 2, B_IRON_DOOR_UPPER);
        sb_set(b, 4, 1, 8, B_IRON_DOOR_LOWER_E + m), sb_set(b, 4, 2, 8, B_IRON_DOOR_UPPER);
        break;
    }
    case SP_LEFT:
    case SP_RIGHT: {
        int x = ((f != F_N && f != F_E) != (p->type == SP_RIGHT)) ? 4 : 0;
        sb_stones(b, 0, 0, 0, 4, 4, 4, 1);
        sb_door(b, d, 1, 1, 0);
        sb_fill(b, x, 1, 1, x, 3, 3, B_AIR);
        break;
    }
    case SP_ROOM:
        sb_stones(b, 0, 0, 0, 10, 6, 10, 1);
        sb_door(b, d, 4, 1, 0);
        sb_fill(b, 4, 1, 10, 6, 3, 10, B_AIR);
        sb_fill(b, 0, 1, 4, 0, 3, 6, B_AIR);
        sb_fill(b, 10, 1, 4, 10, 3, 6, B_AIR);
        switch (fl >> 2 & 7) {
        case 0:   /* a pillar with four torches, on slabs */
            sb_fill(b, 5, 1, 5, 5, 3, 5, SBK);
            sb_set(b, 4, 3, 5, B_TORCH_W), sb_set(b, 6, 3, 5, B_TORCH_E);
            sb_set(b, 5, 3, 4, B_TORCH_S), sb_set(b, 5, 3, 6, B_TORCH_N);
            sb_fill(b, 4, 1, 4, 4, 1, 6, B_STONE_SLAB), sb_fill(b, 6, 1, 4, 6, 1, 6, B_STONE_SLAB);
            sb_set(b, 5, 1, 4, B_STONE_SLAB), sb_set(b, 5, 1, 6, B_STONE_SLAB);
            break;
        case 1:   /* a fountain */
            for (int i = 0; i < 5; i++) {
                sb_set(b, 3, 1, 3 + i, SBK), sb_set(b, 7, 1, 3 + i, SBK);
                sb_set(b, 3 + i, 1, 3, SBK), sb_set(b, 3 + i, 1, 7, SBK);
            }
            sb_fill(b, 5, 1, 5, 5, 3, 5, SBK);
            sb_set(b, 5, 4, 5, B_WATER);
            break;
        case 2:   /* a gallery on cobblestone, a ladder, a chest */
            for (int i = 1; i <= 9; i++) sb_set(b, 1, 3, i, B_COBBLESTONE), sb_set(b, 9, 3, i, B_COBBLESTONE);
            for (int i = 1; i <= 9; i++) sb_set(b, i, 3, 1, B_COBBLESTONE), sb_set(b, i, 3, 9, B_COBBLESTONE);
            sb_set(b, 5, 1, 4, B_COBBLESTONE), sb_set(b, 5, 1, 6, B_COBBLESTONE), sb_set(b, 5, 3, 4, B_COBBLESTONE);
            sb_set(b, 5, 3, 6, B_COBBLESTONE), sb_set(b, 4, 1, 5, B_COBBLESTONE), sb_set(b, 6, 1, 5, B_COBBLESTONE);
            sb_set(b, 4, 3, 5, B_COBBLESTONE), sb_set(b, 6, 3, 5, B_COBBLESTONE);
            for (int k = 1; k <= 3; k++) {
                sb_set(b, 4, k, 4, B_COBBLESTONE), sb_set(b, 6, k, 4, B_COBBLESTONE);
                sb_set(b, 4, k, 6, B_COBBLESTONE), sb_set(b, 6, k, 6, B_COBBLESTONE);
            }
            sb_set(b, 5, 3, 5, B_TORCH);
            for (int l = 2; l <= 8; l++) {
                sb_set(b, 2, 3, l, B_PLANKS_OAK), sb_set(b, 3, 3, l, B_PLANKS_OAK);
                if (l <= 3 || l >= 7) sb_fill(b, 4, 3, l, 6, 3, l, B_PLANKS_OAK);
                sb_set(b, 7, 3, l, B_PLANKS_OAK), sb_set(b, 8, 3, l, B_PLANKS_OAK);
            }
            sb_fill(b, 9, 1, 3, 9, 3, 3, B_LADDER);
            sb_set(b, 3, 4, 8, B_CHEST);
            break;
        }
        break;
    case SP_STAIRS_STRAIGHT: {
        static const uint16_t cs[4] = {B_COBBLESTONE_STAIRS, B_COBBLESTONE_STAIRS_W, B_COBBLESTONE_STAIRS_S,
                                       B_COBBLESTONE_STAIRS_N};
        int s = cs[sb_stairs(f, 2)];
        sb_stones(b, 0, 0, 0, 4, 10, 7, 1);
        sb_door(b, d, 1, 7, 0);
        sb_door(b, DOOR_OPENING, 1, 1, 7);
        for (int j = 0; j < 6; j++) {
            sb_fill(b, 1, 6 - j, 1 + j, 3, 6 - j, 1 + j, s);
            if (j < 5) sb_fill(b, 1, 5 - j, 1 + j, 3, 5 - j, 1 + j, SBK);
        }
        break;
    }
    case SP_CROSSING:
        sb_stones(b, 0, 0, 0, 9, 8, 10, 1);
        sb_door(b, d, 4, 3, 0);
        if (fl & 4) sb_fill(b, 0, 3, 1, 0, 5, 3, B_AIR);
        if (fl & 16) sb_fill(b, 9, 3, 1, 9, 5, 3, B_AIR);
        if (fl & 8) sb_fill(b, 0, 5, 7, 0, 7, 9, B_AIR);
        if (fl & 32) sb_fill(b, 9, 5, 7, 9, 7, 9, B_AIR);
        sb_fill(b, 5, 1, 10, 7, 3, 10, B_AIR);
        sb_stones(b, 1, 2, 1, 8, 2, 6, 0);
        sb_stones(b, 4, 1, 5, 4, 4, 9, 0);
        sb_stones(b, 8, 1, 5, 8, 4, 9, 0);
        sb_stones(b, 1, 4, 7, 3, 4, 9, 0);
        sb_stones(b, 1, 3, 5, 3, 3, 6, 0);
        sb_fill(b, 1, 3, 4, 3, 3, 4, B_STONE_SLAB);
        sb_fill(b, 1, 4, 6, 3, 4, 6, B_STONE_SLAB);
        sb_stones(b, 5, 1, 7, 7, 1, 8, 0);
        sb_fill(b, 5, 1, 9, 7, 1, 9, B_STONE_SLAB);
        sb_fill(b, 5, 2, 7, 7, 2, 7, B_STONE_SLAB);
        sb_fill(b, 4, 5, 7, 4, 5, 9, B_STONE_SLAB);
        sb_fill(b, 8, 5, 7, 8, 5, 9, B_STONE_SLAB);
        sb_fill(b, 5, 5, 7, 7, 5, 9, B_STONE_SLAB_DOUBLE);
        sb_set(b, 6, 5, 6, B_TORCH_S);
        break;
    case SP_CHEST:
        sb_stones(b, 0, 0, 0, 4, 4, 6, 1);
        sb_door(b, d, 1, 1, 0);
        sb_door(b, DOOR_OPENING, 1, 1, 6);
        sb_fill(b, 3, 1, 2, 3, 1, 4, SBK);
        sb_set(b, 3, 1, 1, B_STONE_BRICK_SLAB), sb_set(b, 3, 1, 5, B_STONE_BRICK_SLAB);
        sb_set(b, 3, 2, 2, B_STONE_BRICK_SLAB), sb_set(b, 3, 2, 4, B_STONE_BRICK_SLAB);
        sb_fill(b, 2, 1, 2, 2, 1, 4, B_STONE_BRICK_SLAB);
        sb_set(b, 3, 2, 3, B_CHEST);
        break;
    case SP_LIBRARY: {
        static const int8_t fence[8][2] = {{6, 9}, {7, 9}, {6, 8}, {7, 8}, {6, 7}, {7, 7}, {5, 7}, {8, 7}};
        int big = p->y1 - p->y0 + 1 > 6;
        sb_stones(b, 0, 0, 0, 13, big ? 10 : 5, 14, 1);
        sb_door(b, d, 4, 1, 0);
        sb_webs(b, 0.07f, 2, 1, 1, 11, 4, 13);
        for (int l = 1; l <= 13; l++) {
            int v = (l - 1) % 4 == 0 ? B_PLANKS_OAK : B_BOOKSHELF;
            sb_fill(b, 1, 1, l, 1, 4, l, v), sb_fill(b, 12, 1, l, 12, 4, l, v);
            if (v == B_PLANKS_OAK) sb_set(b, 2, 3, l, B_TORCH_E), sb_set(b, 11, 3, l, B_TORCH_W);
            if (big) sb_fill(b, 1, 6, l, 1, 9, l, v), sb_fill(b, 12, 6, l, 12, 9, l, v);
        }
        for (int k = 3; k < 12; k += 2) {
            sb_fill(b, 3, 1, k, 4, 3, k, B_BOOKSHELF);
            sb_fill(b, 6, 1, k, 7, 3, k, B_BOOKSHELF);
            sb_fill(b, 9, 1, k, 10, 3, k, B_BOOKSHELF);
        }
        if (big) {
            /* the gallery, its rail, the ladder up, the chandelier */
            sb_fill(b, 1, 5, 1, 3, 5, 13, B_PLANKS_OAK), sb_fill(b, 10, 5, 1, 12, 5, 13, B_PLANKS_OAK);
            sb_fill(b, 4, 5, 1, 9, 5, 2, B_PLANKS_OAK), sb_fill(b, 4, 5, 12, 9, 5, 13, B_PLANKS_OAK);
            sb_set(b, 9, 5, 11, B_PLANKS_OAK), sb_set(b, 8, 5, 11, B_PLANKS_OAK), sb_set(b, 9, 5, 10, B_PLANKS_OAK);
            sb_fill(b, 3, 6, 2, 3, 6, 12, B_FENCE_OAK), sb_fill(b, 10, 6, 2, 10, 6, 10, B_FENCE_OAK);
            sb_fill(b, 4, 6, 2, 9, 6, 2, B_FENCE_OAK), sb_fill(b, 4, 6, 12, 8, 6, 12, B_FENCE_OAK);
            sb_set(b, 9, 6, 11, B_FENCE_OAK), sb_set(b, 8, 6, 11, B_FENCE_OAK), sb_set(b, 9, 6, 10, B_FENCE_OAK);
            sb_fill(b, 10, 1, 13, 10, 7, 13, B_LADDER);
            for (int i = 0; i < 8; i++) sb_set(b, fence[i][0], fence[i][1], 7, B_FENCE_OAK);
            sb_set(b, 6, 7, 6, B_FENCE_OAK), sb_set(b, 7, 7, 6, B_FENCE_OAK);
            sb_set(b, 6, 7, 8, B_FENCE_OAK), sb_set(b, 7, 7, 8, B_FENCE_OAK);
            sb_set(b, 5, 8, 7, B_TORCH), sb_set(b, 8, 8, 7, B_TORCH), sb_set(b, 6, 8, 6, B_TORCH);
            sb_set(b, 7, 8, 6, B_TORCH), sb_set(b, 6, 8, 8, B_TORCH), sb_set(b, 7, 8, 8, B_TORCH);
        }
        sb_set(b, 3, 3, 5, B_CHEST);
        if (big) sb_set(b, 12, 9, 1, B_AIR), sb_set(b, 12, 8, 1, B_CHEST);
        break;
    }
    case SP_PORTAL: {
        /* the lava pits, the stairs up, the frame with an eye in one in ten, the silverfish spawner */
        static const int8_t fr[12][2] = {{4, 8}, {5, 8}, {6, 8}, {4, 12}, {5, 12}, {6, 12},
                                         {3, 9}, {3, 10}, {3, 11}, {7, 9}, {7, 10}, {7, 11}};
        sb_stones(b, 0, 0, 0, 10, 7, 15, 0);
        sb_door(b, DOOR_GRATES, 4, 1, 0);
        sb_stones(b, 1, 6, 1, 1, 6, 14, 0);
        sb_stones(b, 9, 6, 1, 9, 6, 14, 0);
        sb_stones(b, 2, 6, 1, 8, 6, 2, 0);
        sb_stones(b, 2, 6, 14, 8, 6, 14, 0);
        sb_stones(b, 1, 1, 1, 2, 1, 4, 0);
        sb_stones(b, 8, 1, 1, 9, 1, 4, 0);
        sb_fill(b, 1, 1, 1, 1, 1, 3, B_LAVA);
        sb_fill(b, 9, 1, 1, 9, 1, 3, B_LAVA);
        sb_stones(b, 3, 1, 8, 7, 1, 12, 0);
        sb_fill(b, 4, 1, 9, 6, 1, 11, B_LAVA);
        for (int j = 3; j < 14; j += 2) sb_fill(b, 0, 3, j, 0, 4, j, B_IRON_BARS), sb_fill(b, 10, 3, j, 10, 4, j, B_IRON_BARS);
        for (int k = 2; k < 9; k += 2) sb_fill(b, k, 3, 15, k, 4, 15, B_IRON_BARS);
        sb_stones(b, 4, 1, 5, 6, 1, 7, 0);
        sb_stones(b, 4, 2, 6, 6, 2, 7, 0);
        sb_stones(b, 4, 3, 7, 6, 3, 7, 0);
        int s = B_STONE_BRICK_STAIRS_E + sb_stairs(f, 3);
        for (int k = 4; k <= 6; k++) sb_set(b, k, 1, 4, s), sb_set(b, k, 2, 5, s), sb_set(b, k, 3, 6, s);
        for (int i = 0; i < 12; i++)
            sb_set(b, fr[i][0], 3, fr[i][1],
                   sb_rnd(b, fr[i][0], 3, fr[i][1], 4) > 0.9f ? B_END_PORTAL_FRAME_EYE : B_END_PORTAL_FRAME);
        sb_set(b, 5, 4, 6, B_MOB_SPAWNER);
        break;
    }
    case SP_CORRIDOR: {
        int n = p->face == F_N || p->face == F_S ? p->z1 - p->z0 + 1 : p->x1 - p->x0 + 1;
        for (int i = 0; i < n; i++) {
            sb_fill(b, 0, 0, i, 4, 0, i, SBK);
            sb_fill(b, 0, 1, i, 0, 3, i, SBK), sb_fill(b, 1, 1, i, 3, 3, i, B_AIR), sb_fill(b, 4, 1, i, 4, 3, i, SBK);
            sb_fill(b, 0, 4, i, 4, 4, i, SBK);
        }
        break;
    }
    }
}

/* isLiquidInStructureBoundingBox over the population's box (pb: x0, z0, x1, z1), the terrain as made: the
 * edges of the piece's box one wider */
static int sb_liquid(const FPiece *p, const int *pb) {
    int x0 = p->x0 - 1 > pb[0] ? p->x0 - 1 : pb[0], z0 = p->z0 - 1 > pb[1] ? p->z0 - 1 : pb[1];
    int x1 = p->x1 + 1 < pb[2] ? p->x1 + 1 : pb[2], z1 = p->z1 + 1 < pb[3] ? p->z1 + 1 : pb[3];
    int y0 = p->y0 - 1 > 1 ? p->y0 - 1 : 1, y1 = p->y1 + 1;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            const ColSum *s = col_sum(x, z);
            if (!s || s->y >= 62) continue;   /* (no liquid in the column) */
            int step = x == x0 || x == x1 || z == z0 || z == z1 ? 1 : (y1 > y0 ? y1 - y0 : 1);
            for (int y = y0; y <= y1; y += step) {
                int t = sum_block(s, y);
                if (t == B_WATER || t == B_LAVA) return 1;
            }
        }
    return 0;
}

/* the strongholds' pieces in chunk (cx, cz), rows [y0, y0 + h): drawn by the four populations writing into
 * C (MapGenStructure.generateStructure), each piece in the order laid out */
static void stronghold_slab(int cx, int cz, int y0, int h, uint8_t *out) {
    if (!sh_far(cx, cz)) return;
    sh_ring();
    for (int s = 0; s < 3; s++) {
        if (iabs(cx - ring.cx[s]) <= 8 && iabs(cz - ring.cz[s]) <= 8) ring.seen |= (uint8_t)(1 << s);
        const int16_t *bb = ring.bb[s];
        if (bb[3] + 1 < cx * 16 || bb[0] - 1 > cx * 16 + 15 || bb[5] + 1 < cz * 16 || bb[2] - 1 > cz * 16 + 15 ||
            bb[4] < y0 || bb[1] >= y0 + h)
            continue;
        sh_layout(s);
        const Hold *hd = SH;
        for (int q = 0; q < 4; q++) {
            int px = cx - 1 + (q & 1), pz = cz - 1 + (q >> 1);
            int pb[4] = {px * 16 + 8, pz * 16 + 8, px * 16 + 23, pz * 16 + 23};
            SB b = {0, pb[0] > cx * 16 ? pb[0] : cx * 16, pb[1] > cz * 16 ? pb[1] : cz * 16,
                    pb[2] < cx * 16 + 15 ? pb[2] : cx * 16 + 15, pb[3] < cz * 16 + 15 ? pb[3] : cz * 16 + 15, y0, h, out};
            for (int i = 0; i < hd->n; i++) {
                FPiece pc = sp_at(i);
                const FPiece *p = &pc;
                if (p->x1 < pb[0] || p->x0 > pb[2] || p->z1 < pb[1] || p->z0 > pb[3]) continue;   /* not this population's */
                if (p->x1 + 1 < b.x0 || p->x0 - 1 > b.x1 || p->z1 + 1 < b.z0 || p->z0 - 1 > b.z1 || p->y1 < y0 ||
                    p->y0 >= y0 + h)
                    continue;   /* (nothing of it here) */
                if (p->type != SP_PORTAL && sb_liquid(p, pb)) continue;
                b.p = p;
                sp_parts(&b);
            }
        }
    }
}
/* gen_prepare's part: the ring placed ahead, a stronghold a call, for chunks it may reach */
static int stronghold_prepare(int cx, int cz) { return sh_far(cx, cz) && sh_ring_step(); }

/* MapGenStructure.getClosestStrongholdPos: the nearest of the strongholds made (their first piece's middle),
 * else of all three (their start chunk's middle, at y 64) */
bool gen_stronghold_near(int x, int z, int *out) {
    if (g_flat || gd_dim) return false;
    sh_ring();
    for (int pass = 0; pass < 2; pass++) {
        int best = -1;
        i64 bd = 0;
        for (int s = 0; s < 3; s++) {
            int made = (ring.seen >> s & 1) || (iabs((x >> 4) - ring.cx[s]) <= 8 && iabs((z >> 4) - ring.cz[s]) <= 8);
            if (!pass && !made) continue;
            i64 mx = pass ? ring.cx[s] * 16 + 8 : ring.mid[s][0], mz = pass ? ring.cz[s] * 16 + 8 : ring.mid[s][2];
            i64 d = (mx - x) * (mx - x) + (mz - z) * (mz - z);
            if (best < 0 || d < bd) best = s, bd = d;
        }
        if (best < 0) continue;
        out[0] = pass ? ring.cx[best] * 16 + 8 : ring.mid[best][0];
        out[1] = pass ? 64 : ring.mid[best][1];
        out[2] = pass ? ring.cz[best] * 16 + 8 : ring.mid[best][2];
        return true;
    }
    return false;
}

/* the stronghold chest there, as made: 1 a corridor's, 2 a room's, 3 a library's (their lists); 0 none */
int gen_stronghold_chest(int x, int y, int z) {
    if (g_flat || gd_dim || !sh_far(x >> 4, z >> 4)) return 0;
    sh_ring();
    for (int s = 0; s < 3; s++) {
        const int16_t *bb = ring.bb[s];
        if (x < bb[0] || x > bb[3] || y < bb[1] || y > bb[4] || z < bb[2] || z > bb[5]) continue;
        sh_layout(s);
        for (int i = 0; i < SH->n; i++) {
            FPiece pc = sp_at(i);
            const FPiece *p = &pc;
            if (x < p->x0 || x > p->x1 || y < p->y0 || y > p->y1 || z < p->z0 || z > p->z1) continue;
            static const int8_t at[4][4] = {{SP_CHEST, 3, 2, 3}, {SP_ROOM, 3, 4, 8}, {SP_LIBRARY, 3, 3, 5}, {SP_LIBRARY, 12, 8, 1}};
            for (int k = 0; k < 4; k++)
                if (p->type == at[k][0] && fb_x(p, at[k][1], at[k][3]) == x && p->y0 + at[k][2] == y &&
                    fb_z(p, at[k][1], at[k][3]) == z)
                    return k == 0 ? 1 : k == 1 ? 2 : 3;
        }
    }
    return 0;
}

/* (tests) stronghold s: out the start chunk's middle, the portal's middle (the frame's level), the pieces */
int gen_stronghold_find(int s, int *out) {
    if (g_flat || gd_dim || s < 0 || s > 2) return 0;
    sh_ring();
    sh_layout(s);
    out[0] = ring.cx[s] * 16 + 8, out[1] = ring.cz[s] * 16 + 8, out[5] = SH->n;
    for (int i = 0; i < SH->n; i++) {
        FPiece p = sp_at(i);
        if (p.type == SP_PORTAL) out[2] = fb_x(&p, 5, 10), out[3] = p.y0 + 3, out[4] = fb_z(&p, 5, 10);
    }
    return 1;
}
