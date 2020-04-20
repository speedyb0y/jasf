#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define loop while(1)

#define elif else if

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t i64;

typedef long long           intll;
typedef unsigned            uint;
typedef unsigned long long  uintll;

typedef struct encode_s encode_s;
typedef struct decode_s decode_s;

#define CHILDS_SIZE 4
#define CHILDS_MASK 0b11U
#define CHILDS_BITS 2

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32 level;
    u32 index;
    u32 childs[CHILDS_SIZE];
    u32* ptr;
};

typedef struct encode_context_s_i {
    uint pos;
    uint size;
    uint headSize;
    uint headMask;
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32* heads;
    u32* indexes;
    encode_s cache[];
} encode_context_s_i;

typedef struct encode_context_s {
    uint pos;
    const uint size;
    const uint headSize;
    const uint headMask;
    const u64 hash;
    const u64 hash1;
    const u64 hash2;
    u32* const heads;
    u32* const indexes;
    encode_s cache[];
} encode_context_s;

struct decode_s {
    u16 type;
};

/* Means no code; it was unknown and hashed instead */
#define CODE_SAME 0xFFFFFFFEU
#define CODE_NEW  0xFFFFFFFFU

#if 1
static inline uint CACHE_ID(const encode_context_s* const restrict ctx, const uint id) {
    if (id > ctx->size) // >=
        abort();
    return id;
}

static inline encode_s* CACHE_NODE(const encode_context_s* const restrict ctx, encode_s* const restrict _node) {
    if (_node < ctx->cache || _node > (ctx->cache + ctx->size))
        abort();
    return _node;
}

// cria um array do tamanho do cache
//   seta todos como ctx->size
static inline void VERIFY_CTX(const encode_context_s* const restrict ctx, uint line) {

    uint count;

    count = ctx->size;

    while (--count) {   //  ptr tem q ue estar entre o head ou o .child[] de algum
        // All cache[indexes[x]].index -> x
        if (ctx->cache[ctx->indexes[count]].index != count)
            abort();
        // All heads are 0 or its target points to it in it s ptr
        if (ctx->cache[count].ptr != NULL && *ctx->cache[count].ptr != count) {
            printf("LINE %u ID %u?\n", line, count);
            abort();
        }
    }

    // All items in cache
        // all childs point to it
        // its *ptr points to it
        // it's ptr is not itself
        // it's childs are not itself
        // all ids are 0 <= id < size

    //count = ctx->size;
    ////while (--count)
        //printf("%u ", ctx->cache[count].level);
    //printf("\n");
}

#define VERIFY_CTX(ctx) VERIFY_CTX((ctx), __LINE__)
#define CACHE_ID(_id) CACHE_ID(ctx, (_id))
#define CACHE_NODE(_node) CACHE_NODE(ctx, (_node))
#else
#define CACHE_ID(id) (id)
#endif

#define FAZ(var, id) (CACHE_NODE((var) = ctx->cache + CACHE_ID(id)) != (encode_s*)ctx->indexes)

#define FOLLOW(_node, level, ptr) ({ \
    (ptr) = &(CACHE_NODE(_node))->childs[(hash >> (level * (level < (64 - CHILDS_BITS)))) & CHILDS_MASK]; \
    level += CHILDS_BITS; \
    })

#define SET_LEVEL_LINK(_node, _level, _ptr) ({ \
    (CACHE_NODE(_node))->level = (_level); \
    (_node)->ptr = (_ptr); \
    *(_ptr) = CACHE_ID((_node) - ctx->cache); \
    })

#define VERIFY_NODE(_node) ({ \
    if ((_node)->childs[0] == ((_node) - ctx->cache) || \
        (_node)->childs[1] == ((_node) - ctx->cache) || \
        (_node)->childs[2] == ((_node) - ctx->cache) || \
        (_node)->childs[3] == ((_node) - ctx->cache) || \
        (_node)->childs[0] > ctx->size || \
        (_node)->childs[1] > ctx->size || \
        (_node)->childs[2] > ctx->size || \
        (_node)->childs[3] > ctx->size \
        ) { \
        printf("LINE %d\n", __LINE__); \
        abort(); \
    } \
    if (ctx->indexes[(_node)->index] != ((_node) - ctx->cache)) { \
        printf("LINE %d   (ctx->indexes[(_node)->index] != ((_node) - ctx->cache)) \n", __LINE__); \
        abort(); \
    } \
    if (!((_node)->ptr == NULL || *(_node)->ptr <= ctx->size)) { \
        printf("LINE %d (!((_node)->ptr == NULL || *(_node)->ptr <= ctx->size)) \n", __LINE__); \
        abort(); \
    } \
})

