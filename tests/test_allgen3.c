#include "gen3_all.h"
#include "sha1.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static void p16be(uint8_t*p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void p32be(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void p64be(uint8_t*p,uint64_t v){p32be(p,(uint32_t)(v>>32));p32be(p+4,(uint32_t)v);}
static void gc_text(uint8_t *p,size_t bytes,const char*s){memset(p,0,bytes);for(size_t i=0;s[i]&&2*i+1<bytes;i++)p16be(p+2*i,(uint8_t)s[i]);}
static void be_double(uint8_t*p,double d){union{double d;uint64_t u;}v;v.d=d;p64be(p,v.u);}

static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t colo_hc(const uint8_t *s,const uint8_t h[20]){uint32_t r=0;for(unsigned i=0;i<0x18;i+=4)r-=be32(s+i);r-=be32(s+0x18)^~be32(h);r-=be32(s+0x1C)^~be32(h+4);return r;}
static void colo_set_checksums(uint8_t *slot,size_t len){uint8_t h[20];memset(slot+0x0C,0,4);pkhexgc_sha1(slot,len-40,h);memcpy(slot+len-20,h,20);p32be(slot+0x0C,colo_hc(slot,h));}
static int colo_checksums_valid(uint8_t *slot,size_t len){uint8_t h[20],hcbytes[4];uint32_t old=be32(slot+0x0C);memcpy(hcbytes,slot+0x0C,4);memset(slot+0x0C,0,4);pkhexgc_sha1(slot,len-40,h);uint32_t want=colo_hc(slot,h);memcpy(slot+0x0C,hcbytes,4);return old==want&&!memcmp(slot+len-20,h,20);}
static void colo_encrypt(uint8_t *slot,size_t len){
    uint8_t digest[20],next[20];for(unsigned i=0;i<20;i++)digest[i]=(uint8_t)~slot[len-20+i];
    uint8_t *p=slot+0x18,*end=slot+len-20;
    while((size_t)(end-p)>=20){for(unsigned i=0;i<20;i++)p[i]^=digest[i];pkhexgc_sha1(p,20,next);memcpy(digest,next,20);p+=20;}
}
static void colo_decrypt_test(uint8_t *slot,size_t len){uint8_t digest[20],next[20];for(unsigned i=0;i<20;i++)digest[i]=(uint8_t)~slot[len-20+i];uint8_t*p=slot+0x18,*end=slot+len-20;while((size_t)(end-p)>=20){pkhexgc_sha1(p,20,next);for(unsigned i=0;i<20;i++)p[i]^=digest[i];memcpy(digest,next,20);p+=20;}}

static void adv(uint16_t k[4]){
    uint16_t k3=(uint16_t)(k[3]+0x13),k2=(uint16_t)(k[2]+0x17),k1=(uint16_t)(k[1]+0x29),k0=(uint16_t)(k[0]+0x43),n[4];
    n[3]=(uint16_t)(((k0>>12)&0xF)|((k1>>8)&0xF0)|((k2>>4)&0xF00)|(k3&0xF000));
    n[2]=(uint16_t)(((k0>>8)&0xF)|((k1>>4)&0xF0)|(k2&0xF00)|((k3<<4)&0xF000));
    n[1]=(uint16_t)(((k0>>4)&0xF)|(k1&0xF0)|((k2<<4)&0xF00)|((k3<<8)&0xF000));
    n[0]=(uint16_t)((k0&0xF)|((k1<<4)&0xF0)|((k2<<8)&0xF00)|((k3<<12)&0xF000));memcpy(k,n,sizeof(n));
}
static void xd_encrypt(uint8_t *slot,size_t len){
    uint16_t k[4];for(unsigned i=0;i<4;i++)k[i]=be16(slot+8+2*i);
    unsigned word=0;for(size_t off=0x10;off+1<len-0x28;off+=2,word++){
        uint16_t v=be16(slot+off);p16be(slot+off,(uint16_t)(v+k[word&3]));if((word&3)==3)adv(k);
    }
}

static uint16_t boxsum(const uint8_t*b){uint32_t s=0;s+=be16(b+4)+be16(b+6)+be16(b+8)+be16(b+10);for(size_t i=0xC;i<0x1FFC;i+=2)s+=be16(b+i);return(uint16_t)s;}
static void finalize_box_block(uint8_t*b){uint16_t s=boxsum(b);p16be(b,s);p16be(b+2,(uint16_t)(0xF004u-s));}
static void box_payload_write(uint8_t *raw,unsigned half,size_t off,const void*src,size_t n){
    while(n){unsigned id=(unsigned)(off/0x1FF0u);size_t in=off%0x1FF0u;size_t take=0x1FF0u-in;if(take>n)take=n;
        uint8_t*b=raw+0x2000u+(size_t)half*23u*0x2000u+(size_t)id*0x2000u;
        memcpy(b+0xC+in,src,take);off+=take;src=(const uint8_t*)src+take;n-=take;
    }
}


static void test_sha1_vector(void){
    static const uint8_t expected[20]={
        0xA9,0x99,0x3E,0x36,0x47,0x06,0x81,0x6A,0xBA,0x3E,
        0x25,0x71,0x78,0x50,0xC2,0x6C,0x9C,0xD0,0xD8,0x9D};
    uint8_t out[20]; pkhexgc_sha1((const uint8_t*)"abc",3,out); assert(memcmp(out,expected,20)==0);
}

