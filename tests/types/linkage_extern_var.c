extern int shared_value;
int shared_value;

int main(void) { /* Exercise linkage extern var behavior. */
  return shared_value;
}
