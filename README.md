# Architecture Labs

## Lab1

### Part 1 Design

In part one of the lab, we are asked to implement two instructions *addOk* and *bitCount*.

In order to implement the instructions, we need to know the `opcode` of each instruction. To accomplish this, we use `objdump` from `simpleutil` to get the assembler code of the test cases. By doing some search in the dump file, we successfully find out the `opcode` of *addOk* and *bigCount* respectively from the following lines:

```
	...
	400260:	42 00 00 00 	addu $17,$0,$5
	400264:	00 11 05 00 
	400268:	34 00 00 00 	sw $19,28($29)
	40026c:	1c 00 13 1d 
	400270:	61 00 00 00 	0x00000061:10111300
	400274:	00 13 11 10 
	400278:	34 00 00 00 	sw $31,32($29)
	40027c:	20 00 1f 1d 
	400280:	34 00 00 00 	sw $18,24($29)
	400284:	18 00 12 1d 
	400288:	02 00 00 00 	jal 4001f0 <test_addOK>
	...
	...
	400348:	06 00 00 00 	bne $2,$3,400370 <bitCount+0x68>
	40034c:	08 00 03 02 
	400350:	28 00 00 00 	lw $2,32($30)
	400354:	20 00 02 1e 
	400358:	62 00 00 00 	0x00000062:02030001
	40035c:	01 00 03 02 
	400360:	34 00 00 00 	sw $3,16($30)
	400364:	10 00 03 1e 
	400368:	01 00 00 00 	j 400388 <bitCount+0x80>
	...
```

The `opcode` is `0x61` for *addOk* and `0x62` for *bitCount*.

After obtaining the `opcode`, we start to implement the instruction in `machine.def`. Since the format of *addOk* is almost identical to *add*, we could just copy the format and change the `opcode`. We do the same for *bitCount*.

The actual implementation of the two instruction is quite simple and straight forward. We use a `while` loop in *bitCount*, although it could be done using only bitwise operator.

### Part 2 Design

In part two, we shall implement a five-stage pipelined simulator that could do operand forwarding.

#### Pipeline with Stall

We start with the one without operand forwarding.

First, let’s take a look at the data structure we used in the simulator. In `sim-pipe.h`, the main data structures we used are five stage buffers: `ifid_buf`, `idex_buf`, `exmem_buf`, `memwb_buf`, and `wb_buf`. The first four buffers are used in pipeline stages to store useful data. The last one, `wb_buf`, is **not** directed used in pipeline but are useful in printing the trace.

In each buffer, we have some data structures used to store data, such as `md_inst_t inst` and `md_addr_t valP`. The meaning and usage of the data will be discussed later when explaining the function in each stage.

For a instruction to go through the pipeline, it has to go through each stage, one at a clock cycle. In each cycle, the simulator run in a way like this:

```c
while (TRUE) {
    cycle_num++;
    check_stall();
    do_wb();
    do_mem();
    do_ex();
    do_id();
    do_if();
    print_to_trace();
  }
```

As we can see, the five stages of the simulator basically run in a reversed order along with the direction of the data flow and control flow. That is because we do not want the change of data in each stage buffer affect the next stage. We need the data in current stage buffer to update the next stage buffer. In real CPU, the change could be write into the buffer only when the clock rises.

We will go through the operation we have in each function listed above.

##### Check Stall

This function is used to handle hazards, data hazards to be exact. There are two kinds of data hazards: `emHazard` and `mwHazard`, each corresponding to hazard happened in two stages.

Data hazard happens when the following instructions trying to read registers that are to be written in the previous instruction while the previous instruction hasn’t got to the *write back* stage yet.

Without using operands forwarding, we need to add some `stall`s to solve this problem. To stall the simulator, we only need to repeatedly decode the same instruction while keeping `NPC`(next PC) from increasing.

##### Instruction Fetch

We use two special register file called `regs.regs_PC` and `regs.regs_NPC` to store current PC and next PC respectively.

In F stage, we first need to select current PC. Since we do not have `ret` instruction in this ISA, we only need to care about two situations when selecting current PC: when there is a mis-prediction 1) select target PC, and otherwise 2) select the next PC store in the register files.

After selecting the current PC, we fetch the instruction use this address from instruction memory.

