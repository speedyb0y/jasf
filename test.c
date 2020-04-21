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

#if 0
static inline u64 rdtscp(void) {
    uint lo;
    uint hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((u64)hi << 32) | lo;
}
#else
#define rdtscp() ((u64)0)
#endif

typedef struct encode_s encode_s;
typedef struct decode_s decode_s;

// É o valor máximo que cabe em 1 byte, nos RBITS
// é (2^NUMERO_DE_BITS - 1)
// 0bCCCC 1111
#define CODE_CACHED_RBITS_MAX 15

#define CACHE_SIZE_MIN  CODE_CACHED_RBITS_MAX
#define CACHE_SIZE_MAX      0xFFFFFU
#define CACHE_SIZE_INVALID 0x100000U

#define PTR_OF(cached) ((void*)(cached) - (void*)cache)
#define PTR_LOAD(ptr)  ((void*)cache + (ptr))
#define PTR_STORE(ptr, value)  ((ptr) = (void*)cache + (value))

#define ENCODE_CHILDS_SIZE 5

struct encode_s {
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32* ptr; // TODO: FIXME: transformar em u32
    u32 childs[ENCODE_CHILDS_SIZE];
    u32 index;
};

// Used to initialize
typedef struct encode_context_s_i {
    uint cur;
    uint size;
    uint headSize;
    u64 hash;
    u64 hash1;
    u64 hash2;
    u32* indexes;
    u32* heads;
    encode_s cache[];
} encode_context_s_i;

// Used everywhere to protect the members
typedef struct encode_context_s {
    uint cur;
    const uint size;
    const uint headSize;
    const u64 hash;
    const u64 hash1;
    const u64 hash2;
    u32* const indexes;
    u32* const heads;
    encode_s cache[];
} encode_context_s;

struct decode_s {
    u16 type;
};

#define ABSDIFF(a, b)  ((a) >= (b)) ? ((a) - (b)) : ((b) - (a))

/* Means no code; it was unknown and hashed instead */
#define ENCODE_SAME ((uint)-1)
#define ENCODE_NEW  ((uint)-2)

#if 1 // TODO: FIXME: stringify the line
#define ASSERT(condition) ({ if (!(condition)) { write(STDERR_FILENO, "\nASSERT FAILED: " #condition "\n", sizeof("\nASSERT FAILED: " #condition "\n")); abort(); } })
#endif

static inline uint leaf_node(const encode_s* const restrict cache, uint node) {
    uint slot = ENCODE_CHILDS_SIZE - 1;
    do { const uint id = cache[node].childs[slot];
        if (id != CACHE_SIZE_INVALID)
            return leaf_node(cache, id);
    } while (slot--);
    return node;
}