static void test_colosseum(void){
    uint8_t *raw=calloc(1,GEN3_COLO_SIZE);assert(raw);uint8_t *s=raw+0x6000;
    s[0]=1;s[1]=1;p32be(s+4,7);gc_text(s+0x78,20,"WES");p16be(s+0xA4,222);p16be(s+0xA6,111);p32be(s+0xAFC,5000);
    union{float f;uint32_t u;}tm;tm.f=3661.0f;p32be(s+0x28,tm.u);
    uint8_t *pk=s+0xA8;p16be(pk,25);p32be(pk+4,0x12345678);p16be(pk+0x14,222);p16be(pk+0x16,111);gc_text(pk+0x44,22,"PIKACHU");gc_text(pk+0x18,22,"WES");pk[0x60]=30;
    p16be(pk+0x78,85);pk[0x7A]=15;p16be(pk+0x88,19);p16be(pk+0xB0,200);p16be(pk+0xD8,7);p32be(pk+0xDC,5000);
    /* Every field the port reads, each with a value of its own. */
    pk[0x08]=3;pk[0x0B]=2;p16be(pk+0x0C,88);pk[0x0E]=17;pk[0x0F]=4;pk[0x10]=1;p32be(pk+0x5C,15625);
    for(unsigned i=0;i<4;i++){p16be(pk+0x78+i*4,(uint16_t)(85+i));pk[0x7A+i*4]=(uint8_t)(15-i);pk[0x7B+i*4]=(uint8_t)(i&3);}
    for(unsigned i=0;i<6;i++){p16be(pk+0x98+i*2,(uint16_t)(10*(i+1)));p16be(pk+0xA4+i*2,(uint16_t)(31-i*2));}
    for(unsigned i=0;i<5;i++)pk[0xB2+i]=(uint8_t)(7*(i+1));
    for(unsigned i=0;i<5;i++)pk[0xB7+i]=(uint8_t)(i%5);
    pk[0xBC]=60;pk[0xBD]=1;pk[0xC8]=1;                 /* champion and world */
    pk[0xCA]=3;pk[0xD0]=4;pk[0xCB]=0;pk[0xCC]=1;pk[0xCD]=0;pk[0xCF]=0x0B;
    pk[0xFB]=1;                                        /* fateful, non-Japanese */
    /* Bag: Items slot 0, Key Items slot 1, Berries slot 2. Base 0x7F8. */
    p16be(s+0x7F8+0x000,13);p16be(s+0x7F8+0x002,42);
    p16be(s+0x7F8+0x050+4,260);p16be(s+0x7F8+0x052+4,1);
    p16be(s+0x7F8+0x23C+8,133);p16be(s+0x7F8+0x23E + 8,777);
    uint8_t *bx=s+0xB90;gc_text(bx,16,"BOX1");uint8_t*bpk=bx+0x14;p16be(bpk,277);bpk[0x60]=15;gc_text(bpk+0x44,22,"TREECKO");
    /* The Strategy Memo, at a fixed offset in Colosseum. Entry 1 has the
     * "complete entry" flag set, which Colosseum reads as not owned. */
    {
        uint8_t *memo=s+0x082B0;
        p16be(memo,2);
        uint8_t *e0=memo+4,*e1=e0+GEN3_MEMO_ENTRY_SIZE;
        p16be(e0,25);p16be(e0+4,222);p16be(e0+6,111);p32be(e0+8,0xDEADBEEFu);
        p16be(e1,277);e1[0]|=0x80;
    }
    colo_set_checksums(s,0x1E000);assert(colo_checksums_valid(s,0x1E000));colo_encrypt(s,0x1E000);
    Gen3AnySave a;assert(gen3_any_open(&a,raw,GEN3_COLO_SIZE));assert(a.kind==GEN3_KIND_COLOSSEUM&&a.integrity_ok);assert(gen3_any_can_edit(&a));assert(a.tid==111&&a.sid==222&&a.money==5000&&a.played_seconds==3661);assert(strcmp(a.trainer_name,"WES")==0);assert(a.party_count==1&&a.box_count==3);

    /* Colosseum has no seen flag: an entry is seen while it holds a species,
     * and the flag XD uses for seen means owned here instead. */
    assert(gen3_any_has_memo(&a) && gen3_any_memo_count(&a)==2);
    {
        Gen3MemoEntry e;
        assert(gen3_any_memo_entry(&a,0,&e));
        assert(e.species_internal==25 && e.tid==111 && e.sid==222);
        assert(e.pid==0xDEADBEEFu && e.seen && e.owned);
        assert(gen3_any_memo_entry(&a,1,&e) && e.seen && !e.owned);

        /* Clearing seen clears the entry, which is what PKHeX does; setting it
         * back is not something the format can express. */
        assert(gen3_any_set_memo_seen(&a,1,false));
        assert(gen3_any_memo_entry(&a,1,&e) && !e.seen && e.species_internal==0);
        assert(!gen3_any_set_memo_seen(&a,1,true));
    }
    Gen3Pokemon p;assert(gen3_any_party_pokemon(&a,0,&p)&&p.present&&p.species_internal==25&&p.level==30);
    assert(p.tid==111&&p.sid==222&&p.moves[0]==85&&p.pp[0]==15&&p.held_item==19&&p.friendship==200&&p.checksum_ok);
    assert(p.is_shadow&&p.shadow_id==7&&p.purification==5000);
    assert(p.origin_game==3&&p.language==2&&p.met_location==88&&p.met_level==17&&p.ball==4&&p.ot_gender==1);
    assert(p.experience==15625&&p.ability_bit&&!p.is_egg&&p.fateful);
    assert(p.markings==0x0D&&p.pokerus==0x34&&p.pp_ups==0xE4);
    assert(strcmp(p.nickname,"PIKACHU")==0&&strcmp(p.ot_name,"WES")==0);
    /* Colosseum stores SpA and SpD before Speed, like XD. */
    assert(p.evs[0]==10&&p.evs[1]==20&&p.evs[2]==30&&p.evs[3]==60&&p.evs[4]==40&&p.evs[5]==50);
    assert(p.ivs[0]==31&&p.ivs[1]==29&&p.ivs[2]==27&&p.ivs[3]==21&&p.ivs[4]==25&&p.ivs[5]==23);
    for(unsigned i=0;i<5;i++)assert(p.contest[i]==7*(i+1));
    assert(p.contest[5]==60);
    for(unsigned i=0;i<5;i++)assert(gen3_contest_ribbon(&p,i)==i%5);
    assert(gen3_ribbon_flag(&p,0)&&gen3_ribbon_flag(&p,11)&&!gen3_ribbon_flag(&p,1));
    assert(gen3_any_box_pokemon(&a,0,0,&p)&&p.species_internal==277);

    /* Colosseum's bag: six pouches, no PC box and no battle discs. */
    Gen3ItemSlot it;
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_ITEMS)==20);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_KEY_ITEMS)==43);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_COLOGNE)==3);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_PC)==0);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_DISCS)==0);
    assert(gen3_any_pocket_max_quantity(&a,GEN3_POCKET_KEY_ITEMS)==1);
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_ITEMS,0,&it)&&it.item_id==13&&it.quantity==42);
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_KEY_ITEMS,1,&it)&&it.item_id==260&&it.quantity==1);
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_BERRIES,2,&it)&&it.item_id==133&&it.quantity==777);
    assert(!gen3_any_get_item_slot(&a,GEN3_POCKET_ITEMS,20,&it));
    /* A key item pouch clamps to one, whatever it is handed. */
    assert(gen3_any_set_item_slot(&a,GEN3_POCKET_KEY_ITEMS,0,261,99));
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_KEY_ITEMS,0,&it)&&it.item_id==261&&it.quantity==1);
    assert(gen3_any_set_item_slot(&a,GEN3_POCKET_ITEMS,3,19,7));

    assert(gen3_any_set_tid(&a,54321));assert(gen3_any_set_sid(&a,12345));assert(gen3_any_set_money(&a,7654321));assert(gen3_any_set_trainer_gender(&a,1));assert(gen3_any_set_played_seconds(&a,7322));
    assert(gen3_any_party_pokemon(&a,0,&p));p.held_item=182;p.moves[0]=94;p.pp[0]=20;p.ivs[0]=30;p.evs[3]=77;p.friendship=155;p.tid=54321;p.sid=12345;
    p.contest[2]=99;p.met_level=44;p.ball=12;p.fateful=false;
    assert(gen3_set_contest_ribbon(&p,3,4));assert(gen3_set_ribbon_flag(&p,5,true));
    assert(gen3_any_set_party_pokemon(&a,0,&p));
    uint8_t *out=calloc(1,GEN3_COLO_SIZE);assert(out);assert(gen3_any_export(&a,out,GEN3_COLO_SIZE));gen3_any_close(&a);
    uint8_t checkslot[0x1E000];memcpy(checkslot,out+0x6000,0x1E000);colo_decrypt_test(checkslot,0x1E000);assert(colo_checksums_valid(checkslot,0x1E000));
    Gen3AnySave b;assert(gen3_any_open(&b,out,GEN3_COLO_SIZE));assert(b.kind==GEN3_KIND_COLOSSEUM);assert(b.tid==54321&&b.sid==12345&&b.money==7654321&&b.trainer_gender==1&&b.played_seconds==7322);assert(gen3_any_party_pokemon(&b,0,&p));assert(p.held_item==182&&p.moves[0]==94&&p.pp[0]==20&&p.ivs[0]==30&&p.evs[3]==77&&p.friendship==155&&p.tid==54321&&p.sid==12345);assert(p.is_shadow&&p.shadow_id==7&&p.purification==5000);
    assert(p.contest[2]==99&&p.met_level==44&&p.ball==12&&!p.fateful);
    assert(gen3_contest_ribbon(&p,3)==4&&gen3_ribbon_flag(&p,5)&&gen3_ribbon_flag(&p,0));
    /* Untouched fields survive the write. */
    assert(p.origin_game==3&&p.language==2&&p.met_location==88&&p.ability_bit&&p.pokerus==0x34);
    assert(strcmp(p.nickname,"PIKACHU")==0&&strcmp(p.ot_name,"WES")==0);
    assert(gen3_any_get_item_slot(&b,GEN3_POCKET_ITEMS,3,&it)&&it.item_id==19&&it.quantity==7);
    /* GameCube names are UTF-16, and go in from plain ASCII. */
    assert(gen3_any_name_is_utf16(&b));
    assert(gen3_any_trainer_name_length(&b)==10);
    assert(gen3_any_box_name_length(&b)==8);
    assert(gen3_any_set_trainer_name_ascii(&b,"RUI"));
    assert(strcmp(b.trainer_name,"RUI")==0);
    assert(be16(b.work+0x78)=='R'&&be16(b.work+0x7A)=='U'&&be16(b.work+0x7C)=='I');
    /* The rest of the field is cleared, not left holding the old name. */
    assert(be16(b.work+0x7E)==0);
    assert(gen3_any_set_box_name_ascii(&b,1,"TEAM"));
    char boxname[32];gen3_any_box_name(&b,1,boxname,sizeof(boxname));
    assert(strcmp(boxname,"TEAM")==0);
    assert(gen3_any_get_item_slot(&b,GEN3_POCKET_KEY_ITEMS,0,&it)&&it.item_id==261&&it.quantity==1);
    gen3_any_close(&b);
    /* Same writer must preserve a 64-byte GCI header byte-for-byte. */
    uint8_t *gci=calloc(1,GEN3_COLO_SIZE+GEN3_GCI_HEADER);assert(gci);memset(gci,0xA5,GEN3_GCI_HEADER);memcpy(gci+GEN3_GCI_HEADER,out,GEN3_COLO_SIZE);assert(gen3_any_open(&a,gci,GEN3_COLO_SIZE+GEN3_GCI_HEADER));uint8_t *gout=calloc(1,GEN3_COLO_SIZE+GEN3_GCI_HEADER);assert(gout);assert(gen3_any_export(&a,gout,GEN3_COLO_SIZE+GEN3_GCI_HEADER));assert(memcmp(gout,gci,GEN3_GCI_HEADER)==0);gen3_any_close(&a);free(gout);free(gci);free(out);free(raw);
}

