/* NumBlocks.nwa only (NB_DIMS), included by gen_dims.h: Nether fortresses (MapGenNetherBridge,
 * StructureNetherBridgePieces), laid out as 1.8.8 lays them, piece by piece from the same random, then
 * built chunk by chunk.
 *
 * A fortress's layout is kept in the scratch union U while the player is in the Nether (gen.c's
 * Overworld does not run there): the last one laid out, as the chunks around one are made one after
 * the other. */

enum { FP_STRAIGHT, FP_CROSSING3, FP_CROSSING, FP_STAIRS, FP_THRONE, FP_ENTRANCE, FP_CORRIDOR5, FP_CROSSING2,
       FP_CORRIDOR2, FP_CORRIDOR, FP_CORRIDOR3, FP_CORRIDOR4, FP_STALK, FP_END };
enum { F_N, F_E, F_S, F_W };   /* EnumFacing.Plane.HORIZONTAL's order */
static inline int iabs(int v) { return v < 0 ? -v : v; }

typedef struct {
    int16_t x0, y0, z0, x1, y1, z1;
    uint8_t type, face, flag, depth;   /* flag: a corridor's chest; depth: getComponentType */
    i32 seed;                          /* an End's fillSeed */
} FPiece;

#define FMAX 232
typedef struct {
    int sx, sz, ok;                     /* the start's chunk */
    int n, npend, last;                 /* pieces; waiting to build; the last weight placed (a type, or -1) */
    FPiece p[FMAX];
    uint8_t pend[FMAX];
    uint8_t wl[2][7], wn[2], cnt[FP_END];   /* the weights left (types), how many each placed */
    int16_t bb[6];                      /* all of it */
} Fort;
#define FT ((Fort *)(void *)&U)

/* PieceWeight: weight, most placed (0: any), may follow itself */
static const uint8_t fw_weight[FP_END] = {30, 10, 10, 10, 5, 5, 25, 15, 5, 5, 10, 7, 5};
static const uint8_t fw_max[FP_END] = {0, 4, 4, 3, 2, 1, 0, 5, 10, 10, 3, 2, 2};
static const uint8_t fw_row[FP_END] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
/* each piece's box: offset and size along its facing (StructureBoundingBox.getComponentToAddBoundingBox) */
static const int8_t fp_box[FP_END + 1][6] = {
    {-1, -3, 0, 5, 10, 19}, {-8, -3, 0, 19, 10, 19}, {-2, 0, 0, 7, 9, 7},  {-2, 0, 0, 7, 11, 7},
    {-2, 0, 0, 7, 8, 9},    {-5, -3, 0, 13, 14, 13}, {-1, 0, 0, 5, 7, 5},  {-1, 0, 0, 5, 7, 5},
    {-1, 0, 0, 5, 7, 5},    {-1, 0, 0, 5, 7, 5},     {-1, -7, 0, 5, 14, 10}, {-3, 0, 0, 9, 7, 9},
    {-5, -3, 0, 13, 14, 13}, {-1, -3, 0, 5, 10, 8}};

static void box_for(int x, int y, int z, const int8_t *b, int face, int16_t *o) {
    int ox = b[0], oy = b[1], oz = b[2], sx = b[3], sy = b[4], sz = b[5];
    switch (face) {
    case F_N: o[0] = x + ox, o[1] = y + oy, o[2] = z - sz + 1 + oz, o[3] = x + sx - 1 + ox, o[4] = y + sy - 1 + oy, o[5] = z + oz; break;
    case F_S: o[0] = x + ox, o[1] = y + oy, o[2] = z + oz, o[3] = x + sx - 1 + ox, o[4] = y + sy - 1 + oy, o[5] = z + sz - 1 + oz; break;
    case F_W: o[0] = x - sz + 1 + oz, o[1] = y + oy, o[2] = z + ox, o[3] = x + oz, o[4] = y + sy - 1 + oy, o[5] = z + sx - 1 + ox; break;
    default: o[0] = x + oz, o[1] = y + oy, o[2] = z + ox, o[3] = x + sz - 1 + oz, o[4] = y + sy - 1 + oy, o[5] = z + sx - 1 + ox; break;
    }
}
static int boxes_meet(const int16_t *a, const FPiece *p) {
    return a[3] >= p->x0 && a[0] <= p->x1 && a[5] >= p->z0 && a[2] <= p->z1 && a[4] >= p->y0 && a[1] <= p->y1;
}

/* the piece of a type there, if its box is above y 10 and free (each one's func_175xxx_a); -1 */
static int fp_new(JRand *r, int type, int x, int y, int z, int face, int depth) {
    Fort *f = FT;
    int16_t b[6];
    box_for(x, y, z, fp_box[type], face, b);
    if (b[1] <= 10) return -1;
    for (int i = 0; i < f->n; i++)
        if (boxes_meet(b, &f->p[i])) return -1;
    if (f->n >= FMAX) return -1;
    FPiece *p = &f->p[f->n];
    p->x0 = b[0], p->y0 = b[1], p->z0 = b[2], p->x1 = b[3], p->y1 = b[4], p->z1 = b[5];
    p->type = (uint8_t)type, p->face = (uint8_t)face, p->depth = (uint8_t)depth, p->flag = 0, p->seed = 0;
    if (type == FP_END) p->seed = jr_next(r, 32);
    if (type == FP_CORRIDOR2 || type == FP_CORRIDOR) p->flag = jr_int(r, 3) == 0;
    return f->n++;
}

