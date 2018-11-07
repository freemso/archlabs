#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* An implementation of 5-stage classic pipeline simulation */

/* don't count instructions flag, enabled by default, disable for inst count */
#undef NO_INSN_COUNT

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "sim.h"
#include "sim-pipe.h"

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* Lab2 simulated cache*/
static struct cache_t *cp;

/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb, 
"sim-pipe: This simulator implements based on sim-fast.\n"
		 );
}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
  if (dlite_active)
    fatal("sim-pipe does not support DLite debugging");
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)
{
#ifndef NO_INSN_COUNT
  stat_reg_counter(sdb, "sim_num_insn",
		   "total number of instructions executed",
		   &sim_num_insn, sim_num_insn, NULL);
#endif /* !NO_INSN_COUNT */
  stat_reg_int(sdb, "sim_elapsed_time",
	       "total simulation time in seconds",
	       &sim_elapsed_time, 0, NULL);
#ifndef NO_INSN_COUNT
  stat_reg_formula(sdb, "sim_inst_rate",
		   "simulation speed (in insts/sec)",
		   "sim_num_insn / sim_elapsed_time", NULL);
#endif /* !NO_INSN_COUNT */
  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}

struct ifid_buf fd;
struct idex_buf de;
struct exmem_buf em;
struct memwb_buf mw;
struct wb_buf wb;
bool_t mis_pred;
md_addr_t correct_npc;

int cycle_num; /* Cycle Number counter, used in trace file*/

#define DNA			(-1)

/* general register dependence decoders */
#define DGPR(N)			(N)
#define DGPR_D(N)		((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)		(((N)+32)&~1)
#define DFPR_F(N)		(((N)+32)&~1)
#define DFPR_D(N)		(((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI			(0+32+32)
#define DLO			(1+32+32)
#define DFCC		(2+32+32)
#define DTMP		(3+32+32)

/* initialize the simulator */
void
sim_init(void)
{
  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);

  /* Lab2 create cache*/
  int enabled = 0;
  int hit_latency = 1;
  int miss_latency = 10;
  cp = cache_create(hit_latency, miss_latency, enabled);

  /* initialize cycle counter*/
  cycle_num = 0;

  mis_pred = 0;
  correct_npc = 0;

  /* initialize stage latches*/
 
  /* IF/ID */
  fd.inst.a = NOP;
  fd.inst.b = 0;
  fd.valP = 0;

  /* ID/EX */
  de.inst.a = NOP;
  de.inst.b = 0;
  de.opcode = NOP;
  de.valP = 0;
  de.rs = DNA;
  de.rt = DNA;
  de.rd = DNA;
  de.valS = 0;
  de.valT = 0;
  de.imm = 0;
  de.uimm = 0;
  de.shamt = 0;

  /* EX/MEM */
  em.inst.a = NOP;
  em.inst.b = 0;
  em.opcode = NOP;
  em.valP = 0;
  em.isBranch = 0;
  em.taken = 0;
  em.valE = 0;
  em.valA = 0;
  em.addr = 0;
  em.dst = DNA;
  em.memRead = 0;
  em.memWrite = 0;
  em.regWrite = 0;

  /* MEM/WB */
  mw.inst.a = NOP;
  mw.inst.b = 0;
  mw.opcode = NOP;
  mw.valP = 0;
  mw.data = 0;
  mw.dst = DNA;
  mw.regWrite = 0;

  // WB
  wb.inst.a = NOP;
  wb.inst.b = 0;
  wb.opcode = NOP;
  wb.valP = 0;
}

/* load program into simulated state */
void
sim_load_prog(char *fname,		/* program to load */
	      int argc, char **argv,	/* program arguments */
	      char **envp)		/* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)
{  
	/* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)
{  /* nada */}

/* un-initialize simulator-specific state */
void 
sim_uninit(void)
{ /* nada */ }


/*
 * configure the execution engine
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))
#define DECLARE_FAULT(EXP) 	{;}
#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)		(regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)	(regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)		(regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)	(regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)		(regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)	(regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)		(regs.regs_C.hi = (EXPR))
#define HI			(regs.regs_C.hi)
#define SET_LO(EXPR)		(regs.regs_C.lo = (EXPR))
#define LO			(regs.regs_C.lo)
#define FCC			(regs.regs_C.fcc)
#define SET_FCC(EXPR)		(regs.regs_C.fcc = (EXPR))

#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_BYTE(mem, (SRC)))
#define READ_HALF(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_HALF(mem, (SRC)))
#define READ_WORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_WORD(mem, (SRC)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_QWORD(mem, (SRC)))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_BYTE(mem, (DST), (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_HALF(mem, (DST), (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_WORD(mem, (DST), (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_QWORD(mem, (DST), (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#ifndef NO_INSN_COUNT
#define INC_INSN_CTR()	sim_num_insn++
#else /* !NO_INSN_COUNT */
#define INC_INSN_CTR()	/* nada */
#endif /* NO_INSN_COUNT */