/* Independent XD checksum model, written from PKHeX's XDCrypto rather than
 * from the port, so a matching bug in both would still show up here. */
static void xd_body_sums(const uint8_t *s,uint16_t out[8]){
    uint32_t part[4];size_t at=8;
    for(unsigned i=0;i<4;i++){uint32_t v=0;size_t end=at+0x9FF4u;for(size_t j=at;j<end;j+=2)v+=be16(s+j);at=end;part[i]=v;}
    uint16_t chk[8];
    for(unsigned i=0;i<4;i++){chk[i*2]=(uint16_t)(part[i]>>16);chk[i*2+1]=(uint16_t)part[i];}
    for(unsigned i=0;i<8;i++)out[i]=chk[7-i];
}
static void xd_set_checksums(uint8_t *s,uint32_t sub0){
    uint32_t h=0;for(unsigned i=0;i<8;i++)h+=s[i];
    p32be(s+0xA8u+sub0+0x38u,h);
    memset(s+0x10,0,0x10);
    uint16_t body[8];xd_body_sums(s,body);
    for(unsigned i=0;i<8;i++)p16be(s+0x10+2*i,body[i]);
}
static int xd_checksums_ok(const uint8_t *s,uint32_t sub0){
    uint32_t h=0;for(unsigned i=0;i<8;i++)h+=s[i];
    if(be32(s+0xA8u+sub0+0x38u)!=h)return 0;
    uint8_t *copy=malloc(0x28000);assert(copy);memcpy(copy,s,0x28000);memset(copy+0x10,0,0x10);
    uint16_t body[8];xd_body_sums(copy,body);free(copy);
    for(unsigned i=0;i<8;i++)if(be16(s+0x10+2*i)!=body[i])return 0;
    return 1;
}
static void xd_decrypt_test(uint8_t *slot,size_t len){
    uint16_t k[4];for(unsigned i=0;i<4;i++)k[i]=be16(slot+8+2*i);
    unsigned word=0;for(size_t off=0x10;off+1<len-0x28;off+=2,word++){
        uint16_t v=be16(slot+off);p16be(slot+off,(uint16_t)(v-k[word&3]));if((word&3)==3)adv(k);
    }
}

