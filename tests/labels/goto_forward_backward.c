int main(void){ /* Exercise goto forward backward behavior. */
  goto end;
start:
  goto end;
end:
  goto start;
  return 0;
}
