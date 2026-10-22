#include "u2.h"
#include "../gfx/vec3.h"

#include <string.h>

#define MAX_FILES 48
#define MAX_INST  64
#define U2_SCALE  8 /* object coordinates are stored divided by this */
#define NEAR_Z    512.0f
#define MAX_CLIP  24

typedef struct {
    int32_t m[9]; /* 16384 = 1.0 */
    int32_t x, y, z;
} RMat;

typedef struct {
    uint16_t nverts, centre, nnorm, npolys;
    uint8_t iw; /* vertex index width in polygons: 1 or 2 bytes */
    const uint8_t* verts; /* 8 bytes: int16 x, y, z, normal */
    const uint8_t* norms; /* 3 bytes: int8 x, y, z */
    const uint8_t* base; /* start of the object record */
    const uint8_t* poly0; /* first polygon: u8 sides, flags, colour, u16 normal, v[] */
} Obj;

struct U2 {
    uint8_t* blob;
    int nobj, ninst;
    Obj obj[MAX_FILES];
    uint8_t inst_obj[MAX_INST];
    uint8_t lum[256];

    /* animation state */
    RMat r0[MAX_INST];
    bool on[MAX_INST];
    int32_t fov;
    int32_t frames, frame;
    bool ended;

    /* streamed animation */
    PlatFile* anm;
    char name[8];
    uint32_t anm_len, anm_pos, buf_pos, buf_len;
    uint8_t buf[512];

    /* per-object scratch, sized for the biggest object (kept small: the
     * city scene only just fits in memory) */
    Vec3* cv; /* camera-space vertices */
    Vec3* cn; /* camera-space normals */
    uint16_t* offs; /* polygon record offsets from base */
    float* key; /* polygon depth */
    uint16_t* order;
};

static inline uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline int16_t rds16(const uint8_t* p) {
    return (int16_t)rd16(p);
}

/* j-th vertex index of polygon record p (after its 5-byte header) */
#define POLY_V(ob, p, j) ((ob)->iw == 2 ? rd16((p) + 5 + 2 * (j)) : (p)[5 + (j)])

/* ------------------------------------------------------------ loading */

static bool parse(U2* u, size_t size) {
    const uint8_t* d = u->blob;
    if(size < 12 || memcmp(d, "SRU2", 4)) return false;
    u->nobj = d[4];
    u->ninst = d[5];
    u->frames = rd16(d + 6);
    u->anm_len = d[8] | d[9] << 8 | (uint32_t)d[10] << 16 | (uint32_t)d[11] << 24;
    if(u->nobj > MAX_FILES || u->ninst > MAX_INST || u->ninst < 1) return false;
    const uint8_t* p = d + 12;
    for(int i = 1; i < u->ninst; i++)
        u->inst_obj[i] = *p++;
    memcpy(u->lum, p, 256);
    p += 256;

    int maxv = 1, maxn = 1, maxp = 1;
    const uint8_t* end = d + size;
    for(int o = 0; o < u->nobj; o++) {
        Obj* ob = &u->obj[o];
        if(p + 4 > end) return false;
        ob->base = p;
        ob->nverts = rd16(p);
        ob->centre = rd16(p + 2);
        ob->verts = p + 4;
        p += 4 + ob->nverts * 8;
        if(p + 4 > end) return false;
        ob->nnorm = rd16(p);
        ob->norms = p + 4;
        p += 4 + ob->nnorm * 3;
        if(p + 3 > end) return false;
        ob->npolys = rd16(p);
        ob->iw = p[2] == 2 ? 2 : 1;
        p += 3;
        ob->poly0 = p;
        for(int k = 0; k < ob->npolys; k++) {
            if(p + 5 > end) return false;
            p += 5 + ob->iw * p[0];
        }
        if(p > end || p - ob->base > 65535) return false;
        if(ob->nverts > maxv) maxv = ob->nverts;
        if(ob->nnorm > maxn) maxn = ob->nnorm;
        if(ob->npolys > maxp) maxp = ob->npolys;
    }
    for(int i = 1; i < u->ninst; i++)
        if(!u->inst_obj[i] || u->inst_obj[i] > u->nobj) return false;

    u->cv = plat_alloc(sizeof(Vec3) * maxv);
    u->cn = plat_alloc(sizeof(Vec3) * maxn);
    u->offs = plat_alloc(sizeof(uint16_t) * maxp);
    u->key = plat_alloc(sizeof(float) * maxp);
    u->order = plat_alloc(sizeof(uint16_t) * maxp);
    return u->cv && u->cn && u->offs && u->key && u->order;
}

