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

#define CACHE_INVALID 0xFFFFFU

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32 index;
    u32 childs[CHILDS_SIZE];
    u32* ptr; // TODO: FIXME: transformar em u32
};

typedef struct encode_context_s_i {
    uint pos;
    uint size;
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
// cria um array do tamanho do cache
//   seta todos como ctx->size
static inline void VERIFY_CTX(const encode_context_s* const restrict ctx, uint line) {
    printf("OK?\n");
    uint count;

    { u8 array[ctx->size];

        count = ctx->size;
        while (count--)
            array[count] = 1;

        count = ctx->size;
        while (count--)
            if(ctx->cache[count].ptr)
                if(*ctx->cache[count].ptr != CACHE_INVALID)
                    array[*ctx->cache[count].ptr] = 0;

        count = ctx->size;
        while (count--)
            if (array[count])
                printf("AFF %u\n", count);
    }

    count = ctx->size;

    while (count--) {   //  ptr tem q ue estar entre o head ou o .child[] de algum

        // All cache[indexes[x]].index -> x
        if (ctx->cache[ctx->indexes[count]].index != count)
            abort();

        // All heads are 0 or its target points to it in it s ptr
        if (ctx->cache[count].ptr != NULL && *ctx->cache[count].ptr != count) {
            printf("LINE %u ID %u?\n", line, count);
            abort();
        }

        const encode_s* const this = &ctx->cache[count];

        // Seu ptr não pode apontar para um próprio slot de child
        if (this->ptr == &this->childs[0] ||
            this->ptr == &this->childs[1] ||
            this->ptr == &this->childs[2] ||
            this->ptr == &this->childs[3]
            ) abort();

        // O valor no slot de seu child não pode ser si mesmo
        if (this == (ctx->cache + this->childs[0]) ||
            this == (ctx->cache + this->childs[1]) ||
            this == (ctx->cache + this->childs[2]) ||
            this == (ctx->cache + this->childs[3])
            ) abort();

        //
        if ((this->childs[0] > ctx->size && this->childs[0] != CACHE_INVALID) ||
            (this->childs[1] > ctx->size && this->childs[1] != CACHE_INVALID) ||
            (this->childs[2] > ctx->size && this->childs[2] != CACHE_INVALID) ||
            (this->childs[3] > ctx->size && this->childs[3] != CACHE_INVALID)
            ) abort();
    }

    // All items in cache
        // all childs point to it
        // its *ptr points to it
        // it's ptr is not itself
        // it's childs are not itself
        // all ids are 0 <= id < size
    printf("OK.\n");
}

#define VERIFY_CTX(ctx) VERIFY_CTX((ctx), __LINE__)
#endif

static inline encode_s* leaf_node(encode_context_s* const restrict ctx, encode_s* const restrict node) {

    //printf("LEAF %p\n", node);

    if (!((ctx->cache + node->childs[0]) != node &&
          (ctx->cache + node->childs[1]) != node &&
          (ctx->cache + node->childs[2]) != node &&
          (ctx->cache + node->childs[3]) != node
        )) abort();

    if (node->childs[0] != CACHE_INVALID) return leaf_node(ctx, ctx->cache + node->childs[0]);
    if (node->childs[1] != CACHE_INVALID) return leaf_node(ctx, ctx->cache + node->childs[1]);
    if (node->childs[2] != CACHE_INVALID) return leaf_node(ctx, ctx->cache + node->childs[2]);
    if (node->childs[3] != CACHE_INVALID) return leaf_node(ctx, ctx->cache + node->childs[3]);

    //uint slot = CHILDS_SIZE;

    //do { const uint id = node->childs[slot];
        //if (id != CACHE_INVALID)
            //return leaf_node(ctx, ctx->cache + id);
    //} while (slot--);

    return node;
}

static inline void _ASSERT(int condition) {
    if (!condition)
        abort();
}

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

    encode_s* const cache = ctx->cache;

    uint level = 0;

    u32* ptr = ctx->heads + (hash1 & ctx->headMask);

    loop {
        const uint id = *ptr;

        if (id == CACHE_INVALID) {
            /* Não encontrou */

            encode_s* const this = cache + ctx->indexes[ctx->pos++ % ctx->size]; // Sobrescreve o mais antigo
            encode_s* const leaf = leaf_node(ctx, this); // Encontra um leaf dele para substituir ele

            _ASSERT (leaf->childs[0] == CACHE_INVALID);
            _ASSERT (leaf->childs[1] == CACHE_INVALID);
            _ASSERT (leaf->childs[2] == CACHE_INVALID);
            _ASSERT (leaf->childs[3] == CACHE_INVALID);

            if (this->ptr == NULL) { // Está usando um que ainda não foi usado

               _ASSERT (this->childs[0] == CACHE_INVALID);
               _ASSERT (this->childs[1] == CACHE_INVALID);
               _ASSERT (this->childs[2] == CACHE_INVALID);
               _ASSERT (this->childs[3] == CACHE_INVALID);

                this->ptr = ptr;
               *this->ptr = this - cache;
            } elif ((void*)this <= (void*)ptr && (void*)ptr < ((void*)this + sizeof(encode_s))) {
                // Está usando o que seria o parent
                // this->ptr mantém
                // ptr aponta para o slot vazio que seguiu
                // Os demais slots podem conter outras coisas
                _ASSERT (
                   &this->childs[0] == ptr ||
                   &this->childs[1] == ptr ||
                   &this->childs[2] == ptr ||
                   &this->childs[3] == ptr
                   );

                *ptr = CACHE_INVALID;
            } elif (this == leaf) { // Escolheu um que não tem childs; não precisa mover nada

               _ASSERT (this->childs[0] == CACHE_INVALID);
               _ASSERT (this->childs[1] == CACHE_INVALID);
               _ASSERT (this->childs[2] == CACHE_INVALID);
               _ASSERT (this->childs[3] == CACHE_INVALID);

               *this->ptr = CACHE_INVALID;
                this->ptr = ptr;
               *this->ptr = this - cache;
            } else {
                // Escolheu um que tem childs, e selecionou um leaf abaixo dele
                // Este leaf pode ser um child direto do this, desmarca essa relação
               *leaf->ptr = CACHE_INVALID;
                leaf->ptr = this->ptr;
               *leaf->ptr = leaf - cache; // this->ptr sendo sobrescrito aqui
                // Passa todos os demais para o leaf
                // Os que realmente existirem, vai neles e muda o ptr deles
                if ((leaf->childs[0] = this->childs[0]) != CACHE_INVALID) (cache + leaf->childs[0])->ptr = &leaf->childs[0];
                if ((leaf->childs[1] = this->childs[1]) != CACHE_INVALID) (cache + leaf->childs[1])->ptr = &leaf->childs[1];
                if ((leaf->childs[2] = this->childs[2]) != CACHE_INVALID) (cache + leaf->childs[2])->ptr = &leaf->childs[2];
                if ((leaf->childs[3] = this->childs[3]) != CACHE_INVALID) (cache + leaf->childs[3])->ptr = &leaf->childs[3];
                // Agora sim usa o this
                this->ptr = ptr;
               *this->ptr = this - cache;
                this->childs[0] = CACHE_INVALID;
                this->childs[1] = CACHE_INVALID;
                this->childs[2] = CACHE_INVALID;
                this->childs[3] = CACHE_INVALID;
            }

            this->hash  = hash;
            this->hash1 = hash1;
            this->hash2 = hash2;

            _ASSERT (*this->ptr == (this - cache));

            _ASSERT (this->ptr != &this->childs[0]);
            _ASSERT (this->ptr != &this->childs[1]);
            _ASSERT (this->ptr != &this->childs[2]);
            _ASSERT (this->ptr != &this->childs[3]);

            _ASSERT (*this->ptr != this->childs[0]);
            _ASSERT (*this->ptr != this->childs[1]);
            _ASSERT (*this->ptr != this->childs[2]);
            _ASSERT (*this->ptr != this->childs[3]);

            return CODE_NEW;
        }

        encode_s* const this = cache + id;

        if (this->hash  == hash  &&
            this->hash1 == hash1 &&
            this->hash2 == hash2) {
            /* Encontrou */

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
        }

        // CHILDS_BITS tem que ser divisor de 64
        //_ptr = &(_node)->childs[(hash >> (_level < (64 - CHILDS_BITS)))) & CHILDS_MASK];
        ptr = &this->childs[(hash >> level) & CHILDS_MASK];
        // TODO: FIXME: vai dar certo isso?
        if ((level += CHILDS_BITS) > 60)
            abort();
    }
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
    if (size >= 0b11111111111111111111U)
        return NULL;

    if (size >= CACHE_INVALID)
        return NULL;

    if (headSize > size)
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
        case 0b1111111111111111U: // 65535
            break;
        default:
            return NULL;
    }

    hash  += rdtscp();
    hash1 += hash;
    hash2 += hash1 >> 32;

    encode_context_s_i* const ctx = malloc(sizeof(encode_context_s) + size*sizeof(encode_s) + size*sizeof(u32) + headSize*sizeof(u32));

    if (ctx == NULL)
        return NULL;

    ctx->pos = 0;
    ctx->size = size;
    ctx->hash  = hash;
    ctx->hash1 = hash1;
    ctx->hash2 = hash2;
    ctx->indexes = (void*)(ctx->cache + size);
    ctx->heads = (void*)(ctx->indexes + size);
    ctx->headMask = headMask;

    // Initialize ctx->cache and ctx->indexes
    uint count = 0;

    do {
        encode_s* const this = &ctx->cache[count];

        this->hash  = 0;
        this->hash1 = 0;
        this->hash2 = 0;
        this->index = count;
        this->childs[0] = CACHE_INVALID;
        this->childs[1] = CACHE_INVALID;
        this->childs[2] = CACHE_INVALID;
        this->childs[3] = CACHE_INVALID;
        this->ptr = NULL;

        ctx->indexes[count] = count;

    } while (++count != size);

    count = 0;

    do {
        ctx->heads[count] = CACHE_INVALID;
    } while (++count != headSize);

    return (encode_context_s*)ctx;
}

