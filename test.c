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

#if 1
static inline u64 rdtsc(void) {
    uint lo;
    uint hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((u64)hi << 32) | lo;
}
#else
#define rdtsc() ((u64)0)
#endif

// É o valor máximo que cabe em 1 byte, nos RBITS
// é (2^NUMERO_DE_BITS - 1)
// 0bCCCC 1111
#define CODE_CACHED_RBITS_MAX 15

#define CACHE_N 0xFFF
#define HEADS_N 65536
#define ENCODE_CHILDS_SIZE 3

typedef struct JASFEncodeNode {
    u64 hash0;
    u64 hash1;
    u64 hash2;
    u64 hash3; // SÓ NECESSÁRIO PARA TESTAR
    u32* ptr; // TODO: FIXME: transformar em i32, relativo ao ctx->cache
    u32 childs[ENCODE_CHILDS_SIZE];
    u32 index;
} JASFEncodeNode;

typedef struct JASFEncodeContext {
    uint cur;
    u32 indexes[CACHE_N];
    u32 heads[HEADS_N];
    JASFEncodeNode cache[CACHE_N];
} JASFEncodeContext;

#define ABSDIFF(a, b)  ((a) >= (b)) ? ((a) - (b)) : ((b) - (a))

/* Means no code; it was unknown and hashed instead */
#define ENCODE_SAME ((uint)-1)
#define ENCODE_NEW  ((uint)-2)

#if 1 // TODO: FIXME: stringify the line
#define ASSERT(condition) ({ if (!(condition)) { write(STDERR_FILENO, "\nASSERT FAILED: " #condition "\n", sizeof("\nASSERT FAILED: " #condition "\n")); abort(); } })
#endif

static inline uint jasf_encode_lookup_leaf_node(const JASFEncodeNode* const restrict cache, const uint node) {
    uint slot = ENCODE_CHILDS_SIZE - 1;
    do { const uint id = cache[node].childs[slot];
        if (id != CACHE_N)
            return jasf_encode_lookup_leaf_node(cache, id);
    } while (slot--);
    return node;
}