U2* u2_load(const char* scene) {
    U2* u = plat_alloc(sizeof(U2));
    if(!u) return NULL;
    char path[16];
    size_t size = 0;
    strncpy(u->name, scene, sizeof(u->name) - 1);
    memcpy(path, scene, strlen(scene));
    strcpy(path + strlen(scene), ".bin");
    u->blob = plat_load_asset(path, &size);
    strcpy(path + strlen(scene), ".anm");
    if(!u->blob || !parse(u, size) || !(u->anm = plat_asset_open(path))) {
        u2_free(u);
        return NULL;
    }
    u->frame = -1;
    u->fov = 40 * 65536 / 360;
    return u;
}

void u2_free(U2* u) {
    if(!u) return;
    plat_asset_close(u->anm);
    plat_free(u->cv);
    plat_free(u->cn);
    plat_free(u->offs);
    plat_free(u->key);
    plat_free(u->order);
    plat_free(u->blob);
    plat_free(u);
}

int32_t u2_frames(const U2* u) {
    return u->frames;
}

/* ------------------------------------------------------------ animation */

static int rd(U2* u) {
    if(u->buf_pos >= u->buf_len) {
        if(u->anm_pos >= u->anm_len) {
            u->ended = true;
            return 0xff;
        }
        uint32_t n = u->anm_len - u->anm_pos;
        if(n > sizeof(u->buf)) n = sizeof(u->buf);
        if(!plat_asset_read(u->anm, u->anm_pos, u->buf, n)) {
            u->ended = true;
            return 0xff;
        }
        u->anm_pos += n;
        u->buf_len = n;
        u->buf_pos = 0;
    }
    return u->buf[u->buf_pos++];
}

/* signed delta of 0, 1, 2 or 4 bytes, selected by the low 2 bits of f */
static int32_t delta(U2* u, uint32_t f) {
    switch(f & 3) {
    case 1:
        return (int8_t)rd(u);
    case 2: {
        int32_t lo = rd(u);
        return lo + (int32_t)(int8_t)rd(u) * 256;
    }
    case 3: {
        uint32_t v = (uint32_t)rd(u);
        v |= (uint32_t)rd(u) << 8;
        v |= (uint32_t)rd(u) << 16;
        v |= (uint32_t)rd(u) << 24;
        return (int32_t)v;
    }
    default:
        return 0;
    }
}

static void u2_rewind(U2* u) {
    memset(u->r0, 0, sizeof(u->r0));
    memset(u->on, 0, sizeof(u->on));
    u->anm_pos = u->buf_pos = u->buf_len = 0;
    u->frame = -1;
    u->ended = false;
    u->fov = 40 * 65536 / 360;
}

/* One frame of the stream: records of (object, on/off, which position and
 * matrix entries change, deltas), until an 0xff <fov> command. */
static void decode_frame(U2* u) {
    int onum = 0;
    while(!u->ended) {
        int a = rd(u);
        if(a == 0xff) {
            a = rd(u);
            if(a <= 0x7f) {
                u->fov = a << 8;
                return;
            }
            if(a == 0xff) {
                u->ended = true;
                return;
            }
        }
        if((a & 0xc0) == 0xc0) {
            onum = (a & 0x3f) << 4;
            a = rd(u);
        }
        onum = (onum & 0xff0) | (a & 0xf);
        if(onum >= u->ninst) {
            u->ended = true; /* corrupt stream */
            return;
        }
        if((a & 0xc0) == 0x80) u->on[onum] = true;
        if((a & 0xc0) == 0x40) u->on[onum] = false;

        uint32_t pflag = 0;
        for(int i = 0; i < ((a & 0x30) >> 4); i++)
            pflag |= (uint32_t)rd(u) << (8 * i);
        RMat* r = &u->r0[onum];
        r->x += delta(u, pflag);
        r->y += delta(u, pflag >> 2);
        r->z += delta(u, pflag >> 4);
        for(int b = 0; b < 9; b++)
            if(pflag & (0x80u << b)) r->m[b] += delta(u, (pflag & 0x40) ? 2 : 1);
    }
}

