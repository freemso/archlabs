#include "machine.h"


/*define buffer between fetch and decode stage*/
struct ifid_buf {
  md_inst_t inst;	    /* instruction that has been fetched */
  md_addr_t valP;	    /* pc value of the instruction */
};


/*define buffer between decode and execute stage*/
struct idex_buf {
  md_inst_t inst;		/* instruction in ID stage */

  int opcode;
  
  byte_t rs;
  byte_t rt;
  byte_t rd;
  
  sword_t valS;
  sword_t valT;
  sword_t imm;
  sword_t uimm;
  sword_t shamt;
  
  md_addr_t valP;
};

/*define buffer between execute and memory stage*/
struct exmem_buf {
  md_inst_t inst;
  int opcode;
  md_addr_t valP;

  bool_t isBranch;
  bool_t taken;
  md_addr_t addr;
  
  bool_t regWrite;
  bool_t memWrite;
  bool_t memRead;

  byte_t dst;
  
  sword_t valE;
  sword_t valA;
};

/*define buffer between memory and writeback stage*/
struct memwb_buf {
  md_inst_t inst;
  int opcode;
  md_addr_t valP;

  sword_t data;

  byte_t dst;

  bool_t regWrite;
};

struct wb_buf {
  md_inst_t inst;
  int opcode;
  md_addr_t valP;

  sword_t data;

  byte_t dst;
};

/* print trace*/
void print_to_trace();
  
/*do fetch stage*/
void do_if();

/*do decode stage*/
void do_id();

/*do execute stage*/
void do_ex();

/*do memory stage*/
void do_mem();

/*do write_back to register*/
void do_wb();

/*check stall*/
void check_stall();

/*do forward*/
void forward();


#define MD_FETCH_INSTI(INST, MEM, PC)					\
  { INST.a = MEM_READ_WORD(mem, (PC));					\
    INST.b = MEM_READ_WORD(mem, (PC) + sizeof(word_t)); }

#define SET_OPCODE(OP, INST) ((OP) = ((INST).a & 0xff)) 

#define RSI(INST)		(INST.b >> 24& 0xff)		/* reg source #1 */
#define RTI(INST)		((INST.b >> 16) & 0xff)		/* reg source #2 */
#define RDI(INST)		((INST.b >> 8) & 0xff)		/* reg dest */

#define IMMI(INST)	((int)((/* signed */short)(INST.b & 0xffff)))	/*get immediate value*/
#define TARGI(INST)	(INST.b & 0x3ffffff)		/*jump target*/

/*  LAB 2  */

/* block status values */
#define CACHE_BLK_VALID		0x00000001	/* block in valid, in use */
#define CACHE_BLK_DIRTY		0x00000002	/* dirty block */

/* cache block (or line) definition */
struct cache_blk_t
{
  int last_access_time;
  md_addr_t tag;		/* data block tag value */
  unsigned int status;		/* block status, see CACHE_BLK_* defs above */
  byte_t data[16];		/* actual data block starts here, block size should probably be a multiple of 8 */
};

/* cache set definition (one or more blocks sharing the same set index) */
struct cache_set_t
{
  struct cache_blk_t *blks[4];
};

/* cache definition */
struct cache_t
{
  int enabled;
  /* parameters */
  int nsets;			/* number of sets */
  int bsize;			/* block size in bytes */
  int assoc;			/* cache associativity */
  unsigned int hit_latency;	/* cache hit latency */
  unsigned int miss_latency; /* cache miss latency */

  /* per-cache stats */
  int hits;		/* total number of hits */
  int misses;		/* total number of misses */
  int replacements;	/* total number of replacements at misses */
  int writebacks;		/* total number of writebacks at misses */
  int mem_accesses; /* total number of memory accesses */

  struct cache_set_t *sets[16];	/* each entry is a set */
};

/* create and initialize a general cache structure */
struct cache_t *			/* pointer to cache created */
cache_create(
      unsigned int hit_latency, /* latency in cycles for a hit */
      unsigned int miss_latency, /* latency in cycles for a miss */
      int enabled);

unsigned int /* latency of this read */
access_cache_word(struct cache_t *cp, enum mem_cmd cmd, md_addr_t addr, sword_t *data);

/* print cache stats */
void print_cache_stats(struct cache_t *cp);
