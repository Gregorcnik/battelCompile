mars 377
;first line MUST be program name and offset

;calc main start to r1
;multiple whitespaces are OK and case doesn't matter
  mv r1, pc
  LDI  19
  add R1  R0

;prepare step to r2
ldi 2
add r2 r0

;save 1 to r29
ldi 1
mv r29, r0

;prepare counter to r4
mv r4, pc
ldi 10
shr r4, r0
add r4, r29
shl r4, r0

;put instruction to jump to abyss to r3
ldi b101000111000000 ;instr: jmp r28
mv r3, r0
shl r3, r29

;main
;------------------------------------
#starts 20
;^ means fill with zeroes, so the next instruction is 20. Useful for developing before optimizing. 
;Will maybe change to a label goto system, that won't require wasting space

st r3, r4 ;fire
add r4, r2 ;increment
flag
jmp r1 ;jump to main