static uint jasf_encode_lookup(JASFEncodeContext* const restrict ctx, const void* restrict str, uint len) {

    JASFEncodeNode* const cache = ctx->cache;

    u64 hash0 = (u64)len;
    u64 hash1 = (u64)len << 16;
    u64 hash2 = (u64)len << 32;
    u64 hash3 = (u64)len << 48;

    while (len) { len -= sizeof(u8);
        const u64 w = *(u8*)str; str += sizeof(u8);
        hash3 += hash0;
        hash3 += hash1;
        hash3 += hash2;
        hash0 += w;
        hash1 ^= w;
        hash2 *= w;
        hash2 += hash0 & w;
        hash0 += hash1 & w;
        hash1 += hash2 & w;
        hash2 += hash3 >> (w % 64);
        hash1 += hash3 >> (w % 41);
        hash0 += hash3 >> (w % 29);
    }

    hash0 += hash0 >> 48;
    hash0 += hash0 >> 32;
    hash0 += hash0 >> 16;
    hash0 %= HEADS_N;

    u32* ptr = &ctx->heads[hash0];

    u64 level = hash3;

    loop {
        const uint thisID = *ptr;

        if (thisID == CACHE_N) {
            // NOT FOUND

            // SOBRESCREVE O MAIS ANTIGO
            JASFEncodeNode* this = cache + ctx->indexes[(ctx->cur = (ctx->cur + 1) % CACHE_N)];

            if (this->ptr) {

                // ANDA ATÉ UM LEAF DELE
                JASFEncodeNode* leaf = cache + jasf_encode_lookup_leaf_node(cache, this - cache);

                // SWAP IT BY ONE OF IT'S LEAVES
                const uint leafIndex = leaf->index;
                const uint thisIndex = this->index;

                ctx->indexes[thisIndex] = leaf - cache;
                ctx->indexes[leafIndex] = this - cache;

                this->hash0 = leaf->hash0;
                this->hash1 = leaf->hash1;
                this->hash2 = leaf->hash2;
                this->hash3 = leaf->hash3;
                this->index = leafIndex;

                this = leaf;
                this->index = thisIndex;

                //
                if (!((void*)this <= (void*)ptr && (void*)ptr < ((void*)this + sizeof(JASFEncodeNode)))) { // TODO: FIXME: PRECISA COLOCAR UM PADDING NO HEADS PARA QUE NÃO DE ERRO AQUI?
                   *this->ptr = CACHE_N;
                    this->ptr = ptr;
                   *this->ptr = this - cache;
                }
            } else { // VAI USAR UM QUE AINDA NÃO FOI USADO
                this->ptr = ptr;
               *this->ptr = this - cache;
            }

            this->hash0 = hash0;
            this->hash1 = hash1;
            this->hash2 = hash2;
            this->hash3 = hash3;

            return ENCODE_NEW;
        }

        JASFEncodeNode* const this = cache + thisID;

        if (this->hash0 == hash0 &&
            this->hash1 == hash1 &&
            this->hash2 == hash2) {
            // FOUND

            const uint thisIndex = this->index;
            const uint currIndex = ctx->cur;

            // 0  1  2  3  4  5  6  7  8  9   -> SIZE 10
            //      LAST        NEW
            // ENCODE        CODE = LAST + (NEW  > LAST)*SIZE - NEW
            // DECODE        NEW  = LAST + (CODE > LAST)*SIZE - CODE
            uint code = currIndex - 1;
            // Transforma o cur no último
            if (code >= CACHE_N)
                code += CACHE_N; // -1 + size = size - 1 (o index do último)
            // Faz a mágica
            // Detectamos o overflow para saber que daria algo negativo
            if ((code -= thisIndex) >= CACHE_N)
                code += CACHE_N;

            ASSERT (code < CACHE_N);
            ASSERT (thisIndex == (((currIndex?:CACHE_N)-1) + (code > ((currIndex?:CACHE_N)-1))*CACHE_N - code));
            ASSERT (code || (code - 1) == ENCODE_SAME);

            // Vai retornar (code - 1), entao:
            //      se aqui (code == 0) não houve diferença (continua no mesmo last); o -1 vai dar overflow e resultar em ENCODE_SAME
            //      se aqui (code >= (CODE_CACHED_RBITS_MAX + 1)) então nao cabe mais em uma mensagem de um byte
            // Só muda mesmo se não for mais caber em um byte
            // Assim evita ficar executando isso para diferenças pequenas caso sejam frequentes, até porque não vai fazer tanta diferença assim
            if (code > (CODE_CACHED_RBITS_MAX + 1)) {
                if (currIndex != thisIndex) { // se thisIndex == ctx->cur, não precisa copiar
                    const uint currID = ctx->indexes[currIndex];
                    ctx->indexes[currIndex] = thisID;
                    ctx->indexes[thisIndex] = currID;
                    ctx->cache[currID].index = thisIndex;
                    ctx->cache[thisID].index = currIndex;
                } ctx->cur = (currIndex + 1) % CACHE_N;
            }

            // Desconta 1 porque sempre haverá diferença
            return code - 1;
        }

        // As coisas só podem ser movidas para cima, pois não depende dos outros hashes.
        // Se colocou um node aqui, é porque o hash dele aceita este nível atual ou qualquer outro acima.
        // TODO: FIXME: mas isso só vale até certo nível?
        ptr = &this->childs[level % ENCODE_CHILDS_SIZE];

        level /= ENCODE_CHILDS_SIZE;
        level += (!level)*hash1;
    }
}