#define XD_SUB0 0x0800u
#define XD_SUB1 0x1000u
#define XD_SUB2 0x3000u
/* Past the eight box grids, which run to about 0xE908. */
#define XD_SUB5 0x8000u
#define XD_SUB7 0x10000u
#define XD_SHADOW_ENTRY 72u

static void test_xd(void){
    uint8_t *raw=calloc(1,GEN3_XD_SIZE);assert(raw);uint8_t*s=raw+0x6000;s[0]=1;s[1]=1;p32be(s+4,9);
    p16be(s+8,0x1111);p16be(s+10,0x2222);p16be(s+12,0x3333);p16be(s+14,0x4444);
    uint32_t off0=XD_SUB0,off1=XD_SUB1,off2=XD_SUB2,off5=XD_SUB5,off7=XD_SUB7;
    p16be(s+0x40,(uint16_t)off0);p16be(s+0x42,(uint16_t)(off0>>16));
    p16be(s+0x44,(uint16_t)off1);p16be(s+0x46,(uint16_t)(off1>>16));
    p16be(s+0x48,(uint16_t)off2);p16be(s+0x4A,(uint16_t)(off2>>16));
    p16be(s+0x40+5*4,(uint16_t)off5);p16be(s+0x42+5*4,(uint16_t)(off5>>16));
    p16be(s+0x40+7*4,(uint16_t)off7);p16be(s+0x42+7*4,(uint16_t)(off7>>16));
    /* Substructure 7 is the shadow table; its length also says whether the
     * save is Japanese, and 0x1E00 would mean it is. */
    p16be(s+0x20+7*2,(uint16_t)(XD_SHADOW_ENTRY*16u));
    /* Substructure 5 is the Strategy Memo: a count, two bytes PKHeX does
     * not name, then twelve bytes an entry. */
    p16be(s+0x20+5*2,(uint16_t)(4u+3u*GEN3_MEMO_ENTRY_SIZE));
    {
        uint8_t *memo=s+off5+0xA8;
        p16be(memo,3);
        uint8_t *e0=memo+4,*e1=e0+GEN3_MEMO_ENTRY_SIZE,*e2=e1+GEN3_MEMO_ENTRY_SIZE;
        p16be(e0,25);p16be(e0+4,444);p16be(e0+6,333);p32be(e0+8,0xAABBCCDDu);
        p16be(e1,277);e1[0]|=0x80;                    /* XD: flag set means not seen */
        p16be(e2,410);p32be(e2+8,0x11223344u);
    }
    size_t cfg=off0+0xA8,tr=off1+0xA8,bx=off2+0xA8;
    gc_text(s+tr,20,"MICHAEL");p16be(s+tr+0x2C,444);p16be(s+tr+0x2E,333);
    s[tr+0x8E0]=0;p32be(s+tr+0x8E4,4242);be_double(s+cfg+0x30,1234.0);

    /* A party record with every field this port reads set to a distinct value,
     * so a wrong offset shows up as a wrong value rather than a zero. */
    uint8_t*pk=s+tr+0x30;
    p16be(pk,25);p16be(pk+2,19);p16be(pk+6,200);p16be(pk+8,88);
    pk[0x0E]=17;pk[0x0F]=4;pk[0x10]=1;pk[0x11]=20;pk[0x12]=60;
    pk[0x13]=3;pk[0x14]=0x0B;pk[0x15]=4;
    pk[0x1D]=0x40;                                     /* second ability, valid */
    p32be(pk+0x20,15625);p32be(pk+0x24,0x01BC014D);p32be(pk+0x28,0xAABBCCDD);
    pk[0x33]=1;pk[0x37]=2;
    gc_text(pk+0x38,22,"MICHAEL");gc_text(pk+0x64,22,"PIKACHU");
    p16be(pk+0x7C,(uint16_t)(1u<<15));                 /* champion ribbon */
    for(unsigned i=0;i<4;i++){p16be(pk+0x80+i*4,(uint16_t)(85+i));pk[0x82+i*4]=(uint8_t)(15-i);pk[0x83+i*4]=(uint8_t)(i&3);}
    for(unsigned i=0;i<6;i++){p16be(pk+0x9C+i*2,(uint16_t)(10*(i+1)));pk[0xA8+i]=(uint8_t)(31-i*2);}
    for(unsigned i=0;i<5;i++)pk[0xAE + i]=(uint8_t)(7*(i+1));
    for(unsigned i=0;i<5;i++)pk[0xB3+i]=(uint8_t)(i%5);
    p16be(pk+0xBA,11);

    /* Shadow entry 11, which the party record above points at. */
    uint8_t *sh=s+off7+0xA8+11u*XD_SHADOW_ENTRY;
    sh[0]=0x40;                                   /* snagged, not purified */
    p32be(sh+0x04,(uint32_t)(4321u<<12));
    for(unsigned i=0;i<6;i++)sh[0x0B+i]=(uint8_t)(30-i);
    p16be(sh+0x1A,25);p32be(sh+0x1C,0xAABBCCDDu);p32be(sh+0x24,(uint32_t)(-1500));
    sh[0x3F]=11;
    gc_text(s+bx,16,"BOX1");uint8_t*bpk=s+bx+0x14;p16be(bpk,277);bpk[0x11]=10;gc_text(bpk+0x64,22,"TREECKO");
    /* Bag: XD keeps it 0x4C8 past the trainer block, with a discs pouch that
     * Colosseum does not have. */
    size_t bag=tr+0x4C8;
    p16be(s+bag+0x000,13);p16be(s+bag+0x002,500);
    p16be(s+bag+0x328,338);p16be(s+bag+0x32A,1);
    xd_set_checksums(s,XD_SUB0);assert(xd_checksums_ok(s,XD_SUB0));
    xd_encrypt(s,0x28000);

    Gen3AnySave a;assert(gen3_any_open(&a,raw,GEN3_XD_SIZE));
    assert(a.kind==GEN3_KIND_XD);
    assert(a.integrity_ok);
    assert(gen3_any_can_edit(&a));
    assert(a.tid==333&&a.sid==444&&a.money==4242);
    assert(strcmp(a.trainer_name,"MICHAEL")==0);
    assert(a.party_count==1&&a.box_count==8);

    /* The Strategy Memo. XD's flag means "not seen", so entry 1 is the hidden
     * one; Colosseum uses the same byte for "owned" instead, which is why the
     * two games are asked separately rather than reading the flag directly. */
    assert(gen3_any_has_memo(&a) && gen3_any_memo_count(&a)==3);
    {
        Gen3MemoEntry e;
        assert(gen3_any_memo_entry(&a,0,&e));
        assert(e.species_internal==25 && e.species==gen3_species_national(25));
        assert(e.tid==333 && e.sid==444 && e.pid==0xAABBCCDDu);
        assert(e.seen && !e.owned);                 /* XD never reports owned */
        assert(gen3_any_memo_entry(&a,1,&e) && !e.seen);
        assert(gen3_any_memo_entry(&a,2,&e) && e.seen && e.pid==0x11223344u);
        assert(!gen3_any_memo_entry(&a,3,&e));

        assert(gen3_any_set_memo_seen(&a,1,true));
        assert(gen3_any_memo_entry(&a,1,&e) && e.seen);
        assert(gen3_any_set_memo_seen(&a,1,false));
        assert(gen3_any_memo_entry(&a,1,&e) && !e.seen);
    }

    Gen3Pokemon p;assert(gen3_any_party_pokemon(&a,0,&p));
    assert(p.species_internal==25&&p.level==20&&p.held_item==19);
    assert(p.friendship==200&&p.met_location==88&&p.met_level==17&&p.ball==4&&p.ot_gender==1);
    assert(p.experience==15625&&p.pid==0xAABBCCDDu&&p.sid==0x01BC&&p.tid==0x014D);
    assert(p.ability_bit&&!p.is_egg&&p.checksum_ok&&p.fateful&&p.language==2);
    /* The stored byte has square and triangle the other way round, so 0x0B
     * on disk is 0x0D in the order the editor shows. */
    assert(p.markings==0x0D);
    /* XD splits the strain and days across two bytes; the port packs them the
     * way the GBA record does. */
    assert(p.pokerus==0x34);
    assert(strcmp(p.nickname,"PIKACHU")==0&&strcmp(p.ot_name,"MICHAEL")==0);
    for(unsigned i=0;i<4;i++){assert(p.moves[i]==85+i);assert(p.pp[i]==15-i);}
    assert(p.pp_ups==0xE4);
    /* XD stores SpA and SpD before Speed; the UI order puts Speed third. */
    assert(p.evs[0]==10&&p.evs[1]==20&&p.evs[2]==30&&p.evs[3]==60&&p.evs[4]==40&&p.evs[5]==50);
    assert(p.ivs[0]==31&&p.ivs[1]==29&&p.ivs[2]==27&&p.ivs[3]==21&&p.ivs[4]==25&&p.ivs[5]==23);
    for(unsigned i=0;i<5;i++)assert(p.contest[i]==7*(i+1));
    assert(p.contest[5]==60);
    for(unsigned i=0;i<5;i++)assert(gen3_contest_ribbon(&p,i)==i%5);
    /* XD's ribbon halfword runs the other way up from the GBA's. */
    assert(gen3_ribbon_flag(&p,0)&&!gen3_ribbon_flag(&p,1));
    assert(p.is_shadow&&p.shadow_id==11);
    /* An XD record only names its Shadow ID; the state comes from the table. */
    assert(p.purification==-1500);
    assert(gen3_any_has_shadow_table(&a));
    assert(gen3_any_shadow_count(&a)==16);
    Gen3ShadowEntry sh_read;
    assert(gen3_any_shadow_entry(&a,11,&sh_read));
    assert(sh_read.present&&sh_read.snagged&&!sh_read.purified);
    assert(sh_read.species==25&&sh_read.pid==0xAABBCCDDu);
    assert(sh_read.purification==-1500&&sh_read.experience==4321&&sh_read.index==11);
    /* The table stores SpA and SpD before Speed, like the record. */
    assert(sh_read.ivs[0]==30&&sh_read.ivs[1]==29&&sh_read.ivs[2]==28);
    assert(sh_read.ivs[3]==25&&sh_read.ivs[4]==27&&sh_read.ivs[5]==26);
    assert(gen3_any_shadow_entry(&a,0,&sh_read)&&!sh_read.present);
    assert(!gen3_any_shadow_entry(&a,16,&sh_read));
    /* Purifying the entry makes the record stop reading as a Shadow. */
    assert(gen3_any_set_shadow_purified(&a,11,true));
    assert(gen3_any_set_shadow_purification(&a,11,0));
    assert(gen3_any_party_pokemon(&a,0,&p)&&!p.is_shadow&&p.purification==0);
    assert(gen3_any_set_shadow_purified(&a,11,false));
    assert(gen3_any_set_shadow_purification(&a,11,-1500));
    assert(gen3_any_party_pokemon(&a,0,&p)&&p.is_shadow);
    assert(gen3_any_box_pokemon(&a,0,0,&p)&&p.species_internal==277&&p.level==10);

    /* XD's bag has one pouch Colosseum lacks, and larger stacks. */
    Gen3ItemSlot it;
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_ITEMS)==30);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_DISCS)==60);
    assert(gen3_any_pocket_capacity(&a,GEN3_POCKET_PC)==0);
    assert(gen3_any_pocket_max_quantity(&a,GEN3_POCKET_ITEMS)==999);
    assert(gen3_any_pocket_max_quantity(&a,GEN3_POCKET_DISCS)==1);
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_ITEMS,0,&it)&&it.item_id==13&&it.quantity==500);
    assert(gen3_any_get_item_slot(&a,GEN3_POCKET_DISCS,0,&it)&&it.item_id==338&&it.quantity==1);
    assert(gen3_any_set_item_slot(&a,GEN3_POCKET_BERRIES,5,133,999));
    assert(gen3_any_set_item_slot(&a,GEN3_POCKET_DISCS,1,339,99));

    /* Edit trainer data and a party record, export, and read it all back. */
    assert(gen3_any_set_tid(&a,54321));assert(gen3_any_set_sid(&a,12345));
    assert(gen3_any_set_money(&a,999999));assert(gen3_any_set_trainer_gender(&a,1));
    assert(gen3_any_party_pokemon(&a,0,&p));
    p.held_item=182;p.moves[0]=94;p.pp[0]=20;p.ivs[3]=7;p.evs[3]=77;
    p.friendship=155;p.contest[2]=99;p.is_egg=true;
    assert(gen3_set_contest_ribbon(&p,3,4));
    assert(gen3_set_ribbon_flag(&p,11,true));
    assert(gen3_any_set_party_pokemon(&a,0,&p));

    uint8_t *out=calloc(1,GEN3_XD_SIZE);assert(out);
    assert(gen3_any_export(&a,out,GEN3_XD_SIZE));
    gen3_any_close(&a);

    /* The exported slot has to satisfy the independent checksum model. */
    uint8_t *check=malloc(0x28000);assert(check);memcpy(check,out+0x6000,0x28000);
    xd_decrypt_test(check,0x28000);
    assert(xd_checksums_ok(check,XD_SUB0));
    free(check);

    Gen3AnySave b;assert(gen3_any_open(&b,out,GEN3_XD_SIZE));
    assert(b.kind==GEN3_KIND_XD&&b.integrity_ok);
    assert(b.tid==54321&&b.sid==12345&&b.money==999999&&b.trainer_gender==1);
    assert(gen3_any_party_pokemon(&b,0,&p));
    assert(p.held_item==182&&p.moves[0]==94&&p.pp[0]==20);
    assert(p.ivs[3]==7&&p.evs[3]==77&&p.friendship==155&&p.contest[2]==99&&p.is_egg);
    assert(gen3_contest_ribbon(&p,3)==4);
    assert(gen3_ribbon_flag(&p,11)&&gen3_ribbon_flag(&p,0));
    assert(gen3_any_get_item_slot(&b,GEN3_POCKET_BERRIES,5,&it)&&it.item_id==133&&it.quantity==999);
    /* The discs pouch holds one of each, whatever quantity it is handed. */
    assert(gen3_any_get_item_slot(&b,GEN3_POCKET_DISCS,1,&it)&&it.item_id==339&&it.quantity==1);
    /* Fields the editor never touches survive the write. */
    assert(p.shadow_id==11&&strcmp(p.nickname,"PIKACHU")==0&&strcmp(p.ot_name,"MICHAEL")==0);
    assert(p.met_location==88&&p.met_level==17&&p.ball==4&&p.language==2&&p.fateful);
    gen3_any_close(&b);

    /* A .gci header must come through byte for byte. */
    uint8_t *gci=calloc(1,GEN3_XD_SIZE+GEN3_GCI_HEADER);assert(gci);
    memset(gci,0x5A,GEN3_GCI_HEADER);memcpy(gci+GEN3_GCI_HEADER,out,GEN3_XD_SIZE);
    assert(gen3_any_open(&a,gci,GEN3_XD_SIZE+GEN3_GCI_HEADER));
    uint8_t *gout=calloc(1,GEN3_XD_SIZE+GEN3_GCI_HEADER);assert(gout);
    assert(gen3_any_export(&a,gout,GEN3_XD_SIZE+GEN3_GCI_HEADER));
    assert(memcmp(gout,gci,GEN3_GCI_HEADER)==0);
    gen3_any_close(&a);free(gout);free(gci);free(out);free(raw);
}

