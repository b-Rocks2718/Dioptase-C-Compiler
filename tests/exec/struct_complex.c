#define kDataLen 4

// Hold packet data for struct/array/pointer behavior.
struct Packet {
  int data[kDataLen];
  int* cursor;
};

// Provide global storage to exercise static struct initialization.
struct Packet g_packet = { {1, 2, 3, 4}, 0 };

// Fill a packet with a sequence and set its cursor.
// Returns the value at index 2 after filling.
int fill_packet(struct Packet* pkt, int seed) {
  int i = 0;
  for (i = 0; i < kDataLen; i = i + 1) {
    pkt->data[i] = seed + i;
  }
  pkt->cursor = &pkt->data[2];
  return *pkt->cursor;
}

// Sum all elements in a packet.
// Returns the sum of pkt->data.
int sum_packet(struct Packet* pkt) {
  int i = 0;
  int sum = 0;
  for (i = 0; i < kDataLen; i = i + 1) {
    sum = sum + pkt->data[i];
  }
  return sum;
}

// Drive struct initialization, pointer fields, and array access.
int main(void) {
  struct Packet local = { {0, 0, 0, 0}, 0 };
  int mid = fill_packet(&local, 10);
  int total = sum_packet(&g_packet);
  g_packet.cursor = &g_packet.data[1];
  return mid + total + *g_packet.cursor;
}