void u2_seek(U2* u, int32_t frame) {
    if(frame < u->frame) u2_rewind(u);
    while(u->frame < frame && !u->ended) {
        decode_frame(u);
        u->frame++;
    }
}

/* ------------------------------------------------------------ drawing */

typedef struct {
    float m[9];
    float x, y, z;
} FMat;

/* object -> camera: camera matrix times object matrix */
__attribute__((optimize("O2"))) static void compose(const RMat* cam, const RMat* o, FMat* out) {
    const float k = 1.0f / 16384;
    for(int r = 0; r < 3; r++)
        for(int c = 0; c < 3; c++) {
            float s = 0;
            for(int i = 0; i < 3; i++)
                s += (float)cam->m[r * 3 + i] * (float)o->m[i * 3 + c];
            out->m[r * 3 + c] = s * k * k;
        }
    out->x = (cam->m[0] * (float)o->x + cam->m[1] * (float)o->y + cam->m[2] * (float)o->z) * k + cam->x;
    out->y = (cam->m[3] * (float)o->x + cam->m[4] * (float)o->y + cam->m[5] * (float)o->z) * k + cam->y;
    out->z = (cam->m[6] * (float)o->x + cam->m[7] * (float)o->y + cam->m[8] * (float)o->z) * k + cam->z;
}

static inline Vec3 xform(const FMat* m, float x, float y, float z) {
    return (Vec3){
        m->m[0] * x + m->m[1] * y + m->m[2] * z + m->x,
        m->m[3] * x + m->m[4] * y + m->m[5] * z + m->y,
        m->m[6] * x + m->m[7] * y + m->m[8] * z + m->z};
}

typedef struct {
    float pmx, pmy;
} Proj;

static inline Vec2 project(const Proj* pj, Vec3 v) {
    float z = v.z < NEAR_Z ? NEAR_Z : v.z;
    float px = v.x * pj->pmx / z + 159, py = v.y * pj->pmy / z + 99;
    return (Vec2){px * U2_SX, (py - BAND_Y0) * U2_SY};
}

/* light (12118, 10603, 3030)/16384, brightness 0..255, as the original */
static int light_level(Vec3 n) {
    float dot = n.x * 0.7396f + n.y * 0.6472f + n.z * 0.1849f;
    return clampi((int)(128 + dot * 128), 0, 255);
}

static uint8_t shade(const U2* u, uint8_t colour, uint8_t flags, Vec3 n) {
    int s = (flags >> 2) & 3; /* shade range: 8, 16 or 32 steps */
    int l = s ? clampi(light_level(n) >> (6 - s), 1, 30) : 1;
    return u->lum[(colour + l) & 255];
}