// a soma d etodos os indexes
// a soma de todos os estrutura->codigo
static uint lookup(encode_context_s* const restrict ctx, const void* restrict str, uint len) {

    u64 hash  = ctx->hash  + ((u64)len);
    u64 hash1 = ctx->hash1 + ((u64)len << 24);
    u64 hash2 = ctx->hash2 + ((u64)len << 48);

    while (len >= sizeof(u64)) {
        const u64 word = *(u64*)str; str += sizeof(u64);
        hash  += word;
        hash1 += word  << (word  & 0b11111U);
        hash2 += hash1 << (word  & 0b11111U);
        hash  += word  << (hash2 & 0b11111U);
        hash  += len;
        len -= sizeof(u64);
    }

    while (len) {
        const u64 word = *(u8*)str; str += sizeof(u8);
        hash  += word;
        hash1 += word  << (word  & 0b11111U);
        hash2 += hash1 << (word  & 0b11111U);
        hash  += word  << (hash2 & 0b11111U);
        hash  += len;
        len -= sizeof(u8);
    }

    uint level = 0;
    u32* ptr = &ctx->heads[hash1 &  ctx->headMask];
    encode_s* this;

    while FAZ(this, *ptr) {
        VERIFY_NODE(this);
        if (this->hash  == hash  &&
            this->hash1 == hash1 &&
            this->hash2 == hash2) {
            int code = ctx->pos - this->index;
            if (code == 0) {
                return CODE_SAME;
            }
            if (code == 1) {
                return code;
            }
            // TODO: FIXME: mover ele para frente
            // e se não foi o último , coloca roúltimo sobre ele
            return code;
        } FOLLOW(this, level, ptr); // TODO: FIXME: vai dar certo isso?
    }

    // Sobrescreve o mais antigo
    this = ctx->cache + ctx->indexes[ctx->pos++ % ctx->size];

    if (((void*)this > (void*)ptr) || (((void*)this + sizeof(encode_s)) <= (void*)ptr)) {
        // O escolhido não foi o último do path que seguiu
        if (this->ptr) {
            // Estamos em uma hash tree, podendo ser outra
            // Para o caso deste ser o último
            *this->ptr = ctx->size;
            uint slot = CHILDS_SIZE;
            // Acha o primeiro child
            while (slot--) { encode_s* child0;
                if FAZ(child0, this->childs[slot]) {
                    // O passa para o parent
                    SET_LEVEL_LINK(child0, this->level, this->ptr);
                    // Os demais viram filhos do primeiro, a partir do mesmo slot
                    while (slot--) { encode_s* child;
                        if FAZ(child, this->childs[slot]) { encode_s* this;
                            u32* ptr = &child0->childs[slot];
                            const u64 hash = child->hash;
                            uint level = child->level;
                            // Vai até o fim
                            while FAZ(this, *ptr)
                                FOLLOW(this, level, ptr);
                            SET_LEVEL_LINK(child, level, ptr);
                        }
                    } break;
                }
            }
        }

        VERIFY_NODE(this);

        *(this->ptr = ptr) = this - ctx->cache;
    }
#if 1
    else {
        // this->ptr == ptr
        // ptr = este
        // este->index
    }
#endif

    VERIFY_NODE(this);

    this->hash  = hash;
    this->hash1 = hash1;
    this->hash2 = hash2;
    this->level = level;
    this->childs[0] = ctx->size;
    this->childs[1] = ctx->size;
    this->childs[2] = ctx->size;
    this->childs[3] = ctx->size;

    VERIFY_NODE(this);

    VERIFY_CTX(ctx);

    return CODE_NEW;
}

static inline u64 rdtscp(void) {
    uint lo;
    uint hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((u64)hi << 32) | lo;
}

