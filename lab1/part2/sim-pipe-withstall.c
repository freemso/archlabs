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
struct exmem_buf em, em_old;
struct memwb_buf mw;
struct wb_buf wb;

unsigned cycle_num; /* Cycle Number counter, used in trace file*/

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

  /* initialize cycle counter*/
  cycle_num = 0;

  /* initialize stage latches*/
 
  /* IF/ID */
  fd.inst.a = NOP;
  fd.inst.b = 0;
  fd.valP = 0;

  /* ID/EX */
  de.inst.a = NOP;
  de.inst.b = 0;
  de.valP = 0;
  de.rs = DNA;
  de.rt = DNA;
  de.valS = 0;
  de.valT = 0;

  /* EX/MEM */
  em.inst.a = NOP;
  em.inst.b = 0;
  em.valP = 0;
  em.isBranch = 0;
  em.taken = 0;
  em.writeRT = 0;
  em.writeRD = 0;
  em.memLoad = 0;
  em.rd = DNA;
  em.rt = DNA;
  em.valE = 0;
  em.dataIn = 0;
  em.addr = 0;

  /* MEM/WB */
  mw.inst.a = NOP;
  mw.inst.b = 0;
  mw.valP = 0;
  mw.valE = 0;
  mw.valM = 0;
  mw.rd = DNA;
  mw.rt = DNA;
  mw.writeRT = 0;
  mw.writeRD = 0;
  mw.memLoad = 0;

  // WB
  wb.inst.a = NOP;
  wb.inst.b = 0;
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
    do_wb();
    do_mem();
    do_ex();
    do_id();
    do_if();

    // now print current state to trace file
    print_to_trace();
  }
}

void check_stall() {
  bool_t emHazard = (em.inst.a != NOP) && (
    (em.writeRT && (em.rt == de.rs || em.rt == de.rt))
    || (em.writeRD && (em.rd == de.rs || em.rd == de.rt)));
  bool_t mwHazard = (mw.inst.a != NOP) && (
    (mw.writeRT && (mw.rt == de.rs || mw.rt == de.rt))
    || (mw.writeRD && (mw.rd == de.rs || mw.rd == de.rt)));

	if(emHazard || mwHazard) {
    // Stall a cycle
		fd.inst = de.inst;
		fd.valP = de.valP;
		de.inst.a = NOP;
    SET_NPC(CPC);
	}	
}

void do_if()
{
  md_inst_t inst;

  // Select PC
  if (em_old.isBranch && em_old.taken) {
    // Mispredicted branch
    // Since our prediction strategy is to always NOT taken,
    // we only need to select the PC after the branch instruction,
    // which is stored in valP of em stage.
  	CPC = em_old.addr;
  } else {
  	CPC = regs.regs_NPC;
  }
  
  // Fetch instruction from memory
  MD_FETCH_INSTI(inst, mem, CPC);

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
  if (em_old.isBranch && em_old.taken) {
    // Mispredicted branch
    // Insert a bubble here
  	de.inst.a = NOP;
    de.valP = fd.valP;
    return;
  }

  // Copy the buffer
  de.inst = fd.inst;
  de.valP = fd.valP;

  md_inst_t inst;
  inst = de.inst;

  // Decode
  de.rs = RS;
  de.rt = RT;

  // Read from Reg
  de.valS = GPR(RS);
  de.valT = GPR(RT);
}