/* Reverse of box_payload_write, for reading a value back out of the blocks. */
static void box_payload_read(const uint8_t *raw,unsigned half,size_t off,void*dst,size_t n){
    while(n){unsigned id=(unsigned)(off/0x1FF0u);size_t in=off%0x1FF0u;size_t take=0x1FF0u-in;if(take>n)take=n;
        const uint8_t*b=raw+0x2000u+(size_t)half*23u*0x2000u+(size_t)id*0x2000u;
        memcpy(dst,b+0xC+in,take);off+=take;dst=(uint8_t*)dst+take;n-=take;
    }
}
static int box_blocks_valid(const uint8_t*raw,unsigned half){
    for(unsigned i=0;i<23;i++){
        const uint8_t*b=raw+0x2000u+(size_t)half*23u*0x2000u+(size_t)i*0x2000u;
        uint16_t s2=boxsum(b);
        if(be16(b)!=s2||be16(b+2)!=(uint16_t)(0xF004u-s2))return 0;
    }
    return 1;
}

/* Pokemon Box stores two PKHeX boxes per 12x5 grid, so slot n of an odd box
 * sits six columns to the right of slot n of the even box before it. */
static size_t box_slot_offset(unsigned box,unsigned slot){
    unsigned row=slot/6,col=slot%6;
    if(box&1)col+=6;
    return 8u+(size_t)84u*(box&~1u)*30u+(size_t)(row*12u+col)*84u;
}

