# Arch. Lab1 Part1 Design
In part one of the lab, we are asked to implement two instructions *addOk* and *bitCount*.
In order to implement the instructions, we need to know the `opcode` of each instruction. To accomplish this, we use `objdump` from `simpleutil` to get the assembler code of the test cases. By doing some search in the dump file, we successfully find out the `opcode` of *addOk* and *bigCount* respectively from the following lines:
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
The `opcode` is `0x61` for *addOk* and `0x62` for *bitCount*.
After obtaining the `opcode`, we start to implement the instruction in `machine.def`. Since the format of *addOk* is almost identical to *add*, we could just copy the format and change the `opcode`. We do the same for *bitCount*.
The actual implementation of the two instruction is quite simple and straight forward. We use a `while` loop in *bitCount*, although it could be done using only bitwise operator.