/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{
  fprintf(stderr, "sim: ** starting *pipe* functional simulation **\n");

  /* must have natural byte/word ordering */
  if (sim_swap_bytes || sim_swap_words)
    fatal("sim: *pipe* functional simulation cannot swap bytes or words");

  /* set up initial default next PC */
  regs.regs_NPC = regs.regs_PC;
  /* maintain $r0 semantics */
  regs.regs_R[MD_REG_ZERO] = 0;
 
  while (TRUE)
  {
	  /*start your pipeline simulation here*/
    cycle_num++;
    /* the order to run the function for each stage should be reversed,
    because we don't want the change of data in each stage buffer instantly
    visible to the next stage. We need the data in current stage buffer to
    update the data in the next stage buffer. In real cpu, the change could only be refreshed
    into the buffer only when the clock refreshes.*/
    check_stall();
    forward();
    // print_to_trace();
    do_wb();
    do_mem();
    do_ex();
    do_id();
    do_if();
    // forward();
    // now print current state to trace file
    // print_to_trace();
    // printf("%d\n", cycle_num);
    // if (cycle_num > 100000000) {
    //   break;
    // }
  }
}

/* Only check the Load_Use Hazard*/
void check_stall() {
	if(em.opcode != NOP && (em.regWrite) && (em.memRead) && (em.dst == de.rs || em.dst == de.rt)) {
    // Stall a cycle
		fd.inst = de.inst;
		fd.valP = de.valP;
		de.inst.a = NOP;
    de.inst.b = 0;
    de.opcode = NOP;
    SET_NPC(CPC);
	}	
}

void forward() {
  // Read before write
  bool_t emHazard = (em.opcode != NOP) && (em.regWrite) && (!em.memRead) && (em.dst == de.rs || em.dst == de.rt);
  
  bool_t mwHazard = (mw.opcode != NOP) && (mw.regWrite) && (mw.dst == de.rs || mw.dst == de.rt);
  
  // mw hazard
  if(mwHazard) {
    if (mw.dst == de.rs) {
      // forward to RS
      de.valS = mw.data;
    } else if (mw.dst == de.rt) {
      // forward to RT
      de.valT = mw.data;
    }
  }

  // em hazard
  if(emHazard) {
    if (em.dst == de.rs) {
      // forward to RS
      de.valS = em.valE;
    } else if (em.dst == de.rt) {
      // forward to RT
      de.valT = em.valE;
    }
  }

  return;
}

void do_if()
{
  md_inst_t inst;

  // Select PC
  if (mis_pred) {
    // Mispredicted branch
    // Since our prediction strategy is to always NOT taken,
    // we only need to select the PC after the branch instruction,
    // which is stored in valP of em stage.
  	CPC = correct_npc;
  } else {
  	CPC = regs.regs_NPC;
  }
  
  // Fetch instruction from memory
  cycle_num += access_cache_word(cp, Read, CPC, &inst.a);
  cycle_num += access_cache_word(cp, Read, CPC + sizeof(word_t), &inst.b);

  // md_inst_t inst2;
  // MD_FETCH_INSTI(inst2, mem, CPC);
  // if (inst.a != inst2.a || inst.b != inst2.b) {
  //   printf("instruction read error! inst: %p, %p. should be %p, %p\n", inst.a, inst.b, inst2.a, inst2.b);
  // }

  // Predict next PC
  int opcode;
  MD_SET_OPCODE(opcode, inst);
  if (opcode == JUMP) {
    // direct jump
    SET_NPC((CPC & 0xF0000000) | (TARG << 2));
  } else {
    // always NOT taken
    // normal increment
    SET_NPC(CPC + sizeof(md_inst_t));
  }

  fd.inst = inst;
  fd.valP = CPC;
}

void do_id()
{
  if (mis_pred) {
    // Mispredicted branch
    // Insert a bubble here
  	de.inst.a = NOP;
    de.inst.b = 0;
    de.opcode = NOP;
    de.valP = fd.valP;
    return;
  }

  // Copy the buffer
  de.inst = fd.inst;
  de.valP = fd.valP;

  md_inst_t inst;
  inst = de.inst;

  de.opcode = MD_OPFIELD(inst);

  de.rs = RS;
  de.rt = RT;
  de.rd = RD;

  de.valS = GPR(RS);
  de.valT = GPR(RT);

  de.imm = IMM;
  de.uimm = UIMM;
  de.shamt = SHAMT;
}