__attribute__((optimize("O2"))) static void draw_object(U2* u, Frame* f, const Obj* ob, const FMat* m, const Proj* pj) {
    for(int i = 0; i < ob->nverts; i++) {
        const uint8_t* v = ob->verts + i * 8;
        u->cv[i] = xform(m, rds16(v) * (float)U2_SCALE, rds16(v + 2) * (float)U2_SCALE, rds16(v + 4) * (float)U2_SCALE);
    }
    for(int i = 0; i < ob->nnorm; i++) {
        const int8_t* n = (const int8_t*)(ob->norms + i * 3);
        float x = n[0] / 127.0f, y = n[1] / 127.0f, z = n[2] / 127.0f;
        u->cn[i] = (Vec3){
            m->m[0] * x + m->m[1] * y + m->m[2] * z,
            m->m[3] * x + m->m[4] * y + m->m[5] * z,
            m->m[6] * x + m->m[7] * y + m->m[8] * z};
    }

    /* painter's order within the object: far polygons first */
    const uint8_t* rec = ob->poly0;
    for(int k = 0; k < ob->npolys; k++) {
        u->offs[k] = (uint16_t)(rec - ob->base);
        rec += 5 + ob->iw * rec[0];
    }
    int n = 0;
    for(int k = 0; k < ob->npolys; k++) {
        const uint8_t* p = ob->base + u->offs[k];
        float z = 0;
        for(int j = 0; j < p[0]; j++)
            z += u->cv[POLY_V(ob, p, j) % ob->nverts].z;
        u->key[k] = z / p[0];
        int at = n++;
        while(at > 0 && u->key[u->order[at - 1]] < u->key[k]) {
            u->order[at] = u->order[at - 1];
            at--;
        }
        u->order[at] = (uint16_t)k;
    }

    for(int oi = 0; oi < n; oi++) {
        const uint8_t* p = ob->base + u->offs[u->order[oi]];
        int sides = p[0];
        uint8_t flags = p[1], colour = p[2];
        uint16_t pn = rd16(p + 3);
        if(sides < 3 || sides > 16 || pn >= ob->nnorm) continue;
        Vec3 nrm = u->cn[pn];
        Vec3 v0 = u->cv[POLY_V(ob, p, 0) % ob->nverts];
        if(!(flags & 0x02) && nrm.x * v0.x + nrm.y * v0.y + nrm.z * v0.z >= 0) continue; /* back face */

        bool gouraud = flags & 0x10;
        uint8_t flat = shade(u, colour, flags, nrm);
        Vec3 cv[MAX_CLIP];
        float val[MAX_CLIP];
        bool near = false;
        for(int j = 0; j < sides; j++) {
            int vi = POLY_V(ob, p, j) % ob->nverts;
            cv[j] = u->cv[vi];
            near |= cv[j].z < NEAR_Z;
            val[j] = flat;
            if(gouraud) {
                int vn = rds16(ob->verts + vi * 8 + 6);
                val[j] = shade(u, colour, flags, (vn >= 0 && vn < ob->nnorm) ? u->cn[vn] : nrm);
            }
        }

        int count = sides;
        if(near) { /* clip against the near plane, carrying the shade */
            Vec3 ov[MAX_CLIP];
            float ovl[MAX_CLIP];
            count = 0;
            for(int j = 0; j < sides && count < MAX_CLIP - 1; j++) {
                const Vec3 *a = &cv[j], *b = &cv[(j + 1) % sides];
                float va = val[j], vb = val[(j + 1) % sides];
                bool ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
                if(ain) {
                    ov[count] = *a;
                    ovl[count++] = va;
                }
                if(ain != bin) {
                    float t = (NEAR_Z - a->z) / (b->z - a->z);
                    ov[count] = (Vec3){a->x + t * (b->x - a->x), a->y + t * (b->y - a->y), NEAR_Z};
                    ovl[count++] = va + t * (vb - va);
                }
            }
            if(count < 3) continue;
            memcpy(cv, ov, sizeof(Vec3) * count);
            memcpy(val, ovl, sizeof(float) * count);
        }
        Vec2 sp[MAX_CLIP];
        for(int j = 0; j < count; j++)
            sp[j] = project(pj, cv[j]);
        if(gouraud)
            gfx_fill_convex_shaded(f, sp, val, count);
        else
            gfx_fill_convex(f, sp, count, flat, FillSet);
    }
}

void u2_draw(U2* u, Frame* f) {
    /* field of view: projmul = half-width / tan(fov / 2), 65536 = 360 deg */
    int32_t half = clampi(u->fov / 2, 8 * 64, 16383);
    int32_t a = half >> 6; /* 1024 steps per turn */
    Proj pj;
    pj.pmx = 160.0f * fx_cos(a) / fx_sin(a);
    pj.pmy = pj.pmx * 225 / 256;

    /* compose every visible object with the camera, then sort far to near
     * by the depth of its centre vertex. (The composed matrices are made
     * again when drawing rather than kept: 3 KB of static data would stay
     * allocated for the whole app, and the big scenes need the memory.) */
    static float dist[MAX_INST];
    uint8_t order[MAX_INST];
    int n = 0;
    for(int i = 1; i < u->ninst; i++) {
        if(!u->on[i]) continue;
        const Obj* ob = &u->obj[u->inst_obj[i] - 1];
        FMat m;
        compose(&u->r0[0], &u->r0[i], &m);
        float d = 0;
        if(ob->centre < ob->nverts) {
            const uint8_t* v = ob->verts + ob->centre * 8;
            d = xform(&m, rds16(v) * (float)U2_SCALE, rds16(v + 2) * (float)U2_SCALE, rds16(v + 4) * (float)U2_SCALE).z;
        }
        dist[i] = d;
        int at = n++;
        while(at > 0 && dist[order[at - 1]] < d) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = (uint8_t)i;
    }
    for(int k = 0; k < n; k++) {
        int i = order[k];
        FMat m;
        compose(&u->r0[0], &u->r0[i], &m);
        draw_object(u, f, &u->obj[u->inst_obj[i] - 1], &m, &pj);
    }
}
