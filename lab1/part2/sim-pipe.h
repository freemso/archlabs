#include "machine.h"


/*define buffer between fetch and decode stage*/
struct ifid_buf {
  md_inst_t inst;	    /* instruction that has been fetched */
  md_addr_t valP;	    /* pc value of the instruction */
};


/*define buffer between decode and execute stage*/
struct idex_buf {
  md_inst_t inst;		/* instruction in ID stage */
  
  byte_t rs;
  byte_t rt;
  
  sword_t valS;
  sword_t valT;
  
  md_addr_t valP;
};

/*define buffer between execute and memory stage*/
struct exmem_buf{
  md_inst_t inst;		/* instruction in EX stage */
  bool_t isBranch;
  bool_t taken;
  bool_t writeRT;
  bool_t writeRD;
  bool_t memLoad;

  byte_t rd;
  byte_t rt;
  
  sword_t valE;
  sword_t dataIn;

  md_addr_t addr;
  
  md_addr_t valP;
};

/*define buffer between memory and writeback stage*/
struct memwb_buf{
  md_inst_t inst;		/* instruction in MEM stage */
  
  sword_t valE;
  sword_t valM;

  byte_t rd;
  byte_t rt;

  bool_t writeRT;
  bool_t writeRD;
  bool_t memLoad;

  md_addr_t valP;
};

struct wb_buf{
  md_inst_t inst;
  md_addr_t valP;
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
