/*
Calls in tail position from functions that take the address of their own
locals or parameters. The callee dereferences a pointer into the caller's
frame, so the caller must stay alive until the callee returns. A real tail
call would free that frame and the callee's own frame would overwrite it
before the read, so these must be lowered as call + return.
Returns the number of checks that produced the expected value.
*/

int read_ptr(int* p) { /* Read through a pointer into the caller's frame. */
  return *p + 1;
}

int addr_of_local(int x) { /* Pass the address of a local. */
  int local = x * 2;
  return read_ptr(&local);
}

int addr_of_param(int x) { /* Pass the address of a parameter. */
  return read_ptr(&x);
}

int sum_arr(int* arr, int n) { /* Sum an array owned by the caller. */
  int total = 0;
  for (int i = 0; i < n; i++) {
    total += arr[i];
  }
  return total;
}

int array_decay(int x) { /* Pass a local array, which decays to a pointer. */
  int buf[4];
  buf[0] = x;
  buf[1] = x + 1;
  buf[2] = x + 2;
  buf[3] = x + 3;
  return sum_arr(buf, 4);
}

int* g_saved;

int read_saved(void) { /* Read a caller local through a pointer stashed in a global. */
  return *g_saved;
}

int stash_then_call(int x) { /* The callee receives no pointer argument at all. */
  int local = x + 5;
  g_saved = &local;
  return read_saved();
}

int main(void) { /* Exercise tail-position calls that must keep the caller frame. */
  int passed = 0;
  passed += addr_of_local(20) == 41;
  passed += addr_of_param(9) == 10;
  passed += array_decay(10) == 46;
  passed += stash_then_call(7) == 12;
  return passed;
}