/* Piece.func_175871_a: a piece of the weights left (five tries), else an end */
static int fp_pick(JRand *r, int sec, int x, int y, int z, int face, int depth) {
    Fort *f = FT;
    int total = 0, any = 0;
    for (int i = 0; i < f->wn[sec]; i++) {
        int t = f->wl[sec][i];
        if (fw_max[t] > 0 && f->cnt[t] < fw_max[t]) any = 1;
        total += fw_weight[t];
    }
    if (!any) total = -1;
    int ok = total > 0 && depth <= 30;
    for (int j = 0; j < 5 && ok; j++) {
        int k = jr_int(r, total);
        for (int i = 0; i < f->wn[sec]; i++) {
            int t = f->wl[sec][i];
            k -= fw_weight[t];
            if (k < 0) {
                if (!(fw_max[t] == 0 || f->cnt[t] < fw_max[t]) || (t == f->last && !fw_row[t])) break;
                int p = fp_new(r, t, x, y, z, face, depth);
                if (p >= 0) {
                    f->cnt[t]++;
                    f->last = t;
                    if (!(fw_max[t] == 0 || f->cnt[t] < fw_max[t])) {
                        memmove(&f->wl[sec][i], &f->wl[sec][i + 1], (size_t)(f->wn[sec] - i - 1));
                        f->wn[sec]--;
                    }
                    return p;
                }
            }
        }
    }
    return fp_new(r, FP_END, x, y, z, face, depth);
}

/* Piece.func_175870_a: the next piece there, within 112 of the start (else an end, left out of the list:
 * as 1.8.8 does), waiting to be built */
static void fp_next(JRand *r, int sec, int x, int y, int z, int face, int depth) {
    Fort *f = FT;
    if (iabs(x - f->p[0].x0) <= 112 && iabs(z - f->p[0].z0) <= 112) {
        int p = fp_pick(r, sec, x, y, z, face, depth + 1);
        if (p >= 0 && f->npend < FMAX) f->pend[f->npend++] = (uint8_t)p;
    } else {
        int16_t b[6];   /* (an end made and dropped: only its draw counts) */
        box_for(x, y, z, fp_box[FP_END], face, b);
        int free = b[1] > 10;
        for (int i = 0; i < f->n && free; i++)
            if (boxes_meet(b, &f->p[i])) free = 0;
        if (free) jr_next(r, 32);
    }
}
/* getNextComponentNormal, X (to the left) and Z (to the right) */
static void fp_normal(JRand *r, int pi, int ox, int oy, int sec) {
    FPiece p = FT->p[pi];
    switch (p.face) {
    case F_N: fp_next(r, sec, p.x0 + ox, p.y0 + oy, p.z0 - 1, F_N, p.depth); break;
    case F_S: fp_next(r, sec, p.x0 + ox, p.y0 + oy, p.z1 + 1, F_S, p.depth); break;
    case F_W: fp_next(r, sec, p.x0 - 1, p.y0 + oy, p.z0 + ox, F_W, p.depth); break;
    default: fp_next(r, sec, p.x1 + 1, p.y0 + oy, p.z0 + ox, F_E, p.depth); break;
    }
}
static void fp_left(JRand *r, int pi, int oy, int ox, int sec) {
    FPiece p = FT->p[pi];
    if (p.face == F_N || p.face == F_S) fp_next(r, sec, p.x0 - 1, p.y0 + oy, p.z0 + ox, F_W, p.depth);
    else fp_next(r, sec, p.x0 + ox, p.y0 + oy, p.z0 - 1, F_N, p.depth);
}
static void fp_right(JRand *r, int pi, int oy, int ox, int sec) {
    FPiece p = FT->p[pi];
    if (p.face == F_N || p.face == F_S) fp_next(r, sec, p.x1 + 1, p.y0 + oy, p.z0 + ox, F_E, p.depth);
    else fp_next(r, sec, p.x0 + ox, p.y0 + oy, p.z1 + 1, F_S, p.depth);
}
/* buildComponent */
static void fp_build(JRand *r, int pi) {
    switch (FT->p[pi].type) {
    case FP_CROSSING3: fp_normal(r, pi, 8, 3, 0), fp_left(r, pi, 3, 8, 0), fp_right(r, pi, 3, 8, 0); break;
    case FP_STRAIGHT: fp_normal(r, pi, 1, 3, 0); break;
    case FP_CROSSING: fp_normal(r, pi, 2, 0, 0), fp_left(r, pi, 0, 2, 0), fp_right(r, pi, 0, 2, 0); break;
    case FP_STAIRS: fp_right(r, pi, 6, 2, 0); break;
    case FP_ENTRANCE: fp_normal(r, pi, 5, 3, 1); break;
    case FP_CORRIDOR5: fp_normal(r, pi, 1, 0, 1); break;
    case FP_CROSSING2: fp_normal(r, pi, 1, 0, 1), fp_left(r, pi, 0, 1, 1), fp_right(r, pi, 0, 1, 1); break;
    case FP_CORRIDOR2: fp_right(r, pi, 0, 1, 1); break;
    case FP_CORRIDOR: fp_left(r, pi, 0, 1, 1); break;
    case FP_CORRIDOR3: fp_normal(r, pi, 1, 0, 1); break;
    case FP_CORRIDOR4: {
        int f = FT->p[pi].face, i = f == F_W || f == F_N ? 5 : 1;
        int s1 = jr_int(r, 8) > 0;
        fp_left(r, pi, 0, i, s1);
        int s2 = jr_int(r, 8) > 0;
        fp_right(r, pi, 0, i, s2);
        break;
    }
    case FP_STALK: fp_normal(r, pi, 5, 3, 1), fp_normal(r, pi, 5, 11, 1); break;
    }
}

