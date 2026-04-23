# Sample program1a for mips_single_cycle_sim.py
        addi $t0, $zero, 5       # t0 = 5
        addi $t1, $zero, 5       # t1 = 5
        addi $t2, $zero, 9       # t2 = 9

        beq  $t0, $t1, EQUAL     # branch taken
        addi $t3, $zero, 111     # should be skipped

EQUAL:  addi $t3, $zero, 222     # t3 = 222

        beq  $t0, $t2, NOTEQ     # not taken
        addi $t4, $zero, 333     # t4 = 333
        beq  $zero, $zero, DONE  # unconditional jump using beq

NOTEQ:  addi $t4, $zero, 444     # should be skipped

DONE:   addi $t5, $zero, 555     # t5 = 555



	
	