/* A minimal stored PK3, encrypted the way the record layer expects. */
static void make_pk3_stored(uint8_t *dst,uint16_t species,uint32_t pid,uint32_t otid,uint32_t exp){
    memset(dst,0,80);
    dst[0]=(uint8_t)pid;dst[1]=(uint8_t)(pid>>8);dst[2]=(uint8_t)(pid>>16);dst[3]=(uint8_t)(pid>>24);
    dst[4]=(uint8_t)otid;dst[5]=(uint8_t)(otid>>8);dst[6]=(uint8_t)(otid>>16);dst[7]=(uint8_t)(otid>>24);
    memset(dst+0x08,0xFF,10);memset(dst+0x14,0xFF,7);
    dst[0x13]=2; /* Has Species */
    uint8_t plain[48];memset(plain,0,sizeof(plain));
    plain[0]=(uint8_t)species;plain[1]=(uint8_t)(species>>8);
    plain[4]=(uint8_t)exp;plain[5]=(uint8_t)(exp>>8);plain[6]=(uint8_t)(exp>>16);plain[7]=(uint8_t)(exp>>24);
    uint32_t sum=0;for(unsigned i=0;i<48;i+=2)sum+=(uint32_t)(plain[i]|(plain[i+1]<<8));
    dst[0x1C]=(uint8_t)sum;dst[0x1D]=(uint8_t)(sum>>8);
    /* PID % 24 == 0 keeps the substructures in GAEM order. */
    const uint32_t key=pid^otid;
    for(unsigned i=0;i<48;i+=4){
        uint32_t w=(uint32_t)plain[i]|((uint32_t)plain[i+1]<<8)|((uint32_t)plain[i+2]<<16)|((uint32_t)plain[i+3]<<24);
        w^=key;
        dst[0x20+i]=(uint8_t)w;dst[0x21+i]=(uint8_t)(w>>8);dst[0x22+i]=(uint8_t)(w>>16);dst[0x23+i]=(uint8_t)(w>>24);
    }
}