/* MapGenNetherBridge.canSpawnStructureAtCoords: the start of the region of 16 x 16 chunks (rx, rz), if
 * it has one (one in three), and the random as it leaves it */
static int fort_start(int rx, int rz, int *sx, int *sz, JRand *r) {
    jr_seed(r, (i64)(i32)(rx ^ (i32)((u32)rz << 4)) ^ g_seed);
    jr_next(r, 32);
    if (jr_int(r, 3) != 0) return 0;
    *sx = (rx << 4) + 4 + jr_int(r, 8);
    *sz = (rz << 4) + 4 + jr_int(r, 8);
    return 1;
}

/* MapGenNetherBridge.Start: the layout of the fortress starting in chunk (sx, sz) */
static void fort_layout(int sx, int sz, JRand *r) {
    Fort *f = FT;
    if (f->ok && f->sx == sx && f->sz == sz) return;
    f->ok = 1, f->sx = sx, f->sz = sz, f->n = 0, f->npend = 0, f->last = -1;
    static const uint8_t prim[6] = {FP_STRAIGHT, FP_CROSSING3, FP_CROSSING, FP_STAIRS, FP_THRONE, FP_ENTRANCE};
    static const uint8_t sec[7] = {FP_CORRIDOR5, FP_CROSSING2, FP_CORRIDOR2, FP_CORRIDOR, FP_CORRIDOR3, FP_CORRIDOR4, FP_STALK};
    memcpy(f->wl[0], prim, 6), memcpy(f->wl[1], sec, 7);
    f->wn[0] = 6, f->wn[1] = 7;
    memset(f->cnt, 0, sizeof f->cnt);
    /* the start: a bridge crossing turned at random */
    int x = (sx << 4) + 2, z = (sz << 4) + 2;
    FPiece *s = &f->p[f->n++];
    s->type = FP_CROSSING3, s->face = (uint8_t)jr_int(r, 4), s->depth = 0, s->flag = 0, s->seed = 0;
    s->x0 = (int16_t)x, s->y0 = 64, s->z0 = (int16_t)z, s->x1 = (int16_t)(x + 18), s->y1 = 73, s->z1 = (int16_t)(z + 18);
    fp_build(r, 0);
    while (f->npend) {
        int i = jr_int(r, f->npend), p = f->pend[i];
        memmove(&f->pend[i], &f->pend[i + 1], (size_t)(f->npend - i - 1));
        f->npend--;
        fp_build(r, p);
    }
    /* updateBoundingBox, then setRandomHeight(48, 70) */
    int16_t *b = f->bb;
    b[0] = b[1] = b[2] = 32767, b[3] = b[4] = b[5] = -32768;
    for (int i = 0; i < f->n; i++) {
        const FPiece *p = &f->p[i];
        if (p->x0 < b[0]) b[0] = p->x0;
        if (p->y0 < b[1]) b[1] = p->y0;
        if (p->z0 < b[2]) b[2] = p->z0;
        if (p->x1 > b[3]) b[3] = p->x1;
        if (p->y1 > b[4]) b[4] = p->y1;
        if (p->z1 > b[5]) b[5] = p->z1;
    }
    int room = 70 - 48 + 1 - (b[4] - b[1] + 1), y = room > 1 ? 48 + jr_int(r, room) : 48, k = y - b[1];
    b[1] += k, b[4] += k;
    for (int i = 0; i < f->n; i++) f->p[i].y0 += k, f->p[i].y1 += k;
}

/* the fortress whose pieces may reach chunk (cx, cz), laid out; 0 if none */
static int fort_near(int cx, int cz) {
    for (int rz = (cz - 9) >> 4; rz <= (cz + 9) >> 4; rz++)
        for (int rx = (cx - 9) >> 4; rx <= (cx + 9) >> 4; rx++) {
            int sx, sz;
            JRand r;
            if (!fort_start(rx, rz, &sx, &sz, &r) || iabs(sx - cx) > 9 || iabs(sz - cz) > 9) continue;
            fort_layout(sx, sz, &r);
            const int16_t *b = FT->bb;
            if (b[3] >= cx * 16 && b[0] <= cx * 16 + 15 && b[5] >= cz * 16 && b[2] <= cz * 16 + 15) return 1;
        }
    return 0;
}

