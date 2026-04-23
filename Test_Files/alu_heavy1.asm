addi $t0, $zero, 0
addi $t1, $zero, 10

loop:
lw   $t2, 0($zero)
add  $t3, $t2, $t2
sub  $t4, $t3, $t2
and  $t5, $t4, $t3
or   $t6, $t5, $t2
or   $t7, $t6, $t3
add  $t8, $t7, $t2
sw   $t8, 0($zero)
addi $t0, $t0, 1
beq  $t0, $t1, end
j    loop