// a soma d etodos os indexes
// a soma de todos os estrutura->codigo
static uint lookup(encode_context_s* const restrict ctx, const void* restrict str, uint len) {

    u64 hash  = ctx->hash  + ((u64)len << (ctx->hash  & 0b11111U));
    u64 hash1 = ctx->hash1 + ((u64)len << (ctx->hash1 & 0b11111U));
    u64 hash2 = ctx->hash2 + ((u64)len << (ctx->hash2 & 0b11111U));

    while (len >= sizeof(u64)) {
        const u64 word = *(u64*)str; str += sizeof(u64);
        hash  += word;
        hash1 += hash  >> 32;
        hash2 += hash1 >> 32;
        hash  += hash2 >> 32;
        hash  += len;
        len -= sizeof(u64);
    }

    while (len) {
        const u64 word = *(u8*)str; str += sizeof(u8);
        hash  += word;
        hash1 += hash  >> 32;
        hash2 += hash1 >> 32;
        hash  += hash2 >> 32;
        hash  += len;
        len -= sizeof(u8);
    }

    hash1 += hash2;
    hash += hash >> 32;

    encode_s* const cache = ctx->cache;

    u64 level = hash;

    u32* ptr = ctx->heads + (hash1 % ctx->headSize);

    loop {
        const uint thisID = *ptr;

        // SE ESTÁ MOVENDO COISAS, TEM QUE MOVER O INDEX JUNTO
        // index -> hash?
        if (thisID == CACHE_SIZE_INVALID) {
            /* Não encontrou */

            encode_s* this = cache + ctx->indexes[(ctx->cur = (ctx->cur + 1) % ctx->size)]; // Sobrescreve o mais antigo
            encode_s* leaf = cache + leaf_node(cache, this - cache); // Encontra um leaf dele para substituir ele

            if (this->ptr == NULL) { // Vai usar um que ainda não foi usado
                this->ptr = ptr;
               *this->ptr = this - cache;
            } elif ((void*)this <= (void*)ptr && (void*)ptr < ((void*)this + sizeof(encode_s))) { // Vai usar um que seria o ptr
                // Vai consumir este, sendo que o node em si já serve para ele no hash tree
            } elif ((void*)leaf <= (void*)ptr && (void*)ptr < ((void*)leaf + sizeof(encode_s))) { // Vai usar um que possui um leaf, que seria o próprio ptr
                // Usa o leaf ao invés do this
                // this != leaf

                this->hash  = leaf->hash;
                this->hash1 = leaf->hash1;
                this->hash2 = leaf->hash2;

                const uint leafIndex = leaf->index;
                const uint thisID = this->index;

                this->index = leafIndex;
                leaf->index = thisID;

                ctx->indexes[thisID] = leaf - cache;
                ctx->indexes[leafIndex] = this - cache;

                this = leaf;
                // o leaf já seria o ptr, então já é um lugar em que pode colocar
            } elif (this == leaf) { // Vai usar um que não tem childs; não precisa mover nada
               *this->ptr = CACHE_SIZE_INVALID; // Tira ele de onde ele está
                this->ptr = ptr; // Coloca ele seguindo o path que seguimos
               *this->ptr = this - cache;
            } else { // Escolheu um que tem leaf
                // Nem o this e nem o leaf são o ptr
                // Usa o leaf ao invés do this

                this->hash  = leaf->hash;
                this->hash1 = leaf->hash1;
                this->hash2 = leaf->hash2;

                const uint leafIndex = leaf->index;
                const uint index = this->index;

                this->index = leafIndex;
                leaf->index = index;

                ctx->indexes[index] = leaf - cache;
                ctx->indexes[leafIndex] = this - cache;

                this = leaf;
               *this->ptr = CACHE_SIZE_INVALID; // Tira ele de onde ele está
                this->ptr = ptr; // Coloca ele seguindo o path que seguimos
               *this->ptr = this - cache;
            }

            this->hash  = hash;
            this->hash1 = hash1;
            this->hash2 = hash2;

            return ENCODE_NEW;
        }

        encode_s* const this = cache + thisID;

        if (this->hash  == hash  &&
            this->hash1 == hash1 &&
            this->hash2 == hash2) {
            /* Encontrou */

            const uint thisIndex = this->index;
            const uint curIndex = ctx->cur;

            //
            // 0  1  2  3  4  5  6  7  8  9   -> SIZE 10
            //      LAST        NEW
            // To encode
            //      CODE = LAST + (NEW  > LAST)*SIZE - NEW
            // To decode
            //      NEW  = LAST + (CODE > LAST)*SIZE - CODE
            //
            uint code;
            // Transforma o cur no último
            if ((code = curIndex - 1) >= CACHE_SIZE_INVALID)
                code += ctx->size; // -1 + size = size - 1 (o index do último)
            // Faz a mágica
            // Detectamos o overflow para saber que daria algo negativo
            if ((code -= thisIndex) >= CACHE_SIZE_INVALID)
                code += ctx->size;

            // Vai retornar (code - 1), entao:
            //      se aqui (code == 0) não houve diferença (continua no mesmo last); o -1 vai dar overflow e resultar em ENCODE_SAME
            //      se aqui (code >= (CODE_CACHED_RBITS_MAX + 1)) então nao cabe mais em uma mensagem de um byte
            // Só muda mesmo se não for mais caber em um byte
            // Assim evita ficar executando isso para diferenças pequenas caso sejam frequentes, até porque não vai fazer tanta diferença assim
            if (code > (CODE_CACHED_RBITS_MAX + 1)) {
                if (curIndex != thisIndex) { // se thisIndex == ctx->cur, não precisa copiar
                    const uint curID = ctx->indexes[curIndex];
                    ctx->indexes[curIndex] = thisID;
                    ctx->indexes[thisIndex] = curID;
                    ctx->cache[curID].index = thisIndex;
                    ctx->cache[thisID].index = curIndex;
                } ctx->cur = (curIndex + 1) % ctx->size;
            }

            ASSERT (ctx->size > code);

            ASSERT (thisIndex == (((curIndex?:ctx->size)-1) + (code > ((curIndex?:ctx->size)-1))*ctx->size - code));
            ASSERT (code || (code - 1) == ENCODE_SAME);

            // Desconta 1 porque sempre haverá diferença
            return code - 1;
        }

        // As coisas só podem ser movidas para cima, pois não depende dos outros hashes.
        // Se colocou um node aqui, é porque o hash dele aceita este nível atual ou qualquer outro acima.
        // TODO: FIXME: mas isso só vale até certo nível?
        ptr = &this->childs[level % ENCODE_CHILDS_SIZE];
        level /= ENCODE_CHILDS_SIZE;
        // TODO: FIXME: isso aqui vai nos salvar?
        level += (!level)*hash;
        // CHILDS_BITS tem que ser divisor de 64
        //ptr = &this->childs[(hash >> (level % 64)) & CHILDS_MASK];
        // TODO: FIXME: vai dar certo isso?
        // se só movemos para cima, então está colocando ele num lugar por onde ele já passou
        //level += CHILDS_BITS;
        //tuple((x%64) for x in range(0, 98, 2))
        //(0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32)
    }
}