static encode_context_s* encoder_new (const uint size, const uint headSize, u64 hash, u64 hash1, u64 hash2) {

    if (size <= 16)
        return NULL;

    // TODO: FIXME: ver se esse valor está correto aqui
    if (size >= 0b11111111111111111111)
        return NULL;

    const uint headMask = headSize - 1;

    switch (headMask) {
        case 0b1U:
        case 0b11U:
        case 0b111U:
        case 0b1111U:
        case 0b11111U:
        case 0b111111U:
        case 0b1111111U:
        case 0b11111111U:
        case 0b111111111U:
        case 0b1111111111U:
        case 0b11111111111U:
        case 0b111111111111U:
        case 0b1111111111111U: // 8192
        case 0b11111111111111U:
        case 0b111111111111111U: // 32767
        case 0b1111111111111111U:
            break;
        default:
            return NULL;
    }

    hash  += rdtscp();
    hash1 += hash;
    hash2 += ~hash;

    encode_context_s_i* const ctx = malloc(sizeof(encode_context_s) + size*sizeof(encode_s) + size*sizeof(u32) + headSize*sizeof(u32*));

    if (ctx == NULL)
        return NULL;

    ctx->pos = 0;
    ctx->size = size;
    ctx->indexes = (void*)(ctx->cache + size);
    ctx->heads = (void*)(ctx->indexes + size);
    ctx->headMask = headMask;
    ctx->hash = hash;
    ctx->hash1 = hash1;
    ctx->hash2 = hash2;

    uint count = 0;

    do {
        encode_s* const this = &ctx->cache[count];

        this->hash  = 0;
        this->hash1 = 0;
        this->hash2 = 0;
        this->level = 0;
        this->index = count;
        this->childs[0] = size;
        this->childs[1] = size;
        this->childs[2] = size;
        this->childs[3] = size;
        this->ptr = NULL;

        ctx->indexes[count] = count;

    } while (++count != size);

    count = 0;

    do {
        ctx->heads[count] = size;
    } while (++count != headSize);

    return (encode_context_s*)ctx;
}

static void fill(encode_context_s* const ctx, uintll count) {

    static u64 random1 = 0x77545b4578540574ULL; //rdtscp();

    static u64 random[32] = {
        0xa405f056054046e4ULL,
        0x01d345f7893BaDEFULL,
        0x480fdbff42ff0134ULL,
        0x4aa4f544f0fa6405ULL,
        0x4f0436a564015456ULL,
        0x5f42a5a45da52d78ULL,
        0x4a6faf3875405744ULL,
        0x540f845645246544ULL,
        0x14545b4578540574ULL,
        0x456074745424a407ULL,
        0x445604fa403af044ULL,
        0x456f4587a3f54060ULL,
        0xe0f40460735dedf5ULL,
        0x0847f974623a4650ULL,
        0x04f05d5ff4065f47ULL,
        0x0054074050507007ULL,
        0xa405f056054046e4ULL,
        0x01d345f7893BaDEFULL,
        0x480fdbff42ff0134ULL,
        0x4aa4f544f0fa6405ULL,
        0x4f0436a564015456ULL,
        0x5f42a5a45da52d78ULL,
        0x4a6faf3875405744ULL,
        0x540f845645246544ULL,
        0x14545b4578540574ULL,
        0x456074745424a407ULL,
        0x445604fa403af044ULL,
        0x456f4587a3f54060ULL,
        0xe0f40460735dedf5ULL,
        0x0847f974623a4650ULL,
        0x04f05d5ff4065f47ULL,
        0x0054074050507007ULL,
        };

    while (count--) {
        random[0] += random1;
        random[1] += random1;
        random[2] += random1;
        random[3] += random1;
        random[4] += random1;
        random[5] += random1;
        random[6] += random1;
        random[7] += random1;
        random[8] += random1;
        random1 += random1 << (count   & 0b11111U);
        random1 += count   << (random1 & 0b11111U);
        lookup(ctx, (void*)random, 1 + (random1 & 0b111111U));
    }
}

int main (void) {

#define CACHE_SIZE 0xFFFF

    encode_context_s* const ctx = encoder_new(CACHE_SIZE, 64, 0, 0, 0);

    VERIFY_CTX(ctx); fill(ctx, 1000*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, 1000*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, 3*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, 2*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, 2*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx);

    return 0;
}
