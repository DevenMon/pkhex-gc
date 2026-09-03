#include "sha1.h"
#include <string.h>

typedef struct Sha1Ctx {
    uint32_t h[5];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} Sha1Ctx;

static uint32_t rol32(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }
static uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void wr32be(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

static void transform(Sha1Ctx *c, const uint8_t block[64]) {
    uint32_t w[80];
    for (unsigned i=0;i<16;i++) w[i]=rd32be(block+i*4);
    for (unsigned i=16;i<80;i++) w[i]=rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],cc=c->h[2];
    for (unsigned i=0;i<80;i++) {
        uint32_t f,k;
        if (i<20) { f=(b&cc)|((~b)&d); k=0x5A827999u; }
        else if (i<40) { f=b^cc^d; k=0x6ED9EBA1u; }
        else if (i<60) { f=(b&cc)|(b&d)|(cc&d); k=0x8F1BBCDCu; }
        else { f=b^cc^d; k=0xCA62C1D6u; }
        uint32_t t=rol32(a,5)+f+e+k+w[i];
        e=d; d=cc; cc=rol32(b,30); b=a; a=t;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e;
}

static void sha1_init(Sha1Ctx *c) {
    c->h[0]=0x67452301u; c->h[1]=0xEFCDAB89u; c->h[2]=0x98BADCFEu; c->h[3]=0x10325476u; c->h[4]=0xC3D2E1F0u;
    c->bits=0; c->used=0;
}
static void sha1_update(Sha1Ctx *c, const uint8_t *p, size_t n) {
    c->bits += (uint64_t)n * 8u;
    while (n) {
        size_t take=64-c->used; if (take>n) take=n;
        memcpy(c->block+c->used,p,take); c->used+=take; p+=take; n-=take;
        if (c->used==64) { transform(c,c->block); c->used=0; }
    }
}
static void sha1_final(Sha1Ctx *c, uint8_t out[20]) {
    c->block[c->used++]=0x80;
    if (c->used>56) { memset(c->block+c->used,0,64-c->used); transform(c,c->block); c->used=0; }
    memset(c->block+c->used,0,56-c->used);
    uint64_t bits=c->bits;
    for (unsigned i=0;i<8;i++) c->block[63-i]=(uint8_t)(bits>>(i*8));
    transform(c,c->block);
    for (unsigned i=0;i<5;i++) wr32be(out+i*4,c->h[i]);
}
void pkhexgc_sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    Sha1Ctx c; sha1_init(&c); sha1_update(&c,data,len); sha1_final(&c,out);
}