/* ---- building a piece into the slab of chunk (cx, cz), rows [y0, y0 + h) */
typedef struct {
    const FPiece *p;
    int cx, cz, y0, h;
    uint8_t *out;
} FBuild;
static int fb_x(const FPiece *p, int x, int z) { return p->face == F_W ? p->x1 - z : p->face == F_E ? p->x0 + z : p->x0 + x; }
static int fb_z(const FPiece *p, int x, int z) { return p->face == F_N ? p->z1 - z : p->face == F_S ? p->z0 + z : p->z0 + x; }
static uint8_t *fb_at(const FBuild *b, int wx, int wy, int wz) {
    int lx = wx - b->cx * 16, lz = wz - b->cz * 16, ly = wy - b->y0;
    if ((unsigned)lx > 15 || (unsigned)lz > 15 || ly < 0 || ly >= b->h || wy < 1) return NULL;
    return &b->out[(ly * 16 + lz) * 16 + lx];
}
static void fb_set(const FBuild *b, int x, int y, int z, uint8_t v) {
    uint8_t *c = fb_at(b, fb_x(b->p, x, z), b->p->y0 + y, fb_z(b->p, x, z));
    if (c) *c = v;
}
/* fillWithBlocks (the same block on the edges and inside) */
static void fb_fill(const FBuild *b, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t v) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int z = z0; z <= z1; z++) fb_set(b, x, y, z, v);
}
#define NB GEN_CODE(GC_NETHER_BRICK)
#define FENCE GEN_CODE(GC_FENCE)
static int nq(int x, int y, int z);   /* (below: the terrain, without caves) */
/* replaceAirAndLiquidDownwards: a pillar down to the ground (here, the terrain without its caves, or another
 * piece) */
static void fb_down(const FBuild *b, int x, int y, int z) {
    const FPiece *p = b->p;
    int wx = fb_x(p, x, z), wy = p->y0 + y, wz = fb_z(p, x, z);
    if (wx < b->cx * 16 || wx > b->cx * 16 + 15 || wz < b->cz * 16 || wz > b->cz * 16 + 15) return;
    const Fort *f = FT;
    int16_t under[8][2];   /* the other pieces over this column: their heights */
    int nu = 0;
    for (int i = 0; i < f->n && nu < 8; i++) {
        const FPiece *o = &f->p[i];
        if (o != p && wx >= o->x0 && wx <= o->x1 && wz >= o->z0 && wz <= o->z1 && o->y0 < wy)
            under[nu][0] = o->y0, under[nu++][1] = o->y1;
    }
    for (; wy > 1; wy--) {
        int t = nq(wx, wy, wz);
        if (t != B_AIR && t != B_LAVA) return;
        for (int i = 0; i < nu; i++)
            if (wy >= under[i][0] && wy <= under[i][1]) return;
        uint8_t *c = fb_at(b, wx, wy, wz);
        if (c) *c = NB;
    }
}
/* getMetadataWithOffset for stairs (0 east, 1 west, 2 south, 3 north), as the code */
static uint8_t fb_stairs(const FPiece *p, int m) {
    static const uint8_t rot[4][4] = {{0, 1, 2, 3}, {0, 1, 2, 3}, {0, 1, 3, 2}, {2, 3, 0, 1}};   /* N, -, S, W */
    static const uint8_t east[4] = {2, 3, 1, 0};
    int k = p->face == F_E ? east[m] : rot[p->face][m];
    return GEN_CODE(GC_STAIRS_E + k);
}
/* the castle's rooms with the walls of fences on top (the entrance, the wart room) */
static void fb_castle(const FBuild *b) {
    fb_fill(b, 0, 3, 0, 12, 4, 12, NB);
    fb_fill(b, 0, 5, 0, 12, 13, 12, B_AIR);
    fb_fill(b, 0, 5, 0, 1, 12, 12, NB);
    fb_fill(b, 11, 5, 0, 12, 12, 12, NB);
    fb_fill(b, 2, 5, 11, 4, 12, 12, NB);
    fb_fill(b, 8, 5, 11, 10, 12, 12, NB);
    fb_fill(b, 5, 9, 11, 7, 12, 12, NB);
    fb_fill(b, 2, 5, 0, 4, 12, 1, NB);
    fb_fill(b, 8, 5, 0, 10, 12, 1, NB);
    fb_fill(b, 5, 9, 0, 7, 12, 1, NB);
    fb_fill(b, 2, 11, 2, 10, 12, 10, NB);
}
static void fb_castle_top(const FBuild *b) {
    for (int i = 1; i <= 11; i += 2) {
        fb_fill(b, i, 10, 0, i, 11, 0, FENCE);
        fb_fill(b, i, 10, 12, i, 11, 12, FENCE);
        fb_fill(b, 0, 10, i, 0, 11, i, FENCE);
        fb_fill(b, 12, 10, i, 12, 11, i, FENCE);
        fb_set(b, i, 13, 0, NB);
        fb_set(b, i, 13, 12, NB);
        fb_set(b, 0, 13, i, NB);
        fb_set(b, 12, 13, i, NB);
        fb_set(b, i + 1, 13, 0, FENCE);
        fb_set(b, i + 1, 13, 12, FENCE);
        fb_set(b, 0, 13, i + 1, FENCE);
        fb_set(b, 12, 13, i + 1, FENCE);
    }
    fb_set(b, 0, 13, 0, FENCE);
    fb_set(b, 0, 13, 12, FENCE);
    fb_set(b, 12, 13, 0, FENCE);
    for (int j = 3; j <= 9; j += 2) {
        fb_fill(b, 1, 7, j, 1, 8, j, FENCE);
        fb_fill(b, 11, 7, j, 11, 8, j, FENCE);
    }
}
static void fb_castle_base(const FBuild *b) {
    fb_fill(b, 4, 2, 0, 8, 2, 12, NB);
    fb_fill(b, 0, 2, 4, 12, 2, 8, NB);
    fb_fill(b, 4, 0, 0, 8, 1, 3, NB);
    fb_fill(b, 4, 0, 9, 8, 1, 12, NB);
    fb_fill(b, 0, 0, 4, 3, 1, 8, NB);
    fb_fill(b, 9, 0, 4, 12, 1, 8, NB);
    for (int k = 4; k <= 8; k++)
        for (int l = 0; l <= 2; l++) fb_down(b, k, -1, l), fb_down(b, k, -1, 12 - l);
    for (int i = 0; i <= 2; i++)
        for (int j = 4; j <= 8; j++) fb_down(b, i, -1, j), fb_down(b, 12 - i, -1, j);
}
/* a small corridor's floor, room and roof (5 x 7 x 5) */
static void fb_small(const FBuild *b) {
    fb_fill(b, 0, 0, 0, 4, 1, 4, NB);
    fb_fill(b, 0, 2, 0, 4, 5, 4, B_AIR);
}
static void fb_small_end(const FBuild *b) {
    fb_fill(b, 0, 6, 0, 4, 6, 4, NB);
    for (int i = 0; i <= 4; i++)
        for (int j = 0; j <= 4; j++) fb_down(b, i, -1, j);
}