static void test_box_and_gci(void){
    uint8_t *raw=calloc(1,GEN3_RSBOX_SIZE);assert(raw);
    for(unsigned h=0;h<2;h++)for(unsigned i=0;i<23;i++){uint8_t*b=raw+0x2000u+(size_t)h*23u*0x2000u+(size_t)i*0x2000u;p32be(b+4,i);p32be(b+8,h+1);}
    uint8_t cur=2;box_payload_write(raw,1,4,&cur,1);
    /* Names use GBA text; FF terminator and encoded A-Z values. */
    uint8_t nm[9];memset(nm,0xFF,9);nm[0]=0xBC;nm[1]=0xC9;nm[2]=0xD2; /* BOX */ box_payload_write(raw,1,0x1EC38,nm,9);
    uint8_t wall=3;box_payload_write(raw,1,0x1ED19,&wall,1);
    /* One record in box 0 slot 0 and one in box 1 slot 0: the same grid row,
     * six columns apart, which is what makes the swizzle worth testing. */
    uint8_t rec[80];
    make_pk3_stored(rec,25,0x00000018u,0x12345678u,15625u);   /* Pikachu */
    box_payload_write(raw,1,box_slot_offset(0,0),rec,80);
    make_pk3_stored(rec,277,0x00000018u,0x12345678u,8000u);   /* Treecko */
    box_payload_write(raw,1,box_slot_offset(1,0),rec,80);
    for(unsigned h=0;h<2;h++)for(unsigned i=0;i<23;i++)finalize_box_block(raw+0x2000u+(size_t)h*23u*0x2000u+(size_t)i*0x2000u);

    Gen3AnySave a;assert(gen3_any_open(&a,raw,GEN3_RSBOX_SIZE));
    assert(a.kind==GEN3_KIND_RSBOX&&a.box_count==50&&a.current_box==4&&a.active_slot==1&&a.integrity_ok);
    assert(gen3_any_can_edit(&a));
    assert(gen3_any_box_wallpaper(&a,0)==3&&gen3_any_box_wallpaper(&a,1)==3);
    Gen3Pokemon p;
    assert(gen3_any_box_pokemon(&a,0,0,&p)&&p.present&&p.checksum_ok);
    assert(p.species_internal==25&&p.experience==15625);
    assert(gen3_any_box_pokemon(&a,1,0,&p)&&p.present&&p.species_internal==277);
    /* The other slots of that grid row are empty, so the swizzle is not just
     * reading the same record twice. */
    assert(gen3_any_box_pokemon(&a,0,1,&p)&&!p.present);

    /* Edit a record and a wallpaper, export, and read it all back. */
    assert(gen3_any_box_pokemon(&a,1,0,&p));
    p.experience=125000;p.friendship=222;p.ivs[0]=31;p.evs[2]=44;p.moves[0]=94;
    assert(gen3_any_set_box_pokemon(&a,1,0,&p));
    assert(gen3_any_set_box_wallpaper(&a,0,9));

    uint8_t *out=calloc(1,GEN3_RSBOX_SIZE);assert(out);
    assert(gen3_any_export(&a,out,GEN3_RSBOX_SIZE));
    gen3_any_close(&a);

    /* Every block in the written half has to re-checksum, and that half has
     * to be the newer one so the reader and the game both pick it. */
    assert(box_blocks_valid(out,1));
    uint32_t c0=be32(out+0x2000u+8),c1=be32(out+0x2000u+23u*0x2000u+8);
    assert(c1>c0);
    /* The record really landed in the odd box's half of the grid, not in the
     * even box's slot 0. */
    uint8_t before[80],after[80];
    box_payload_read(raw,1,box_slot_offset(0,0),before,80);
    box_payload_read(out,1,box_slot_offset(0,0),after,80);
    assert(memcmp(before,after,80)==0);
    box_payload_read(raw,1,box_slot_offset(1,0),before,80);
    box_payload_read(out,1,box_slot_offset(1,0),after,80);
    assert(memcmp(before,after,80)!=0);

    Gen3AnySave b;assert(gen3_any_open(&b,out,GEN3_RSBOX_SIZE));
    assert(b.kind==GEN3_KIND_RSBOX&&b.integrity_ok&&b.active_slot==1);
    assert(gen3_any_box_pokemon(&b,1,0,&p));
    assert(p.present&&p.checksum_ok&&p.species_internal==277);
    assert(p.experience==125000&&p.friendship==222&&p.ivs[0]==31&&p.evs[2]==44&&p.moves[0]==94);
    /* A box record comes out and goes back in byte for byte, and a file of
     * the right length but the wrong contents is refused rather than written. */
    assert(gen3_any_record_size(&b)==80);
    assert(strcmp(gen3_any_record_extension(&b),"pk3")==0);
    uint8_t saved[80],reloaded[80];
    assert(gen3_any_box_record_raw(&b,1,0,saved,sizeof(saved)));
    assert(!gen3_any_box_record_raw(&b,1,0,saved,79));
    assert(gen3_any_set_box_record_raw(&b,4,7,saved,sizeof(saved)));
    assert(gen3_any_box_record_raw(&b,4,7,reloaded,sizeof(reloaded)));
    assert(memcmp(saved,reloaded,80)==0);
    assert(gen3_any_box_pokemon(&b,4,7,&p)&&p.present&&p.species_internal==277);
    /* Wrong length, and right length but not a record. */
    assert(!gen3_any_set_box_record_raw(&b,4,8,saved,79));
    uint8_t junk[80];memset(junk,0xA5,sizeof(junk));
    assert(!gen3_any_set_box_record_raw(&b,4,8,junk,sizeof(junk)));
    assert(gen3_any_box_pokemon(&b,4,8,&p)&&!p.present);

    assert(gen3_any_box_wallpaper(&b,0)==9);
    /* Pokemon Box keeps Generation III text, and two boxes share one name. */
    assert(!gen3_any_name_is_utf16(&b));
    assert(gen3_any_box_name_length(&b)==8);
    assert(gen3_any_set_box_name_ascii(&b,2,"LEGENDS"));
    char n0[32],n1[32];
    gen3_any_box_name(&b,2,n0,sizeof(n0));
    gen3_any_box_name(&b,3,n1,sizeof(n1));
    assert(strstr(n0,"LEGENDS")&&strstr(n1,"LEGENDS"));
    /* Box 0 was not touched. */
    assert(gen3_any_box_pokemon(&b,0,0,&p)&&p.present&&p.experience==15625);
    gen3_any_close(&b);

    uint8_t *gci=calloc(1,GEN3_RSBOX_SIZE+0x40);assert(gci);memset(gci,0x3C,0x40);memcpy(gci+0x40,out,GEN3_RSBOX_SIZE);
    assert(gen3_any_open(&a,gci,GEN3_RSBOX_SIZE+0x40));assert(a.kind==GEN3_KIND_RSBOX&&a.data_offset==0x40);
    uint8_t *gout=calloc(1,GEN3_RSBOX_SIZE+0x40);assert(gout);
    assert(gen3_any_export(&a,gout,GEN3_RSBOX_SIZE+0x40));
    assert(memcmp(gout,gci,0x40)==0);
    gen3_any_close(&a);free(gout);free(gci);free(out);free(raw);
}

int main(void){test_sha1_vector();test_colosseum();test_xd();test_box_and_gci();puts("all-gen3 GameCube parser tests: PASS");return 0;}
