void do_it(void) { /* Provide the void call used by the statement-expression test. */
}

int main(void) { /* Exercise stmt expr void value behavior. */
  int x = ({ do_it(); });
  return x;
}
