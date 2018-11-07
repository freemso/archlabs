# Arch. Lab2 Design Doc
*Atuhor: Chengzu Ou (14302010037)*



In this lab, we are asked to modify the `sim-pipe` to let it simulate the behavior of cache.

## Data Structures
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













## Attach Cache to Processor

Now that we’ve had the data structure of the cache, how can we attach the cache to the pipeline processor to make it useful?

Cache is only useful when it comes to memory access. Cache serves as a middle layer between processor and memory. Every time when there is a memory access, whether it’s read or write, the processor would ask cache for data. So, all we need to do is to provide an `access_cache` function and replace every memory access operation, in this case `MEM_READ_WORD`, with this function. Of course, we have to create a cache and do some initialization work, `cache_create`, before we start the simulation.

Note that besides the well-known memory access stage, `do_mem`, in our pipeline processor, we also have another memory access operation. That is in the instruction fetch stage. We have to read memory to get the instructions!

Since we are implementing a *unified cache*, which do not differentiate between instruction cache and data cache, we just have to replace `MD_FETCH_INSTI` with our cache access function. Pay attention to the size of the instruction. We have to read two `word`s to fetch an instruction.



## Design of Cache Access Function

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

## Statistics
We need to record some statistical information during the access of the cache. 

The latency is added by `cp->miss_latency` every time we access memory and by `cp->hit_latency` every time we access cache.

`cp->mem_access` is added by `1` every time there is a memory access.

`cp->hits` is added by `1` every time the access is a hit. And `cp->misses` is added by `1` if there is a miss.

`cp->replacements` is added by `1` if there is a replacement. And `cp->writebacks` is added by `1` if the replaced block needs to be written back.

## Result Analysis
On the testing of `mmm.s`, we record the statistics when both cache is enabled or not. The result is listed in `result-with-cache.txt` and `result-without-cache,txt`.

From the result, we can see that total number of cycles is hugely reduced when cache is enabled. It shows that cache is indeed useful in reducing processing time.

We can also see that the number of hits is way more than the number of misses. It also proves that cache is useful since cache hit is much more efficient than directly accessing memory even though cache miss is expensive.