void do_ex()
{
  // Preserve the old buffer for PC selection
  em_old = em;
  // em_old.inst.a = em.inst.a;
  // em_old.inst.b = em.inst.b;
  // em_old.valP = em.valP;
  // em_old.isBranch = em.isBranch;
  // em_old.taken = em.taken;
  // em_old.writeRT = em.writeRT;
  // em_old.writeRD = em.writeRD;
  // em_old.rd = em.rd;
  // em_old.rt = em.rt;
  // em_old.valE = em.valE;
  // em_old.dataIn = em.dataIn;
  // em_old.addr = em.addr;

  if (em_old.isBranch && em_old.taken) {
    // Mispredicted branch
    // Insert a bubble here
  	em.inst.a = NOP;
    em.valP = de.valP;
    em.isBranch = 0;
    em.taken = 0;
    return;
  }

  // Copy the buffer
  em.inst = de.inst;
  em.valP = de.valP;

  md_inst_t inst;
  inst = em.inst;

  em.isBranch = 0;
  em.taken = 0;
  em.writeRT = 0;
  em.writeRD = 0;
  em.memLoad = 0;
  em.rt = RT;
  em.rd = RD;

  int opcode;
  MD_SET_OPCODE(opcode, inst);

  switch(opcode) {
    case ADD:
      em.valE = (int)de.valS + (int)de.valT;
      em.writeRD = 1;
		  break;
    case ADDU:
      em.valE = (unsigned)de.valS + (unsigned)de.valT;
      em.writeRD = 1;
		  break;
    case SUBU:
      em.valE = (unsigned)de.valS - (unsigned)de.valT;
      em.writeRD = 1;
		  break;
    case ADDIU:
      em.valE = (unsigned)de.valS + IMM;
      em.writeRT = 1;
		  break;
	  case ANDI:
      em.valE = de.valS & UIMM;
      em.writeRT = 1;
		  break;
	  case BNE:
      em.isBranch = 1;
      em.taken = (bool_t)(de.valS != de.valT);
      em.addr = em.valP + 8 + (OFS << 2);
		  break;
	  case JUMP:
		  break;
	  case LUI:
      em.valE = UIMM << 16;
      em.writeRT = 1;
		  break;
	  case LW:
      em.valE = de.valS + OFS;
      em.writeRT = 1;
      em.memLoad = 1;
		  break;
	  case SLL:
      em.valE = de.valT << SHAMT;
      em.writeRD = 1;
		  break;
	  case SW:
      em.valE = de.valS + OFS;
      em.dataIn = de.valT;
		  break;
	  case SLTI:
      em.valE = de.valS < IMM;
      em.writeRT = 1;
	    break;
  }
  	
}

void do_mem()
{
  // Copy the buffer
  mw.inst = em.inst;
  mw.valP = em.valP;
  mw.valE = em.valE;
  mw.writeRT = em.writeRT;
  mw.writeRD = em.writeRD;
  mw.memLoad = em.memLoad;

  md_inst_t inst;
  inst = mw.inst;

  mw.rt = RT;
  mw.rd = RD;

  int opcode;
  MD_SET_OPCODE(opcode, inst);

  switch(opcode) {
	  case LW:
      mw.valM = MEM_READ_WORD(mem, em.valE);
		  break;
	  case SW:
      MEM_WRITE_WORD(mem, em.valE, em.dataIn);
		  break;
  }
  
}                                                                                        

void do_wb()
{
  wb.inst = mw.inst;
  wb.valP = mw.valP;

	md_inst_t inst;
  inst = wb.inst;

  int opcode;
  MD_SET_OPCODE(opcode, inst);

  if(opcode == NOP) {
    return;
  }

  if(opcode == SYSCALL){
		printf("Loop terminated. Result = %d\n",GPR(6));
		SET_GPR(2, SS_SYS_exit);
		SYSCALL(mw.inst);
	}

  if(mw.memLoad) {
    // write data from memory
    if(mw.writeRT) {
      SET_GPR(RT, mw.valM);
    }
    if(mw.writeRD) {
      SET_GPR(RD, mw.valM);
    }
  } else {
    // write data from execution
    if(mw.writeRT) {
      SET_GPR(RT, mw.valE);
    }
    if(mw.writeRD) {
      SET_GPR(RD, mw.valE);
    } 
  }
}

void print_to_trace()
{
  printf("[Cycle %3d]---------------------------------\n", cycle_num);
	printf("[IF]  ");md_print_insn(fd.inst, fd.valP, stdout);printf("\n");
	printf("[ID]  ");md_print_insn(de.inst, de.valP, stdout);printf("\n");
	printf("[EX]  ");md_print_insn(em.inst, em.valP, stdout);printf("\n");
	printf("[MEM] ");md_print_insn(mw.inst, mw.valP, stdout);printf("\n");
	printf("[WB]  ");md_print_insn(wb.inst, wb.valP, stdout);printf("\n");
	printf("[REGS]r2=%d r3=%d r4=%d r5=%d r6=%d mem = %d\n", 
			GPR(2),GPR(3),GPR(4),GPR(5),GPR(6),MEM_READ_WORD(mem, GPR(30)+16));
	printf("----------------------------------------------\n");
}

