
int main(void){ /* Exercise stmt expr behavior. */
  int result = ({ int x = 5; 
    for (int i = 0; ; i++) { 
      x += i; 
      if (i >= 10) break;
    }
    x + 10; 
  });
  return result;
}
