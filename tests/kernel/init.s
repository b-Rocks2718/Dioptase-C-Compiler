
# Physmem address range: 0x0000000 - 0x7FFFFFF

.define KERNEL_STACK_TOP, 0x400000
.define KERNEL_STACK_SIZE, 0x10000

# kernel entry point
  .global _start
  .origin 0x400
_start:
  # initialize kernel stack
  mov  r1, cid # get core id
  movi r2, KERNEL_STACK_SIZE
  movi sp, KERNEL_STACK_TOP
  call umul
  sub  sp, sp, r1

  call main

  mode halt

# Provide a simple exit routine for kernel-mode tests.
  .global exit
exit:
  mode halt