#define ENCODE_BUFF_SIZE(size, headSize) (sizeof(encode_context_s) + (size)*(sizeof(encode_s) + sizeof(u32)) + (headSize)*sizeof(u32))

static encode_context_s* encoder_new (const uint size, const uint headSize, u64 hash, u64 hash1, u64 hash2) {

    if (size < CACHE_SIZE_MIN)
        return NULL;

    // TODO: FIXME: ver se esse valor está correto aqui
    if (size > CACHE_SIZE_MAX)
        return NULL;

    if (headSize < 2)
        return NULL;

    if (headSize > size)
        return NULL;

    hash  += rdtscp();
    hash1 += hash;
    hash2 += hash1 >> 32;

    encode_context_s_i* const ctx = malloc(ENCODE_BUFF_SIZE(size, headSize));

    if (ctx == NULL)
        return NULL;

    ctx->cur = 0;
    ctx->size = size;
    ctx->hash  = hash;
    ctx->hash1 = hash1;
    ctx->hash2 = hash2;
    ctx->indexes = (void*)(ctx->cache + size);
    ctx->heads = (void*)(ctx->indexes + size);
    ctx->headSize = headSize;

    // Initialize ctx->cache and ctx->indexes
    uint count = 0;

    do {
        encode_s* const this = &ctx->cache[count];

        this->hash  = 0;
        this->hash1 = 0;
        this->hash2 = 0;
        this->index = count;
        this->childs[0] = CACHE_SIZE_INVALID;
        this->childs[1] = CACHE_SIZE_INVALID;
        this->childs[2] = CACHE_SIZE_INVALID;
        this->childs[3] = CACHE_SIZE_INVALID;
#if ENCODE_CHILDS_SIZE > 4
        this->childs[4] = CACHE_SIZE_INVALID;
#if ENCODE_CHILDS_SIZE > 5
        this->childs[5] = CACHE_SIZE_INVALID;
        this->childs[6] = CACHE_SIZE_INVALID;
#endif
#endif
        this->ptr = NULL;

        ctx->indexes[count] = count;

    } while (++count != size);

    count = 0;

    do {
        ctx->heads[count] = CACHE_SIZE_INVALID;
    } while (++count != headSize);

    return (encode_context_s*)ctx;
}

#define COMMONS_N (sizeof(commons)/(2*sizeof(char*)))
#define COMMONS_VALUE(x) (commons[((x) % COMMONS_N)*2])
#define COMMONS_SIZE(x) ((uint)(uintll)commons[((x) % COMMONS_N)*2+1])
#define _COMMON(string) string, (const char*)sizeof(string)