/* addComponentParts */
static void fp_parts(const FBuild *b) {
    const FPiece *p = b->p;
    switch (p->type) {
    case FP_CROSSING3:
        fb_fill(b, 7, 3, 0, 11, 4, 18, NB);
        fb_fill(b, 0, 3, 7, 18, 4, 11, NB);
        fb_fill(b, 8, 5, 0, 10, 7, 18, B_AIR);
        fb_fill(b, 0, 5, 8, 18, 7, 10, B_AIR);
        fb_fill(b, 7, 5, 0, 7, 5, 7, NB);
        fb_fill(b, 7, 5, 11, 7, 5, 18, NB);
        fb_fill(b, 11, 5, 0, 11, 5, 7, NB);
        fb_fill(b, 11, 5, 11, 11, 5, 18, NB);
        fb_fill(b, 0, 5, 7, 7, 5, 7, NB);
        fb_fill(b, 11, 5, 7, 18, 5, 7, NB);
        fb_fill(b, 0, 5, 11, 7, 5, 11, NB);
        fb_fill(b, 11, 5, 11, 18, 5, 11, NB);
        fb_fill(b, 7, 2, 0, 11, 2, 5, NB);
        fb_fill(b, 7, 2, 13, 11, 2, 18, NB);
        fb_fill(b, 7, 0, 0, 11, 1, 3, NB);
        fb_fill(b, 7, 0, 15, 11, 1, 18, NB);
        for (int i = 7; i <= 11; i++)
            for (int j = 0; j <= 2; j++) fb_down(b, i, -1, j), fb_down(b, i, -1, 18 - j);
        fb_fill(b, 0, 2, 7, 5, 2, 11, NB);
        fb_fill(b, 13, 2, 7, 18, 2, 11, NB);
        fb_fill(b, 0, 0, 7, 3, 1, 11, NB);
        fb_fill(b, 15, 0, 7, 18, 1, 11, NB);
        for (int k = 0; k <= 2; k++)
            for (int l = 7; l <= 11; l++) fb_down(b, k, -1, l), fb_down(b, 18 - k, -1, l);
        break;
    case FP_STRAIGHT:
        fb_fill(b, 0, 3, 0, 4, 4, 18, NB);
        fb_fill(b, 1, 5, 0, 3, 7, 18, B_AIR);
        fb_fill(b, 0, 5, 0, 0, 5, 18, NB);
        fb_fill(b, 4, 5, 0, 4, 5, 18, NB);
        fb_fill(b, 0, 2, 0, 4, 2, 5, NB);
        fb_fill(b, 0, 2, 13, 4, 2, 18, NB);
        fb_fill(b, 0, 0, 0, 4, 1, 3, NB);
        fb_fill(b, 0, 0, 15, 4, 1, 18, NB);
        for (int i = 0; i <= 4; i++)
            for (int j = 0; j <= 2; j++) fb_down(b, i, -1, j), fb_down(b, i, -1, 18 - j);
        fb_fill(b, 0, 1, 1, 0, 4, 1, FENCE);
        fb_fill(b, 0, 3, 4, 0, 4, 4, FENCE);
        fb_fill(b, 0, 3, 14, 0, 4, 14, FENCE);
        fb_fill(b, 0, 1, 17, 0, 4, 17, FENCE);
        fb_fill(b, 4, 1, 1, 4, 4, 1, FENCE);
        fb_fill(b, 4, 3, 4, 4, 4, 4, FENCE);
        fb_fill(b, 4, 3, 14, 4, 4, 14, FENCE);
        fb_fill(b, 4, 1, 17, 4, 4, 17, FENCE);
        break;
    case FP_CROSSING:
        fb_fill(b, 0, 0, 0, 6, 1, 6, NB);
        fb_fill(b, 0, 2, 0, 6, 7, 6, B_AIR);
        fb_fill(b, 0, 2, 0, 1, 6, 0, NB);
        fb_fill(b, 0, 2, 6, 1, 6, 6, NB);
        fb_fill(b, 5, 2, 0, 6, 6, 0, NB);
        fb_fill(b, 5, 2, 6, 6, 6, 6, NB);
        fb_fill(b, 0, 2, 0, 0, 6, 1, NB);
        fb_fill(b, 0, 2, 5, 0, 6, 6, NB);
        fb_fill(b, 6, 2, 0, 6, 6, 1, NB);
        fb_fill(b, 6, 2, 5, 6, 6, 6, NB);
        fb_fill(b, 2, 6, 0, 4, 6, 0, NB);
        fb_fill(b, 2, 5, 0, 4, 5, 0, FENCE);
        fb_fill(b, 2, 6, 6, 4, 6, 6, NB);
        fb_fill(b, 2, 5, 6, 4, 5, 6, FENCE);
        fb_fill(b, 0, 6, 2, 0, 6, 4, NB);
        fb_fill(b, 0, 5, 2, 0, 5, 4, FENCE);
        fb_fill(b, 6, 6, 2, 6, 6, 4, NB);
        fb_fill(b, 6, 5, 2, 6, 5, 4, FENCE);
        for (int i = 0; i <= 6; i++)
            for (int j = 0; j <= 6; j++) fb_down(b, i, -1, j);
        break;
    case FP_STAIRS:
        fb_fill(b, 0, 0, 0, 6, 1, 6, NB);
        fb_fill(b, 0, 2, 0, 6, 10, 6, B_AIR);
        fb_fill(b, 0, 2, 0, 1, 8, 0, NB);
        fb_fill(b, 5, 2, 0, 6, 8, 0, NB);
        fb_fill(b, 0, 2, 1, 0, 8, 6, NB);
        fb_fill(b, 6, 2, 1, 6, 8, 6, NB);
        fb_fill(b, 1, 2, 6, 5, 8, 6, NB);
        fb_fill(b, 0, 3, 2, 0, 5, 4, FENCE);
        fb_fill(b, 6, 3, 2, 6, 5, 2, FENCE);
        fb_fill(b, 6, 3, 4, 6, 5, 4, FENCE);
        fb_set(b, 5, 2, 5, NB);
        fb_fill(b, 4, 2, 5, 4, 3, 5, NB);
        fb_fill(b, 3, 2, 5, 3, 4, 5, NB);
        fb_fill(b, 2, 2, 5, 2, 5, 5, NB);
        fb_fill(b, 1, 2, 5, 1, 6, 5, NB);
        fb_fill(b, 1, 7, 1, 5, 7, 4, NB);
        fb_fill(b, 6, 8, 2, 6, 8, 4, B_AIR);
        fb_fill(b, 2, 6, 0, 4, 8, 0, NB);
        fb_fill(b, 2, 5, 0, 4, 5, 0, FENCE);
        for (int i = 0; i <= 6; i++)
            for (int j = 0; j <= 6; j++) fb_down(b, i, -1, j);
        break;
    case FP_THRONE:
        fb_fill(b, 0, 2, 0, 6, 7, 7, B_AIR);
        fb_fill(b, 1, 0, 0, 5, 1, 7, NB);
        fb_fill(b, 1, 2, 1, 5, 2, 7, NB);
        fb_fill(b, 1, 3, 2, 5, 3, 7, NB);
        fb_fill(b, 1, 4, 3, 5, 4, 7, NB);
        fb_fill(b, 1, 2, 0, 1, 4, 2, NB);
        fb_fill(b, 5, 2, 0, 5, 4, 2, NB);
        fb_fill(b, 1, 5, 2, 1, 5, 3, NB);
        fb_fill(b, 5, 5, 2, 5, 5, 3, NB);
        fb_fill(b, 0, 5, 3, 0, 5, 8, NB);
        fb_fill(b, 6, 5, 3, 6, 5, 8, NB);
        fb_fill(b, 1, 5, 8, 5, 5, 8, NB);
        fb_set(b, 1, 6, 3, FENCE);
        fb_set(b, 5, 6, 3, FENCE);
        fb_fill(b, 0, 6, 3, 0, 6, 8, FENCE);
        fb_fill(b, 6, 6, 3, 6, 6, 8, FENCE);
        fb_fill(b, 1, 6, 8, 5, 7, 8, FENCE);
        fb_fill(b, 2, 8, 8, 4, 8, 8, FENCE);
        fb_set(b, 3, 5, 5, B_MOB_SPAWNER);   /* (blazes) */
        for (int i = 0; i <= 6; i++)
            for (int j = 0; j <= 6; j++) fb_down(b, i, -1, j);
        break;
    case FP_ENTRANCE:
        fb_castle(b);
        fb_fill(b, 5, 8, 0, 7, 8, 0, FENCE);
        fb_castle_top(b);
        fb_castle_base(b);
        fb_fill(b, 5, 5, 5, 7, 5, 7, NB);
        fb_fill(b, 6, 1, 6, 6, 4, 6, B_AIR);
        fb_set(b, 6, 0, 6, NB);
        fb_set(b, 6, 5, 6, B_LAVA);
        break;
    case FP_CORRIDOR5:
        fb_small(b);
        fb_fill(b, 0, 2, 0, 0, 5, 4, NB);
        fb_fill(b, 4, 2, 0, 4, 5, 4, NB);
        fb_fill(b, 0, 3, 1, 0, 4, 1, FENCE);
        fb_fill(b, 0, 3, 3, 0, 4, 3, FENCE);
        fb_fill(b, 4, 3, 1, 4, 4, 1, FENCE);
        fb_fill(b, 4, 3, 3, 4, 4, 3, FENCE);
        fb_small_end(b);
        break;
    case FP_CROSSING2:
        fb_small(b);
        fb_fill(b, 0, 2, 0, 0, 5, 0, NB);
        fb_fill(b, 4, 2, 0, 4, 5, 0, NB);
        fb_fill(b, 0, 2, 4, 0, 5, 4, NB);
        fb_fill(b, 4, 2, 4, 4, 5, 4, NB);
        fb_small_end(b);
        break;
    case FP_CORRIDOR2:
        fb_small(b);
        fb_fill(b, 0, 2, 0, 0, 5, 4, NB);
        fb_fill(b, 0, 3, 1, 0, 4, 1, FENCE);
        fb_fill(b, 0, 3, 3, 0, 4, 3, FENCE);
        fb_fill(b, 4, 2, 0, 4, 5, 0, NB);
        fb_fill(b, 1, 2, 4, 4, 5, 4, NB);
        fb_fill(b, 1, 3, 4, 1, 4, 4, FENCE);
        fb_fill(b, 3, 3, 4, 3, 4, 4, FENCE);
        if (p->flag) fb_set(b, 1, 2, 3, B_CHEST);
        fb_small_end(b);
        break;
    case FP_CORRIDOR:
        fb_small(b);
        fb_fill(b, 4, 2, 0, 4, 5, 4, NB);
        fb_fill(b, 4, 3, 1, 4, 4, 1, FENCE);
        fb_fill(b, 4, 3, 3, 4, 4, 3, FENCE);
        fb_fill(b, 0, 2, 0, 0, 5, 0, NB);
        fb_fill(b, 0, 2, 4, 3, 5, 4, NB);
        fb_fill(b, 1, 3, 4, 1, 4, 4, FENCE);
        fb_fill(b, 3, 3, 4, 3, 4, 4, FENCE);
        if (p->flag) fb_set(b, 3, 2, 3, B_CHEST);
        fb_small_end(b);
        break;
    case FP_CORRIDOR3: {
        uint8_t st = fb_stairs(p, 2);
        for (int j = 0; j <= 9; j++) {
            int k = 7 - j > 1 ? 7 - j : 1, l = k + 5 > 14 - j ? k + 5 : 14 - j;
            if (l > 13) l = 13;
            fb_fill(b, 0, 0, j, 4, k, j, NB);
            fb_fill(b, 1, k + 1, j, 3, l - 1, j, B_AIR);
            if (j <= 6) fb_set(b, 1, k + 1, j, st), fb_set(b, 2, k + 1, j, st), fb_set(b, 3, k + 1, j, st);
            fb_fill(b, 0, l, j, 4, l, j, NB);
            fb_fill(b, 0, k + 1, j, 0, l - 1, j, NB);
            fb_fill(b, 4, k + 1, j, 4, l - 1, j, NB);
            if (!(j & 1)) fb_fill(b, 0, k + 2, j, 0, k + 3, j, FENCE), fb_fill(b, 4, k + 2, j, 4, k + 3, j, FENCE);
            for (int i = 0; i <= 4; i++) fb_down(b, i, -1, j);
        }
        break;
    }
    case FP_CORRIDOR4:
        fb_fill(b, 0, 0, 0, 8, 1, 8, NB);
        fb_fill(b, 0, 2, 0, 8, 5, 8, B_AIR);
        fb_fill(b, 0, 6, 0, 8, 6, 5, NB);
        fb_fill(b, 0, 2, 0, 2, 5, 0, NB);
        fb_fill(b, 6, 2, 0, 8, 5, 0, NB);
        fb_fill(b, 1, 3, 0, 1, 4, 0, FENCE);
        fb_fill(b, 7, 3, 0, 7, 4, 0, FENCE);
        fb_fill(b, 0, 2, 4, 8, 2, 8, NB);
        fb_fill(b, 1, 1, 4, 2, 2, 4, B_AIR);
        fb_fill(b, 6, 1, 4, 7, 2, 4, B_AIR);
        fb_fill(b, 0, 3, 8, 8, 3, 8, FENCE);
        fb_fill(b, 0, 3, 6, 0, 3, 7, FENCE);
        fb_fill(b, 8, 3, 6, 8, 3, 7, FENCE);
        fb_fill(b, 0, 3, 4, 0, 5, 5, NB);
        fb_fill(b, 8, 3, 4, 8, 5, 5, NB);
        fb_fill(b, 1, 3, 5, 2, 5, 5, NB);
        fb_fill(b, 6, 3, 5, 7, 5, 5, NB);
        fb_fill(b, 1, 4, 5, 1, 5, 5, FENCE);
        fb_fill(b, 7, 4, 5, 7, 5, 5, FENCE);
        for (int i = 0; i <= 5; i++)
            for (int j = 0; j <= 8; j++) fb_down(b, j, -1, i);
        break;
    case FP_STALK: {
        fb_castle(b);
        fb_castle_top(b);
        uint8_t k1 = fb_stairs(p, 3);
        for (int j = 0; j <= 6; j++) {
            int k = j + 4;
            for (int l = 5; l <= 7; l++) fb_set(b, l, 5 + j, k, k1);
            if (k >= 5 && k <= 8) fb_fill(b, 5, 5, k, 7, j + 4, k, NB);
            else if (k >= 9 && k <= 10) fb_fill(b, 5, 8, k, 7, j + 4, k, NB);
            if (j >= 1) fb_fill(b, 5, 6 + j, k, 7, 9 + j, k, B_AIR);
        }
        for (int l = 5; l <= 7; l++) fb_set(b, l, 12, 11, k1);
        fb_fill(b, 5, 6, 7, 5, 7, 7, FENCE);
        fb_fill(b, 7, 6, 7, 7, 7, 7, FENCE);
        fb_fill(b, 5, 13, 12, 7, 13, 12, B_AIR);
        fb_fill(b, 2, 5, 2, 3, 5, 3, NB);
        fb_fill(b, 2, 5, 9, 3, 5, 10, NB);
        fb_fill(b, 9, 5, 2, 10, 5, 3, NB);
        fb_fill(b, 9, 5, 9, 10, 5, 10, NB);
        fb_fill(b, 10, 5, 4, 10, 5, 8, NB);
        uint8_t i2 = fb_stairs(p, 0), j2 = fb_stairs(p, 1);
        fb_set(b, 4, 5, 2, j2), fb_set(b, 4, 5, 3, j2), fb_set(b, 4, 5, 9, j2), fb_set(b, 4, 5, 10, j2);
        fb_set(b, 8, 5, 2, i2), fb_set(b, 8, 5, 3, i2), fb_set(b, 8, 5, 9, i2), fb_set(b, 8, 5, 10, i2);
        fb_fill(b, 3, 4, 4, 4, 4, 8, GEN_CODE(GC_SOUL_SAND));
        fb_fill(b, 8, 4, 4, 9, 4, 8, GEN_CODE(GC_SOUL_SAND));
        fb_fill(b, 3, 5, 4, 4, 5, 8, GEN_CODE(GC_WART));
        fb_fill(b, 8, 5, 4, 9, 5, 8, GEN_CODE(GC_WART));
        fb_castle_base(b);
        break;
    }
    case FP_END: {
        JRand r;
        jr_seed(&r, p->seed);
        for (int i = 0; i <= 4; i++)
            for (int j = 3; j <= 4; j++) {
                int k = jr_int(&r, 8);
                fb_fill(b, i, j, 0, i, j, k, NB);
            }
        int l = jr_int(&r, 8);
        fb_fill(b, 0, 5, 0, 0, 5, l, NB);
        l = jr_int(&r, 8);
        fb_fill(b, 4, 5, 0, 4, 5, l, NB);
        for (l = 0; l <= 4; l++) {
            int i1 = jr_int(&r, 5);
            fb_fill(b, l, 2, 0, l, 2, i1, NB);
        }
        for (l = 0; l <= 4; l++)
            for (int j1 = 0; j1 <= 1; j1++) {
                int k1 = jr_int(&r, 3);
                fb_fill(b, l, j1, 0, l, j1, k1, NB);
            }
        break;
    }
    }
}

