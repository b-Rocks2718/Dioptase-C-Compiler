	.text
	
	.global _start
_start:
  # eventually heap init would be called here
	call main

  # Trap ABI:
  # - r1 = trap code
  # - r2 = pointer to trap arguments
  #
  # exit uses trap code 0 and carries the 32-bit status directly in r2.
  mov  r2, r1
_start_exit_loop:
  movi r1, 0
  trap
  jmp _start_exit_loop

.global exit
exit:
  mov  r2, r1
exit_loop:
  movi r1, 0
  trap
  jmp exit_loop