static const char* commons[] = {
    _COMMON("UVA"),
    _COMMON("MELANCIA"),
    _COMMON("BETERRABA"),
    _COMMON("CAMISOLA"),
    _COMMON("CAMISA"),
    _COMMON("CAMIZINHA"),
    _COMMON("AZUL"),
    _COMMON("BRASIL"),
    _COMMON("ORGULHO"),
    _COMMON("GENEROSO"),
    _COMMON("INTELIGENCIA"),
    _COMMON("CASA"),
    _COMMON("PROMISSORA"),
    _COMMON("ERIKA"),
    _COMMON("AGUBA"),
    _COMMON("WILLIAN"),
    _COMMON("AMOR"),
    _COMMON("SOMBRANCELHA"),
    _COMMON("ARMAGEDOM"),
    _COMMON("ALIENS"),
    _COMMON("ASSINCRONICIDADE"),
    _COMMON("PALAVRAS ALEATORIAS ALEATORIAMENTE SELECIONASDAS"),
    _COMMON("ATROCIDADE"),
    _COMMON("JOGO DE COMPUTADOR"),
    _COMMON("JOGO DE VIDEO GAME"),
    _COMMON("JOGO DE PANELA"),
    _COMMON("JOGO DE TABULEIRO"),
    _COMMON("ESTA EH UMA FRASE BEEEM GRANDONA GRANDONA GIGANTE GIGANTESCAMESMO OLHA SO COMO EH GRANDE E TOSCA ESSA FRASE GENTE VOU GANHAR UM OSCAR POR TER ESCRITO ELA IIHAAA"),
    _COMMON("EIWFIGWEWIOFGUY Y23 78F Y2HCDSAJ CGHW D2TY3 DUGBHE2DUI234W9E8FUH3g43g2YE 89DIJX 8Y29HUX 89UHFC2EW 8CV9HU2E8CH9U2E 78HX29HUC 29U"),
    _COMMON("EIWFIGWEEWG39WEG 234G3BVREHH56HN56BFUIWE IFHJASN 7EUIWY34W9E8FUHCWEJC2YE 89DIJX 8Y29HUX 89UHFC2EW 8CV9HU2E8CH9U2E 78HX29HUC 29U"),
    _COMMON("ewu 9w2evu89 2uy3890 ui2vhe 8392 yhdjv87923ehv23y89ifhh3hloi423yfh2u hy98v2e0ivhevv 2o v209i io I HU HUu hjk ku kjveHX29HUC 29U"),
    _COMMON("Ewevwn jcsjwed vjhh ewv ew vgyJH67H6r8ew3 bcewdnv28g euhif32y98hcuijvwbdojbovwd  w29HUX 89UHFC2EW 8CV95NBRQWERU2E 78HX29HUC 29U"),
    _COMMON("EIWFIew78gv546rgbvr8h7gw0c8 w4eqf 2ikewggweHJASN 7EUIWY34W9E8FUHCWEJC2YE 8923f3rf239HUX 89UHFC2EW 8CV9HU2E8CH9U2E 78HX29HUC 29U"),
    _COMMON("EIWFIGWEHFG65J5J6KEWHUI WHEIUFWIUEHFUIWE IFHJASN 7EUIWY34W9E8FUHCWEJK67U89DIJX 8Y29HUX 8B56B52EW 8CV9HWd18CH9B65B56HX29HUC 29U"),
    _COMMON("EIWFIGewgvoiewov ioewuio 32o r7328 fjkvdy238e hf2ojkl23 e8fy2eui wjk 2k3 fe2IJX 8Y29HUX 89UHFC2EW 8CV9HU2E8CH9U2E 78HX29HUC 29U"),
};