void do_ex()
{
  // Check misprediction in the last cycle
  if (em.isBranch && em.taken) {
    // Mispredicted branch
    mis_pred = 1;
    correct_npc = em.addr;
    // Insert a bubble here
  	em.opcode = NOP;
    em.inst.a = NOP;
    em.inst.b = 0;
    em.valP = de.valP;
    em.isBranch = 0;
    em.taken = 0;
    return;
  }

  mis_pred = 0;

  // Copy the buffer
  em.inst = de.inst;
  em.valP = de.valP;
  em.opcode = de.opcode;

  em.isBranch = 0;
  em.taken = 0;
  em.regWrite = 0;
  em.memWrite = 0;
  em.memRead = 0;
  em.addr = 0;
  em.valE = 0;
  em.valA = 0;
  em.dst = 0;

  switch(em.opcode) {
    case ADD:
      em.valE = (int)de.valS + (int)de.valT;
      em.regWrite = 1;
      em.dst = de.rd;
		  break;
    case ADDU:
      em.valE = (unsigned)de.valS + (unsigned)de.valT;
      em.regWrite = 1;
      em.dst = de.rd;
		  break;
    case SUBU:
      em.valE = (unsigned)de.valS - (unsigned)de.valT;
      em.regWrite = 1;
      em.dst = de.rd;
		  break;
    case ADDIU:
      em.valE = (unsigned)de.valS + (int)de.imm;
      em.regWrite = 1;
      em.dst = de.rt;
		  break;
	  case ANDI:
      em.valE = de.valS & (unsigned)de.uimm;
      em.regWrite = 1;
      em.dst = de.rt;
		  break;
	  case BNE:
      em.isBranch = 1;
      em.taken = (bool_t)(de.valS != de.valT);
      em.addr = em.valP + 8 + (de.imm << 2);
		  break;
    case BEQ:
      em.isBranch = 1;
      em.taken = (bool_t)(de.valS == de.valT);
      em.addr = em.valP + 8 + (de.imm << 2);
      break;
	  case JUMP:
		  break;
	  case LUI:
      em.valE = de.uimm << 16;
      em.regWrite = 1;
      em.dst = de.rt;
		  break;
	  case LW:
      em.valE = de.valS + de.imm;
      em.regWrite = 1;
      em.dst = de.rt;
      em.memRead = 1;
		  break;
	  case SLL:
      em.valE = de.valT << de.shamt;
      em.regWrite = 1;
      em.dst = de.rd;
		  break;
	  case SW:
      em.valE = de.valS + de.imm;
      em.valA = de.valT;
      em.memWrite = 1;
		  break;
	  case SLTI:
      em.valE = de.valS < de.imm;
      em.regWrite = 1;
      em.dst = de.rt;
	    break;
    case NOP:
      break;
    case SYSCALL:
      break;
    case MULTU: {
      // printf("%d * %d\n", de.valS, de.valT);
      int _i;
      /* HI,LO <- [rs] * [rt], integer product of [rs] & [rt] to HI/LO */
      HI = 0;
      LO = 0;
      if (de.valS & 020000000000)
        LO = de.valT;
      for (_i = 0; _i < 31; _i++) {
	      HI = HI << 1;
	      HI = HI + extractl(LO, 31, 1);
	      LO = LO << 1;
	      if ((extractl(de.valS, 30 - _i, 1)) == 1) {
	        if (((unsigned)037777777777 - (unsigned)LO) < (unsigned)de.valT) {
		        HI = HI + 1;
	        }
	        LO = LO + de.valT;
	      }
      }
      break;
    }
    case MFLO:
      em.valE = LO;
      em.regWrite = 1;
      em.dst = de.rd;
      break;
    case ADDI:
      em.valE = (int)de.valS + de.imm;
      em.regWrite = 1;
      em.dst = de.rt;
      break;
    // default:
      // printf("%s not implemented!\n", MD_OP_NAME(em.opcode));
  }
  	
}

void do_mem()
{
  // Copy the buffer
  mw.inst = em.inst;
  mw.valP = em.valP;
  mw.opcode = em.opcode;
  mw.regWrite = em.regWrite;
  mw.data = em.valE;
  mw.dst = em.dst;

  if (em.memRead) {
    cycle_num += access_cache_word(cp, Read, em.valE, &mw.data);
    // mw.data = MEM_READ_WORD(mem, em.valE);
  }

  if (em.memWrite) {
    cycle_num += access_cache_word(cp, Write, em.valE, &em.valA);
    // MEM_WRITE_WORD(mem, em.valE, em.valA);
  }
}                                                                                        

