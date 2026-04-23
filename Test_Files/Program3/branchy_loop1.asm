addi $t0, $zero, 0
addi $t1, $zero, 10

loop:
lw   $t2, 0($zero)
add  $t3, $t2, $t0
sub  $t4, $t3, $t2

beq  $t3, $t4, skip
addi $t0, $t0, 1

skip:
beq  $t0, $t1, end
j    loop

end:
