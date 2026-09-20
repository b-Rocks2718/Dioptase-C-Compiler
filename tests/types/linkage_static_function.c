int shared_value;

static int helper(void) { /* Provide the helper function referenced by this test. */
  return shared_value;
}

int main(void) { /* Exercise linkage static function behavior. */
  return helper();
}
