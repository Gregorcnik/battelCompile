mars 0
;====================================

;prepare counter
mv [counter], pc

;create instruction to jump to abyss
ldi 0b101001.11100.0000
mv [fire_instr], r0
shli [fire_instr], 1 ;instr: jmp r28

;main loop
mv [main], pc
  subi [counter], 2 ;step
  st [fire_instr], [counter] ;fire
  flag
  jmp [main]