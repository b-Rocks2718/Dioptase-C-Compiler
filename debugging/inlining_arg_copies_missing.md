# Missing inlining argument-copy TAC

## Symptom

Running `./build/debug/bcc tests/test.c -opt -tac -cg -s` prints two
`Inlining callsite: add` messages, but the final TAC still contains the call
and contains no copies from the arguments into renamed parameters.

## Findings

- `inline_callsite` currently inserts only argument-to-parameter `TACCOPY`
  instructions. It does not copy the callee body, rewrite returns, or remove
  the original call.
- `perform_inlining` runs twice (`NUM_INLINE_ITERS == 2`). Because the original
  call remains, both iterations process the same callsite and insert two sets
  of copies.
- The optimizer reruns the enabled body passes after inlining. With `-opt`,
  dead-store elimination removes all four copies because the renamed
  parameters are never read by any copied callee body.
- `-inline -constant-fold -tac` and `-inline -dead-code -tac` expose both sets
  of copies before the unchanged call. `-inline -dead-store -tac` reproduces
  their disappearance.
- `-inline` by itself currently does nothing because `optimize` returns early
  when no CFG/body optimization is enabled, before reaching the inlining pass.

## Conclusion

The TAC printer is showing the actual post-optimization body. The missing
copies are dead stores removed after an incomplete inlining transformation;
this is not a TAC-printing problem.