void do_wb()
{
  wb.inst = mw.inst;
  wb.valP = mw.valP;
  wb.opcode = mw.opcode;
  wb.dst = mw.dst;
  wb.data = mw.data;

  if(wb.opcode == NOP) {
    return;
  }

  if(wb.opcode == SYSCALL) {
		// printf("Loop terminated. Result = %d\n",GPR(6));
    print_cache_stats(cp);
		SET_GPR(2, SS_SYS_exit);
		SYSCALL(wb.inst);
	}

  if(mw.regWrite) {
    SET_GPR(mw.dst, mw.data);
  }
}

void print_to_trace()
{
  printf("[Cycle %3d]---------------------------------\n", cycle_num);
	printf("[IF]  ");md_print_insn(fd.inst, fd.valP, stdout);printf("\n");
	printf("[ID]  ");md_print_insn(de.inst, de.valP, stdout);printf("\n");
  printf("rs: %d, rt: %d, valS: %d, valT: %d\n", de.rs, de.rt, de.valS, de.valT);
	printf("[EX]  ");md_print_insn(em.inst, em.valP, stdout);printf("\n");
  printf("dst: %d, data: %d, regWrite: %d\n", em.dst, em.valE, em.regWrite);
	printf("[MEM] ");md_print_insn(mw.inst, mw.valP, stdout);printf("\n");
  printf("dst: %d, data: %d, regWrite: %d\n", mw.dst, mw.data, mw.regWrite);
	printf("[WB]  ");md_print_insn(wb.inst, wb.valP, stdout);printf("\n");
	printf("[REGS]r5=%d r6=%d r8=%d r9=%d r10=%d r12=%d r13=%d r14=%d mem = %d\n", 
			GPR(5),GPR(6),GPR(8),GPR(9),GPR(10),GPR(12),GPR(13),GPR(14),MEM_READ_WORD(mem, GPR(20)));
	printf("----------------------------------------------\n");
}

/*  LAB 2  */

/* extract/reconstruct a block address */
#define CACHE_BADDR(addr)	((addr) & ~0xf)
#define CACHE_MK_BADDR(tag, set) (((tag) << 8) | ((set) << 4))

/* cache data block accessor by word */
#define CACHE_WORD(data, bofs)	  (*((unsigned int *)(((char *)data) + (bofs))))

struct cache_t * cache_create(unsigned int hit_latency, unsigned int miss_latency, int enabled)
{
  struct cache_t *cp;
  struct cache_set_t *set;
  struct cache_blk_t *blk;
  int i, j;

  // printf("%d\n", sizeof(struct cache_t));

  /* allocate the cache structure */
  cp = (struct cache_t *) malloc(sizeof(struct cache_t));
  if (!cp)
    fatal("out of virtual memory");

  cp->enabled = enabled;
  cp->miss_latency = miss_latency;
  cp->hit_latency = hit_latency;

  /* initialize user parameters */
  cp->nsets = 16;
  cp->bsize = 16;
  cp->assoc = 4;
  cp->hit_latency = hit_latency;

  /* initialize cache stats */
  cp->hits = 0;
  cp->misses = 0;
  cp->replacements = 0;
  cp->writebacks = 0;
  cp->mem_accesses = 0;

  for (i = 0; i < cp->nsets; i++) {
    cp->sets[i] = malloc(sizeof(struct cache_set_t));
    for (j = 0; j < cp->assoc; j++) {
      cp->sets[i]->blks[j] = malloc(sizeof(struct cache_blk_t));
      cp->sets[i]->blks[j]->tag = 0;
      cp->sets[i]->blks[j]->status = 0;
      cp->sets[i]->blks[j]->last_access_time = 0;
    }
  }
  // printf("%d\n", cp->sets[3]->blks[2]->tag);
  return cp;
}

static struct cache_blk_t*
select_blk_to_repl(struct cache_set_t *set, int total_num) {
  int current_min = set->blks[0]->last_access_time;
  int min_idx = 0;
  int i;
  for (i = 0; i < total_num; i++) {
    if (set->blks[i]->last_access_time < current_min) {
      current_min = set->blks[i]->last_access_time;
      min_idx = i;
    }
  }
  set->blks[min_idx]->last_access_time = cycle_num;
  return set->blks[min_idx];
}

