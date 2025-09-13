static uint16_t mars_mem[] = {
	0b1000000000111111, // mv [counter], pc
	0b0101001111000000, // ldi 0b101001.11100.0000
	0b1000000001000000, // mv [fire_instr], r0
	0b1101000001000001, // shli [fire_instr], 1 ;instr: jmp r28
	0b1000000001111111, // mv [main], pc
	0b1100110000100010, //   subi [counter], 2 ;step
	0b1011110001000001, //   st [fire_instr], [counter] ;fire
	0b1111110000000000, //   flag
	0b1010010001100000, //   jmp [main]
};
static uint16_t mars_size = 9;
static uint16_t mars_offset = 0;

// [counter]: r1
// [fire_instr]: r2
// [main]: r3
