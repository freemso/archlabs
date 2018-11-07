# Arch. Lab1 Part2 Design
In part two, we shall implement a five-stage pipelined simulator that could do operand forwarding.
## Pipeline with Stall
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
### Check Stall
This function is used to handle hazards, data hazards to be exact. There are two kinds of data hazards: `emHazard` and `mwHazard`, each corresponding to hazard happened in two stages.
 Data hazard happens when the following instructions trying to read registers that are to be written in the previous instruction while the previous instruction hasn’t got to the *write back* stage yet.
 Without using operands forwarding, we need to add some `stall`s to solve this problem. To stall the simulator, we only need to repeatedly decode the same instruction while keeping `NPC`(next PC) from increasing.

### Instruction Fetch
We use two special register file called `regs.regs_PC` and `regs.regs_NPC` to store current PC and next PC respectively.
 In F stage, we first need to select current PC. Since we do not have `ret` instruction in this ISA, we only need to care about two situations when selecting current PC: when there is a mis-prediction 1) select target PC, and otherwise 2) select the next PC store in the register files.
After selecting the current PC, we fetch the instruction use this address from instruction memory.
 Before write the instruction to the `fd_buf`, we have to predict the next PC based on the instruction we just fetched. Our branch prediction strategy is to alway **NOT** taken, which means the next PC should always be predicted as current PC plus size of the instruction, unless the instruction is unconditional jump(`JUMP`), in which case the next PC should be the address that we are jumping to.
The data called `valP` is actually served as the PC address of the current instruction and will be passed along the pipeline for it has its use in many ways.

### Decode
Before starting to do the actual decoding work, we have to first check if there is a mis-prediction ahead. If there is, we have to cancel the decoding because the instruction we are decoding now is not the one we actually wanted to have.
Data like `inst` and `valP` are directed copied from previous buffer `fd_buf`.
In decode stage, we don’t have to consider that many situations. In fact, all we need to do is to read from register files using `RS` and `RT`.

### Execute
Just like in the decode stage, we also need to check if there is mis-prediction and insert a bubble if there is. When inserting the bubble, by which we mean abort current instruction, we have to pay much attention to clear the data store within the buffer, especially the data relating to the judgement of whether there is a mis-prediction.
Here you may notice that we use a `em_old` structure. It is identical to `em_buf` and its main use is to store the data left by the previous instruction. The reason we have this is that unlike in simulator, which runs in a reversed order, in real CPU, the operation in each stage is run simultaneously. Thus, by the time we know there is a mis-prediction, there will have been already two instructions fetched. If we simply use data in `em_buf` to decide whether there is a mis-prediction, we would be able to prevent the second instruction to be fetched. In order to make the simulator more similar to real case, we have to maintain a older version of the buffer and manually copy the data before doing the execution.
In this stage, besides regular data, we have five special boolean variable. Their meaning are listed below:
- `isBranch`: whether the instruction is a branch instruction
- `taken`: whether the branch is taken or not
- `writeRT` and `writeRD`: whether the instruction will need to write `RT` or `RD` register in the following stages
- `memLoad`: whether the instruction will need to load data from memory and write to register
The result produced by the ALU in this stage is store in `valE`.

### Memory
First we have to copy every useful data from previous buffer. Then we do the write-to-memory or read-from-memory thing based on the instruction.

### Write Back
In this stage we mainly deal with two things: write back and deal with `syscall`.
Write back is done based on the boolean variable generated in the previous stages.
The reason why `syscall` must be handle in this stage is that we want the program to exist only when the `SYSCALL` instruction passes the last stage of the pipeline.

## Pipeline with Forward
After completing the version with stall, we only need to add a small amount of code to make it support operands forwarding.
The simplest way to achieve this is to add a `forward()` function after `check_stall()` and before `do_wb()`. In forward function, we handle the two kinds of hazards by forward the value calculated in `ex` stage and value that load from memory in  `mem` stage respectively.
One thing worth notice is that although we can use operands forwarding to handle most of the data hazards, we can not solve the load-use hazard which means using the register and are expected to load data from memory with. In this case, we still need to stall for at least one cycle in order to properly handle this hazard.