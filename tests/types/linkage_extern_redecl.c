extern int shared_value;
extern int shared_value;
int shared_value;

int main(void) { /* Exercise linkage extern redecl behavior. */
  return shared_value;
}