unsigned int access_cache_word(struct cache_t *cp, enum mem_cmd cmd, md_addr_t addr, sword_t *data)
{
  int lat = 0;
  // printf("access %p\n", addr);

  if (!cp->enabled) {
    /* copy data out of cache block */
    if (cmd == Read) {
      *data = MEM_READ_WORD(mem, addr);
    } else if (cmd == Write) {
      MEM_WRITE_WORD(mem, addr, *data);
    }
    lat += cp->miss_latency;
    cp->mem_accesses++;
    return lat;
  }
  
  md_addr_t tag = addr >> 8;
  md_addr_t set_idx = (addr >> 4) & 0xf;
  md_addr_t bofs = addr & 0xf;
  
  struct cache_set_t *set = cp->sets[set_idx];
  struct cache_blk_t *blk, *repl;

  int _i;
  for (_i = 0; _i < cp->assoc; _i++) {
    blk = set->blks[_i];
	  if (blk->tag == tag && (blk->status & CACHE_BLK_VALID)) {
      // printf("hit, blk_tag=%d, tag=%d\n", blk->tag, tag);
      cp->hits++;
      goto cache_hit;
    }
	}

  /* cache block not found */

  /* **MISS** */
  cp->misses++;

  /* select the appropriate block to replace, and re-link this entry to
     the appropriate place in the way list */
  repl = select_blk_to_repl(set, cp->assoc);

  /* write back replaced block data */
  if (repl->status & CACHE_BLK_VALID) {
    cp->replacements++;
    if (repl->status & CACHE_BLK_DIRTY) {
      /* write back the cache block */
      cp->writebacks++;

      int words = cp->bsize >> 2;
      int i = 0;
      md_addr_t p = CACHE_MK_BADDR(repl->tag, set_idx);
	    while (words-- > 0) {
        word_t temp = repl->data[i] | (unsigned)repl->data[i+1] << 8 | (unsigned)repl->data[i+2] << 16 | (unsigned)repl->data[i+3] << 24;
	      MEM_WRITE_WORD(mem, p, temp);
	      p += 4; i += 4;
        lat += cp->miss_latency;
        cp->mem_accesses++;
	    }
      // lat += cp->miss_latency;
      // cp->mem_accesses++;
    }
  }

  /* replace the block */
  blk = repl;

  /* update block tags */
  blk->tag = tag;
  blk->status = CACHE_BLK_VALID;

  /* read data block */
  int words = cp->bsize >> 2;
  int i = 0;
  md_addr_t p = CACHE_BADDR(addr);
  while (words-- > 0) {
    word_t temp = MEM_READ_WORD(mem, p);
    blk->data[i] = (byte_t) (temp & 0xff);
    blk->data[i+1] = (byte_t) ((temp >> 8) & 0xff);
    blk->data[i+2] = (byte_t) ((temp >> 16) & 0xff);
    blk->data[i+3] = (byte_t) ((temp >> 24) & 0xff);
    p += 4; i += 4;
    lat += cp->miss_latency;
    cp->mem_accesses++;
  }
  // lat += cp->miss_latency;
  // cp->mem_accesses++;

  // /* copy data out of cache block */
  // if (cmd == Read) {
    
  //   lat += cp->hit_latency;
  // } else if (cmd == Write) {
  //   blk->data[bofs] = *data;
  //   lat += cp->hit_latency;
  //   blk->status |= CACHE_BLK_DIRTY;
  // }
  
  // /* return latency of the operation */
  // return lat;


 cache_hit: /* **HIT** */

  /* copy data out of cache block */
  if (cmd == Read) {
    *data = blk->data[bofs] | blk->data[bofs+1] << 8 | blk->data[bofs+2] << 16 | blk->data[bofs+3] << 24;
    lat += cp->hit_latency;
  } else if (cmd == Write) {
    blk->data[bofs] = (byte_t) (*data & 0xff);
    blk->data[bofs+1] = (byte_t) ((*data >> 8) & 0xff);
    blk->data[bofs+2] = (byte_t) ((*data >> 16) & 0xff);
    blk->data[bofs+3] = (byte_t) ((*data >> 24) & 0xff);
    lat += cp->hit_latency;
    blk->status |= CACHE_BLK_DIRTY;
  }

  /* return first cycle data is available to access */
  return lat;
}

void print_cache_stats(struct cache_t *cp)
{
  printf("Total number of clock cycles: %d,\n", cycle_num);
  printf("Total number of memory access: %d,\n", cp->mem_accesses);
  printf("Total number of cache hits: %d,\n", cp->hits);
  printf("Total number of cache misses: %d,\n", cp->misses);
  printf("Total number of cache line replacements: %d,\n", cp->replacements);
  printf("Total number of cache line write backs: %d.\n", cp->writebacks);
}