Before write the instruction to the `fd_buf`, we have to predict the next PC based on the instruction we just fetched. Our branch prediction strategy is to alway **NOT** taken, which means the next PC should always be predicted as current PC plus size of the instruction, unless the instruction is unconditional jump(`JUMP`), in which case the next PC should be the address that we are jumping to.

The data called `valP` is actually served as the PC address of the current instruction and will be passed along the pipeline for it has its use in many ways.

##### Decode

Before starting to do the actual decoding work, we have to first check if there is a mis-prediction ahead. If there is, we have to cancel the decoding because the instruction we are decoding now is not the one we actually wanted to have.

Data like `inst` and `valP` are directed copied from previous buffer `fd_buf`.

In decode stage, we don’t have to consider that many situations. In fact, all we need to do is to read from register files using `RS` and `RT`.

##### Execute
Just like in the decode stage, we also need to check if there is mis-prediction and insert a bubble if there is. When inserting the bubble, by which we mean abort current instruction, we have to pay much attention to clear the data store within the buffer, especially the data relating to the judgement of whether there is a mis-prediction.

Here you may notice that we use a `em_old` structure. It is identical to `em_buf` and its main use is to store the data left by the previous instruction. The reason we have this is that unlike in simulator, which runs in a reversed order, in real CPU, the operation in each stage is run simultaneously. Thus, by the time we know there is a mis-prediction, there will have been already two instructions fetched. If we simply use data in `em_buf` to decide whether there is a mis-prediction, we would be able to prevent the second instruction to be fetched. In order to make the simulator more similar to real case, we have to maintain a older version of the buffer and manually copy the data before doing the execution.

In this stage, besides regular data, we have five special boolean variable. Their meaning are listed below:

- `isBranch`: whether the instruction is a branch instruction
- `taken`: whether the branch is taken or not
- `writeRT` and `writeRD`: whether the instruction will need to write `RT` or `RD` register in the following stages
- `memLoad`: whether the instruction will need to load data from memory and write to register

The result produced by the ALU in this stage is store in `valE`.

##### Memory

First we have to copy every useful data from previous buffer. Then we do the write-to-memory or read-from-memory thing based on the instruction.

##### Write Back

In this stage we mainly deal with two things: write back and deal with `syscall`.

Write back is done based on the boolean variable generated in the previous stages.

The reason why `syscall` must be handle in this stage is that we want the program to exist only when the `SYSCALL` instruction passes the last stage of the pipeline.

#### Pipeline with Forward

After completing the version with stall, we only need to add a small amount of code to make it support operands forwarding.

The simplest way to achieve this is to add a `forward()` function after `check_stall()` and before `do_wb()`. In forward function, we handle the two kinds of hazards by forward the value calculated in `ex` stage and value that load from memory in  `mem` stage respectively.

One thing worth notice is that although we can use operands forwarding to handle most of the data hazards, we can not solve the load-use hazard which means using the register and are expected to load data from memory with. In this case, we still need to stall for at least one cycle in order to properly handle this hazard.

#### Inst. list

- `ADD`
- `ADDU`
- `SUBU`
- `ADDIU`
- `ANDI`
- `BNE`
- `JUMP`
- `LUI`
- `LW`
- `SLL`
- `SW`
- `SLTI`
- `SYSCALL`
- `NOP`

## Lab2

In this lab, we are asked to modify the `sim-pipe` to let it simulate the behavior of cache.

### Data Structures
To simulate cache in SimpleScalar, we need a data structure rest in the memory that behaves like a cache just like the simulation of xmemory. The data structure looks like this:

```c
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
```

We use `enabled` to toggle the cache on and off. And we store some configuration such as the number of sets and hit latency in the cache. We also record some statistical information within the cache to print out later. The last field `sets` is the array of `struct cache_set_t` pointers with each one of them points to a data structure that holds a cache set. Let’s take a look at it:

```c
/* cache set definition (one or more blocks sharing the same set index) */
struct cache_set_t
{
  struct cache_blk_t *blks[4];
};
```

Pretty simple, right? We only need to know where is the blocks in the cache set. So there is only one field in this structure, `blks`, which is an array of `struct cache_blk_t` pointer with each one points to a cache block:

```c
/* cache block (or line) definition */
struct cache_blk_t
{
  int last_access_time;
  md_addr_t tag;		/* data block tag value */
  unsigned int status;		/* block status, see CACHE_BLK_* defs above */
  byte_t data[16];		/* actual data block starts here, block size should probably be a multiple of 8 */
};
```