static void fill(encode_context_s* const ctx, uintll count) {

    static u64 buff[64] = {
        0xa405f056054046e4ULL, 0x01d345f7893BaDEFULL,
        0x480fdbff42ff0134ULL, 0x4aa4f544f0fa6405ULL, 0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x540f845645246544ULL, 0x14545b4578540574ULL, 0x456074745424a407ULL, 0x445604fa403af044ULL,
        0x456f4587a3f54060ULL, 0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL,
        0x04f05d5ff4065f47ULL, 0x0054074050507007ULL, 0xa405f056054046e4ULL,
        0x01d345f7893BaDEFULL, 0x480fdbff42ff0134ULL, 0x4aa4f544f0fa6405ULL,
        0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x540f845645246544ULL, 0x14545b4578540574ULL, 0x456074745424a407ULL,
        0x445604fa403af044ULL, 0x456f4587a3f54060ULL, 0xe0f40460735dedf5ULL,
        0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL,
        0x0054074050507007ULL, 0xa405f056054046e4ULL, 0x01d345f7893BaDEFULL, 0x480fdbff42ff0134ULL,
        0x4aa4f544f0fa6405ULL, 0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x540f845645246544ULL, 0x14545b4578540574ULL, 0x456074745424a407ULL, 0x445604fa403af044ULL,
        0x456f4587a3f54060ULL, 0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL,
        0x0054074050507007ULL, 0xa405f056054046e4ULL, 0x01d345f7893BaDEFULL,
        0x480fdbff42ff0134ULL, 0x4aa4f544f0fa6405ULL,
        0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL, 0x540f845645246544ULL,
        0x14545b4578540574ULL, 0x456074745424a407ULL, 0x445604fa403af044ULL, 0x456f4587a3f54060ULL,
        0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL, 0x0054074050507007ULL,
        };

    u64 random1 = rdtscp();

    while (count--) {
        buff[0] += random1++;
        buff[1] += random1++;
        buff[2] += random1++;
        buff[3] += random1++;
        buff[4] += random1++;
        buff[5] += random1++;
        buff[6] += random1++;
        buff[7] += random1++;
        buff[8] += random1++;
        random1 += random1 << (count   & 0b11111U);
        random1 += count   << (random1 & 0b11111U);
        random1++;
        lookup(ctx, (void*)buff, 1 + (random1 & 0b111111U));
    }
}

int main (void) {

#define CACHE_SIZE 0xFFFF

    encode_context_s* const ctx = encoder_new(CACHE_SIZE, 64, 0, 0, 0);

    VERIFY_CTX(ctx); fill(ctx, 100*CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx); fill(ctx, CACHE_SIZE);
    VERIFY_CTX(ctx);

    return 0;
}