/* the fortress's pieces in chunk (cx, cz), rows [y0, y0 + h) */
static void fort_build(int cx, int cz, int y0, int h, uint8_t *out) {
    if (!fort_near(cx, cz)) return;
    const Fort *f = FT;
    for (int i = 0; i < f->n; i++) {
        const FPiece *p = &f->p[i];
        if (p->x1 < cx * 16 || p->x0 > cx * 16 + 15 || p->z1 < cz * 16 || p->z0 > cz * 16 + 15 || p->y1 + 1 < y0)
            continue;
        FBuild b = {p, cx, cz, y0, h, out};
        fp_parts(&b);
    }
}

/* MapGenNetherBridge.func_175795_b, func_175796_a: in one of a fortress's pieces (2), or in its box (1),
 * of the fortress near (the one last laid out); 0 */
int gen_fortress_at(int x, int y, int z) {
    if (gd_dim != DIM_NETHER || !fort_near(x >> 4, z >> 4)) return 0;
    const Fort *f = FT;
    const int16_t *b = f->bb;
    if (x < b[0] || x > b[3] || y < b[1] || y > b[4] || z < b[2] || z > b[5]) return 0;
    for (int i = 0; i < f->n; i++) {
        const FPiece *p = &f->p[i];
        if (x >= p->x0 && x <= p->x1 && y >= p->y0 && y <= p->y1 && z >= p->z0 && z <= p->z1) return 2;
    }
    return 1;
}
/* (tests) the nearest fortress's start to (x, z), within 4 regions: its middle at the bridges' level */
int gen_fortress_find(int x, int z, int *out) {
    int best = -1;
    for (int rz = (z >> 8) - 2; rz <= (z >> 8) + 2; rz++)
        for (int rx = (x >> 8) - 2; rx <= (x >> 8) + 2; rx++) {
            int sx, sz;
            JRand r;
            if (!fort_start(rx, rz, &sx, &sz, &r)) continue;
            fort_layout(sx, sz, &r);
            const FPiece *p = &FT->p[0];
            int cx = (p->x0 + p->x1) / 2, cz = (p->z0 + p->z1) / 2, d = iabs(cx - x) + iabs(cz - z);
            if (best < 0 || d < best) best = d, out[0] = cx, out[1] = p->y0 + 5, out[2] = cz, out[3] = FT->n;
        }
    return best >= 0;
}
#undef NB
#undef FENCE

/* the blaze spawners of the fortress whose pieces reach (x, z): in its thrones, up to n of them (x, y, z) */
int gen_fortress_spawners(int x, int z, int *out, int n) {
    if (gd_dim != DIM_NETHER || !fort_near(x >> 4, z >> 4)) return 0;
    int k = 0;
    for (int i = 0; i < FT->n && k < n; i++) {
        const FPiece *p = &FT->p[i];
        if (p->type != FP_THRONE) continue;
        out[k * 3] = fb_x(p, 3, 5), out[k * 3 + 1] = p->y0 + 5, out[k * 3 + 2] = fb_z(p, 3, 5);
        k++;
    }
    return k;
}