static JASFEncodeContext* jasf_encode_new (void) {

    JASFEncodeContext* const ctx = malloc(sizeof(JASFEncodeContext));

    if (ctx == NULL)
        return NULL;

    ctx->cur = 0;

    // INDEXES AND CACHE
    uint count = CACHE_N;

    while (count--) {

        ctx->cache[count].hash0 = 0;
        ctx->cache[count].hash1 = 0;
        ctx->cache[count].hash2 = 0;
        ctx->cache[count].hash3 = 0;
        ctx->cache[count].ptr = NULL;
        ctx->cache[count].childs[0] = CACHE_N;
        ctx->cache[count].childs[1] = CACHE_N;
        ctx->cache[count].childs[2] = CACHE_N;
        ctx->cache[count].index = count;

        ctx->indexes[count] = count;
    }

    // HEADS
    count = HEADS_N;

    while (count--) {
        ctx->heads[count] = CACHE_N;
    }

    return ctx;
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
    _COMMON("O RATO ROEU A ROUPA DO REI DE ROMA"),
    _COMMON("VOVO MAFALDA"),
    _COMMON("RECEITA DA VO SINHA"),
    _COMMON("AM I INSANE? OR AM I, SO SANE THAT I JUST... BLEW YOUR MIND? =]"),
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

    u64 random1 = 0;

    u64 buff[] = { // 20*5*8 = 800
        0xa4eb3146054046e4ULL, 0x01d345f7893BaDEFULL, 0x4f0436a564015456ULL, 0x5f42a5ae5da52d78ULL, 0x4a6faf3875405744ULL,
        0x480fdbff42ff0134ULL, 0x4aae4544f0ea6405ULL, 0x4f0436aa64015456ULL, 0x5f42a5a45d052d78ULL, 0x4a6faf3875405744ULL,
        0x540f826045246544ULL, 0x14545b45e8540574ULL, 0x45607e705424a407ULL, 0x4456042e40eaf044ULL, 0x445604fa403af044ULL,
        0x45ef42e7a0ff4060ULL, 0xe0f40460720dedf5ULL, 0x0847e974623a4650ULL, 0x44560ffa400af044ULL, 0x44e600fa403af044ULL,
        0x04f05d5ff4065f47ULL, 0x0054074f5a507407ULL, 0xa405e75005a036e4ULL, 0x04f05d5ff4e65f47ULL, 0x445604ea403af0f4ULL,
        0x01d342f7893BaDEFULL, 0x480fdbff42ff0134ULL, 0x4aaefa44f2f00405ULL, 0x04f05deff40a5f47ULL, 0x445604faa03af044ULL,
        0x4f0432304f015456ULL, 0xa242a5ad2da52d78ULL, 0x4a6fa33875405744ULL, 0x04eaef50f4060f47ULL, 0x445604fa403af044ULL,
        0x540e845645246544ULL, 0x14545b45e8741574ULL, 0x45607f73542ea007ULL, 0x00f05d5fe4065f47ULL, 0x445604fa403af044ULL,
        0x4e5600f4403af044ULL, 0x456f348723f54060ULL, 0xe0f40460235dedf5ULL, 0x04f05d5ff4065f47ULL, 0x4456e4fa403af044ULL,
        0x084ee956623a4650ULL, 0x04f05d5ef4065f47ULL, 0x4f0430a464015456ULL, 0x5f42a5a45da52d78ULL, 0x4a6faf3875405744ULL,
        0x0054060e5050e007ULL, 0xa405f05605f046e4ULL, 0x01de45308930aDEFULL, 0x480fdbff22ff0134ULL, 0x4456e4fa4e3af044ULL,
        0x4aa4f5f4f0ff6405ULL, 0x0f0436a504015456ULL, 0x5f42a5ee5da52d78ULL, 0x4a0faf3b75025744ULL, 0x445604f0403af044ULL,
        0x540f845645246544ULL, 0x14548b4508540574ULL, 0x456074745424af07ULL, 0x445604fa403af044ULL, 0x445604fa403af044ULL,
        0x456f4507a3254060ULL, 0x40f40e6073fdedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff406ff47ULL, 0x445604fae03af04fULL,
        0x0054074050507007ULL, 0xa405f056054042e4ULL, 0x01d34107890BaDEFULL, 0x04f0fd5ff4065f47ULL, 0x445604fa403af044ULL,
        0x480fdbff42ff0134ULL, 0x4aa4f544f0fa6405ULL, 0x4f0436ae64015f56ULL, 0x5f42aea45da52d08ULL, 0x4a6faf3875405f44ULL,
        0x4f0436a564015456ULL, 0x5f42a5a45da52d78ULL, 0x4a0faf3875405744ULL, 0x540f845645246f40ULL, 0x445604fae03af044ULL,
        0x14545b4578540574ULL, 0x4560747e5424a407ULL, 0x445604f5403af044ULL, 0x456f4587a3f54060ULL, 0x445604fa403af044ULL,
        0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL, 0x0054074050507007ULL, 0x445604fa403af044ULL,
        0xe0f40460735dedf5ULL, 0x0847f974623a4650ULL, 0x04f05d5ff4065f47ULL, 0x0054074050507007ULL, 0x445604fa403af044ULL,
        };

    uintll tests[] = { 1, 50, 51, 52, 100, 500, 768,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
         100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
        2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
        2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
        2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
        2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,
        4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096, 4096,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
        CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N, CACHE_N,
        10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N, 10*CACHE_N,
        100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N, 100*CACHE_N,
        500*CACHE_N, 500*CACHE_N, 500*CACHE_N, 500*CACHE_N, 500*CACHE_N, 500*CACHE_N, 500*CACHE_N, 500*CACHE_N,
        2048*CACHE_N, 2048*CACHE_N, 4096*CACHE_N, 4096*CACHE_N,
              CACHE_N*CACHE_N,
        500ULL*CACHE_N*CACHE_N,
        0
        };
    uint test = 0;
    uintll count;

    while ((count = tests[test++])) {

        JASFEncodeContext* const ctx = jasf_encode_new();

        ASSERT ( ctx != NULL );

        random1 += rdtsc() & 0xFFFFU;

        printf("BUFF %5u TEST %2u COUNT %12llu SEED 0x%016llX ...", (uint)sizeof(JASFEncodeContext), test, count, (uintll)random1);

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

            buff[ 0] += random1;
            buff[ 1] += random1;
            buff[ 2] += random1;
            buff[ 3] += random1;
            buff[ 4] += random1;
            buff[ 5] += random1;
            buff[ 6] += random1;
            buff[ 7] += random1;
            buff[ 8] += random1;
            buff[ 9] += random1;
            buff[10] += random1;
            buff[11] += random1;
            buff[12] += random1;
            buff[13] += random1;
            buff[14] += random1;
            buff[15] += random1;

            random1 += buff[random1 & 0b1111U];
            random1 += random1 << (count   & 0b11111U);
            random1 += random1 << (random1 & 0b11111U);
            random1++;

            const void* value;
            uint valueSize;

            if ((count % 35) > 15) {
                value = buff;
                valueSize = 1 + (random1 % (sizeof(buff) - 1));
            } else {
                value = COMMONS_VALUE(random1);
                valueSize = COMMONS_SIZE(random1);
            }

            const uint code = jasf_encode_lookup(ctx, value, valueSize);

            ASSERT (code < (CACHE_N - 1) || (code == ENCODE_NEW) || (code == ENCODE_SAME));

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

        count = 0;

        do {   //  ptr tem q ue estar entre o head ou o .child[] de algum

            // All cache[indexes[x]].index -> x
            ASSERT ( ctx->cache[ctx->indexes[count]].index == count );

            const JASFEncodeNode* const this = &ctx->cache[count];

            // Seu ptr tem que apontar para ele
            ASSERT ( this->ptr == NULL || *this->ptr == count );

            // Seu ptr não pode apontar para um próprio slot de child
            ASSERT ( (void*)this->ptr < (void*)this || (void*)this->ptr >= ((void*)this + sizeof(*this)) );

            // O valor no slot de seu child não pode ser si mesmo
            ASSERT (
                count != this->childs[0] &&
                count != this->childs[1] &&
                count != this->childs[2]
                );

        } while (++count != CACHE_N);

        // TODO: FIXME: All heads are 0 or its target points to it in it s ptr

        // All items in cache
            // all childs point to it
            // its *ptr points to it
            // it's ptr is not itself
            // it's childs are not itself
            // all ids are 0 <= id < size

        count = 0;

        do {
            const JASFEncodeNode* const this = &ctx->cache[count];

            if (this->ptr) {

                u64 level = this->hash3;

                uint id = ctx->heads[this->hash0 % HEADS_N];

                while (id != count) {
                    ASSERT (id != CACHE_N);
                    levels++;
                    id = ctx->cache[id].childs[level % ENCODE_CHILDS_SIZE];
                    level /= ENCODE_CHILDS_SIZE;
                    level += (!level)*this->hash1;
                }

                entries++;

            } else {
                ASSERT (this->hash0 == 0);
                ASSERT (this->hash1 == 0);
                ASSERT (this->hash2 == 0);
                ASSERT (this->hash3 == 0);
                ASSERT (this->ptr == NULL);
                ASSERT (this->childs[0] == CACHE_N);
                ASSERT (this->childs[1] == CACHE_N);
                ASSERT (this->childs[2] == CACHE_N);
                //ASSERT (this->index == count);
            }
        } while (++count != CACHE_N);

        // TODO: FIXME: se não é o mesmo, não pode ter hash igual

        printf(" NEW %3u %% CACHED %3u %% REPEATED %3u %%  LEVEL AVG %3llu SIZE %3u %%\n",
            (uint)((news*100)/valuesN), (uint)((cacheds*100)/valuesN), (uint)((repeateds*100)/valuesN), levels/entries, (uint)((sizeEncoded*100)/sizeReal)
            );

        free(ctx);
    }

    return 0;
}
