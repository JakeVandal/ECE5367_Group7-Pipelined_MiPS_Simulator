# Sample program for mips_single_cycle_sim.py

main:
    lw  $t0, 0($zero)
    lw  $t1, 4($zero)
    add $t2, $t0, $t1
    sw  $t2, 8($zero)
    beq $t2, $t1, equal
    sub $t3, $t2, $t0
    j done

equal:
    and $s0, $t0, $t1

done:
    or  $s1, $t0, $t1
    slt $s2, $t1, $t0


	
	
