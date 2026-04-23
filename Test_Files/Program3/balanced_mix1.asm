addi $t0, $zero, 0
addi $t1, $zero, 32

loop:
lw   $t2, 0($t0)
add  $t3, $t2, $t2
sub  $t4, $t3, $t2
and  $t5, $t4, $t3
or   $t6, $t5, $t2
sw   $t6, 0($t0)

addi $t0, $t0, 4
beq  $t0, $t1, end
j    loop

end:

