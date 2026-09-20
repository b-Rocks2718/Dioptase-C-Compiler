int main(void) { /* Exercise local arr behavior. */
  int arr[4] = {1, 2};
  int matrix[2][3] = {{1, 2}, {3}};
  static int tentatize[6][7];
  return arr[3] + matrix[1][2];
}