int main (void) {

    uint cacheSize;

    u64 random1 = 0;

    u64 buff[] = { // 20*5*8 = 800
        0xa405f056054046e4ULL, 0x01d345f7893BaDEFULL, 0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x480fdbff42ff0134ULL, 0x4aaef544f0ea6405ULL, 0x4f0436af64015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x540f845645246544ULL, 0x14545b4578540574ULL, 0x456074745424a407ULL, 0x445604fe40eaf044ULL, 0x445604fa403af044ULL,
        0x45ef4587a0ff4060ULL, 0xe0f404607f5dedf5ULL, 0x0847e97f623a4650ULL, 0x44560ffa400af044ULL, 0x44e600fa403af044ULL,
        0x04f05d5ff4065f47ULL, 0x0054074f50507007ULL, 0xa405e056054046e4ULL, 0x04f05d5ff4e65f47ULL, 0x445604fa403af0f4ULL,
        0x01d345f7893BaDEFULL, 0x480fdbff42ff0134ULL, 0x4aa4f544f0f00405ULL, 0x04f05deff40f5f47ULL, 0x445604fa403af044ULL,
        0x4f0436a06f015456ULL, 0x5f42a5a05da52d78ULL, 0x4a6faf3875405744ULL, 0x04e0ef5ff4060f47ULL, 0x445604fa403af044ULL,
        0x540f845645246544ULL, 0x14545b45f8540574ULL, 0x45607f74542ea407ULL, 0x00f05d5ff4065f47ULL, 0x445604fa403af044ULL,
        0x4e5604fa403af044ULL, 0x456f4587a3f54060ULL, 0xe0f40460735dedf5ULL, 0x04f05d5ff4065f47ULL, 0x4456e4fa403af044ULL,
        0x084ee974623a4650ULL, 0x04f05d5ef4065f47ULL, 0x4f0436a064015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x0054074e5050e007ULL, 0xa405f05605f046e4ULL, 0x01de45f78930aDEFULL, 0x480fdbff42ff0134ULL, 0x445604fa4e3af044ULL,
        0x4aa4f5f4f0ff6405ULL, 0x4f0436a504015456ULL, 0x5f42a5e45da52d78ULL, 0x4a6faf3e75005744ULL, 0x445604f0403af044ULL,
        0x540f845645246544ULL, 0x14545b4508540574ULL, 0x456074745424af07ULL, 0x445604fa403af044ULL, 0x445604fa403af044ULL,
        0x456f4507a3f54060ULL, 0xe0f4046073fdedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff406ff47ULL, 0x445604fae03af04fULL,
        0x0054074050507007ULL, 0xa405f056054046e4ULL, 0x01d345f7890BaDEFULL, 0x04f0fd5ff4065f47ULL, 0x445604fa403af044ULL,
        0x480fdbff42ff0134ULL, 0x4aa4f544f0fa6405ULL, 0x4f0436ae64015f56ULL, 0x5f42aea45da52d08ULL, 0x4a6faf3875405f44ULL,
        0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL, 0x540f845645246f40ULL, 0x445604fae03af044ULL,
        0x14545b4578540574ULL, 0x4560747e5424a407ULL, 0x445604f0403af044ULL, 0x456f4587a3f54060ULL, 0x445604fa403af044ULL,
        0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL, 0x0054074050507007ULL, 0x445604fa403af044ULL,
        0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL, 0x0054074050507007ULL, 0x445604fa403af044ULL,
        };

    static const uint cacheSizes[] = {
         0xFFFF,   64,
         0xFFFF,  512,
         0xFFFF, 4096,
         0xFFFF, 7049,
         0xFFFF, 8192,
        0xFFFFF,   64,
        0xFFFFF,  512,
        0xFFFFF, 4096,
        0xFFFFF, 8192,
        0
        };

    uint x = 0;

    while ((cacheSize = cacheSizes[x*2])) {

        uint headSize = cacheSizes[x++*2 + 1];

        uintll tests[] = { 1, 1, 2, 2, 100, 200, 300, 400, 500, 1000, 500, 1, 1, 15*cacheSize, 50*cacheSize, 50*cacheSize, 50*cacheSize, 0 };
        uint test = 0;
        uintll count;

        while ((count = tests[test++])) {

            encode_context_s* const ctx = encoder_new(cacheSize, headSize, 0, 0, 0);

            ASSERT ( ctx != NULL );
            ASSERT ( ctx->size == cacheSize );
            ASSERT ( ctx->headSize == headSize );

            random1 += rdtscp();

            printf("CACHE SIZE %7u HEAD SIZE %5u BUFF %8llu TEST %2u COUNT %12llu SEED 0x%016llX ...", cacheSize, headSize, (uintll)ENCODE_BUFF_SIZE(cacheSize, headSize), test, count, (uintll)random1);

            fflush(stdout);

            uintll news = 0;
            uintll repeateds = 0;
            uintll cacheds = 0;
            uintll levels = 0;
            uintll entries = 0;
            uintll sizeEncoded = 0;
            uintll sizeReal = 0;

            uintll valuesN = count;

            while (count--) {

                buff[ 0] += random1++;
                buff[ 1] += random1++;
                buff[ 2] += random1++;
                buff[ 3] += random1++;
                buff[ 4] += random1++;
                buff[ 5] += random1++;
                buff[ 6] += random1++;
                buff[ 7] += random1++;
                buff[ 8] += random1++;
                buff[ 9] += random1++;
                buff[10] += random1++;

                random1 += random1 << (count   & 0b11111U);
                random1 += count   << (random1 & 0b11111U);
                random1++;

                const void* value;
                uint valueSize;

                if ((count % 30) > 15) {
                    value = buff;
                    valueSize = 1 + (random1 % (sizeof(buff) - 1));
                } else {
                    value = COMMONS_VALUE(random1);
                    valueSize = COMMONS_SIZE(random1);
                }

                const uint code = lookup(ctx, value, valueSize);

                ASSERT (code < (cacheSize - 1) || (code == ENCODE_NEW) || (code == ENCODE_SAME));

                switch (code) {
                    case ENCODE_SAME:
                        sizeEncoded += 1;
                        repeateds++;
                        break;
                    case ENCODE_NEW:
                        sizeEncoded += 1 + valueSize;
                        news++;
                        break;
                    default: /* TODO: FIXME: manter isso em sincronia com as definições/configurações */
                        sizeEncoded += (code > 0xFFF) ? 3 : (code >= 0xF) ? 2 : 1;
                        cacheds++;
                }

                sizeReal += 1 + valueSize;
            }

            count = ctx->size;

            while (count--) {   //  ptr tem q ue estar entre o head ou o .child[] de algum

                // All cache[indexes[x]].index -> x
                ASSERT ( ctx->cache[ctx->indexes[count]].index == count );

                const encode_s* const this = &ctx->cache[count];

                // Seu ptr tem que apontar para ele
                ASSERT ( this->ptr == NULL || *this->ptr == count );

                // Seu ptr não pode apontar para um próprio slot de child
                ASSERT ( (void*)this->ptr < (void*)this || (void*)this->ptr >= ((void*)this + sizeof(*this)) );

                // O valor no slot de seu child não pode ser si mesmo
                ASSERT (
                    count != this->childs[0] &&
                    count != this->childs[1] &&
                    count != this->childs[2] &&
                    count != this->childs[3] &&
                    count != this->childs[4]
                    );

                //
                ASSERT (!(
                    (this->childs[0] > ctx->size && this->childs[0] != CACHE_SIZE_INVALID) ||
                    (this->childs[1] > ctx->size && this->childs[1] != CACHE_SIZE_INVALID) ||
                    (this->childs[2] > ctx->size && this->childs[2] != CACHE_SIZE_INVALID) ||
                    (this->childs[3] > ctx->size && this->childs[3] != CACHE_SIZE_INVALID) ||
                    (this->childs[4] > ctx->size && this->childs[4] != CACHE_SIZE_INVALID)
                    ));
            }

            // TODO: FIXME: All heads are 0 or its target points to it in it s ptr

            // All items in cache
                // all childs point to it
                // its *ptr points to it
                // it's ptr is not itself
                // it's childs are not itself
                // all ids are 0 <= id < size

            count = cacheSize;

            while (count--) {

                const encode_s* const this = &ctx->cache[count];

                if (this->ptr) {

                    u64 level = this->hash;
                    uint id = ctx->heads[this->hash1 % ctx->headSize];

                    while (id != count) {
                        ASSERT (id != CACHE_SIZE_INVALID);
                        levels++;
                        id = ctx->cache[id].childs[level % ENCODE_CHILDS_SIZE];
                        level /= ENCODE_CHILDS_SIZE;
                        level += (!level)*this->hash;
                    }

                    entries++;

                } else {
                    ASSERT (this->ptr == NULL);
                    ASSERT (this->childs[0] == CACHE_SIZE_INVALID);
                    ASSERT (this->childs[1] == CACHE_SIZE_INVALID);
                    ASSERT (this->childs[2] == CACHE_SIZE_INVALID);
                    ASSERT (this->childs[3] == CACHE_SIZE_INVALID);
                    ASSERT (this->childs[4] == CACHE_SIZE_INVALID);
                    //ASSERT (this->index == count);
                    ASSERT (this->hash == 0);
                    ASSERT (this->hash1 == 0);
                    ASSERT (this->hash2 == 0);
                }
            }

            // TODO: FIXME: se não é o mesmo, não pode ter hash igual

            printf(" NEWS (%%) %3u CACHEDS (%%) %3u REPEATEDS (%%) %3u LEVEL AVG %3llu SIZE (%%) %3u\n",
                (uint)((news*100)/valuesN), (uint)((cacheds*100)/valuesN), (uint)((repeateds*100)/valuesN), levels/entries, (uint)((sizeEncoded*100)/sizeReal)
                );

            free(ctx);
        }
    }

    return 0;
}