`tag` is the tag value of the block. `status` is the status of the block. There are two status bits controlling the status of the block:
```c
/* block status values */
#define CACHE_BLK_VALID		0x00000001	/* block in valid, in use */
#define CACHE_BLK_DIRTY		0x00000002	/* dirty block */
```

Note that we also have a `last_access_time` field in cache block. This is used to record the time that the block is cached. Since we use FIFO replacement strategy, it is useful to have the created time of the block recorded so that we can select the blocks to be replaced later.

### Attach Cache to Processor

Now that we’ve had the data structure of the cache, how can we attach the cache to the pipeline processor to make it useful?

Cache is only useful when it comes to memory access. Cache serves as a middle layer between processor and memory. Every time when there is a memory access, whether it’s read or write, the processor would ask cache for data. So, all we need to do is to provide an `access_cache` function and replace every memory access operation, in this case `MEM_READ_WORD`, with this function. Of course, we have to create a cache and do some initialization work, `cache_create`, before we start the simulation.

Note that besides the well-known memory access stage, `do_mem`, in our pipeline processor, we also have another memory access operation. That is in the instruction fetch stage. We have to read memory to get the instructions!

Since we are implementing a *unified cache*, which do not differentiate between instruction cache and data cache, we just have to replace `MD_FETCH_INSTI` with our cache access function. Pay attention to the size of the instruction. We have to read two `word`s to fetch an instruction.

### Design of Cache Access Function

First we check if the cache is enabled. If it’s not, we do the old school memory access.
```c
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
```

If cache is enabled, we decode the address to access and get the tag, set index, and block offset.
```c
md_addr_t tag = addr >> 8;
md_addr_t set_idx = (addr >> 4) & 0xf;
md_addr_t bofs = addr & 0xf;
```

We iterate through the cache set to find if the access is a hit or not.

```c
int _i;
for (_i = 0; _i < cp->assoc; _i++) {
	blk = set->blks[_i];
	if (blk->tag == tag && (blk->status & CACHE_BLK_VALID)) {
		cp->hits++;
		goto cache_hit;
	}
}

/* MISS */
...
```

If there is a hit, we directly jump to the step below to access the cache. If it’s a miss, we first select a block in this set to be replaced. The selection of the block is implemented based on FIFO. We select the block with the minimum `last_access_time`, which means the oldest block, then update the `last_access_time` with current `cycle_num`. We use current cycle number to indicate the time because it’s strictly incrementing.

```c
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
```

After selecting the block to replace, we first check if the block needs to be written back to memory using `CACHE_BLK_VALID` and `CACHE_BLK_DIRTY` status bits.

If it needs write-back, we write the data in this block to corresponding memory address.
```c
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
	}
}
```

Then we read from memory and update the data and status of the block.

```c
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
```

At last, we access the cache. If we need to write data, do not forget to set dirty bit of the block.

```c
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
```

### Statistics
We need to record some statistical information during the access of the cache. 

The latency is added by `cp->miss_latency` every time we access memory and by `cp->hit_latency` every time we access cache.

`cp->mem_access` is added by `1` every time there is a memory access.

`cp->hits` is added by `1` every time the access is a hit. And `cp->misses` is added by `1` if there is a miss.

`cp->replacements` is added by `1` if there is a replacement. And `cp->writebacks` is added by `1` if the replaced block needs to be written back.

### Result Analysis
On the testing of `mmm.s`, we record the statistics when both cache is enabled or not. The result is listed in `result-with-cache.txt` and `result-without-cache,txt`.

From the result, we can see that total number of cycles is hugely reduced when cache is enabled. It shows that cache is indeed useful in reducing processing time.

We can also see that the number of hits is way more than the number of misses. It also proves that cache is useful since cache hit is much more efficient than directly accessing memory even though cache miss is expensive.

## Lab3

### Abstract

In this report, we will going to take a look at an interesting technology called RDMA. We will explain what it is and why we should consider using it. We will also cover some basic concepts in this field. At last, we will show a possible application of RDMA in key-value services.

### What is RDMA?
RDMA, remote direct memory access, is a direct memory access from the memory of one computer into that of another without involving either one’s operating system.
![](lab3/rdma-arch-layer.tiff)

The term “RDMA” is usually used to refer to networking technologies that have a software interface with Remote DMA.

Just as the name says, remote DMA is DMA on a remote system. The adapter on system 1 can send a message to the adapter on system 2 that causes the adapter on system 2 to DMA data to or from system 2’s memory.

The messages come in two main types:
- **RDMA Write:** includes an address and data to put at that address, and causes the adapter that receives it to put the supplied data at the specified address
- **RDMA Read:** includes an address and a length, and causes the adapter that receives it to generate a reply that sends back the data at the address requested

These messages are “one-sided” in the sense that they will be processed by the adapter that receive them without involving the CPU on the system that receives the messages.

RDMA adapters give fine-grained control over what remote systems are allowed to do. Things like protection domains and memory keys allow you to control connection-by-connection and byte-by-byte with separate read and write permissions. So there is no need to worry about the securities.

### Basic Concepts
To have a better understanding of RDMA, let’s take a look at some basic concepts behind it.

#### Queue pair
To draw an analogy from everyday mail service, queue pair (QP) defines the address of the communication endpoints, or equivalently, sockets in traditional socket based programming. Each communication endpoint needs to create a QP in order to talk to each other.

#### Verbs
In RDMA based programming, verb is a term that defines the types of communication operations. There are two different communication primitives: channel semantics (send/receive) and memory semantics (read/write). If we only consider how data is delivered to the other end, channel semantics involves both communication endpoints: the receiver needs to pre-post receives and the sender posts sends; while memory semantics only involves one side of the communication endpoint: the sender can write the data directly to the receiver's memory region, or the receiver can read from the target's memory region without notifying the target.
![](lab3/verbs.tiff)
Generally speaking, memory semantics has less overhead compared to channel semantics and thus has higher raw performance; On the other hand, channel semantics involves less programming effort.

### Why it is Useful?
RDMA has been widely used in high performance computing (HPC), and now becomes more popular in today's datacenter environment. But why it is useful?

To see why RDMA is useful, you can think of RDMA operations as “direct placement” operations: data comes along with information about where it’s supposed to go. For example, there is a spec for NFS/RDMA, and it’s pretty easy to see why RDMA is nice for NFS. The NFS/RDMA server can service requests in whatever order it wants and return responses via RDMA as they become available; by using direct placement, the responses can go right into the buffers where the client wants them, without requiring the NFS client to do any copying of data.

RDMA supports zero-copy networking by enabling the network adapter to transfer data directly to or from application memory, eliminating the need to copy data between application memory and the data buffers in the operating system. Such transfers require no work to be done by CPUs, caches, or context switches, and transfers continue in parallel with other system operations. When an application performs an RDMA Read or Write request, the application data is delivered directly to the network, reducing latency and enabling fast message transfer.

### An Application of RDMA in K-V Services
There are already many trying to develop an application using RDMA’s good features. One of the attempts is to use RDMA to support remote hash-table access.

DRAM-based key-value stores and caches are widespread in large- scale Internet services. They are used both as primary stores, and as caches in front of backend databases. At their most basic level, these systems export a traditional `GET`/`PUT`/`DELETE` interface. Internally, they use a variety of data structures to provide fast, memory-efficient access to their underlying data.

Recent in-memory object stores have used both tree and hash table-based designs. Hash table design has a long and rich history, and the particular flavor one chooses depends largely on the desired optimization goals.

*Pilaf* is a key-value store that aims for high performance and low CPU use. For `GET`s, clients access a cuckoo hash table at the server using `READ`s, which requires 2.6 round trips on average for single `GET` request. For `PUT`s, clients send their requests to the server using a `SEND` message.

*FaRM* is a more general-purpose distributed computing platform that exposes memory of a cluster of machines as a shared address space. It’s design provides two components for comparison. First is its key-value store design, which uses a variant of *Hopscotch hashing* to create a locality-aware hash table. For `GET`s, clients `READ` several consecutive Hopscotch slots, one of which contains the key with high probability. Another `READ` is required to fetch the value if it is not stored inside the hash table. For `PUT`s, clients `WRITE` their request to a circular buffer in the server’s memory. The server polls this buffer to detect new requests.

### Conclusion
We took a brief introduction to RDMA and showed a possible application of it. RDMA is really an exciting technology in boosting communication within clustering. There are also some critiques of it, mostly related to the the scalability. However it is still a successful technology from some of the programming paradigms.

