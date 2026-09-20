// Define named constants for struct/array/pointer test data.
#define kDataLen 4
#define kSeed 10
#define kIndexMid 2
#define kIndexPick 1
#define kGlobal0 1
#define kGlobal1 2
#define kGlobal2 3
#define kGlobal3 4

// Hold packet data for struct/array/pointer behavior.
struct Packet {
  int data[kDataLen];
  int* cursor;
};

// Provide global storage to exercise static struct initialization.
struct Packet g_packet = { {kGlobal0, kGlobal1, kGlobal2, kGlobal3}, 0 };

// Fill a packet with a sequence and set its cursor.
// Returns the value at kIndexMid after filling.
int fill_packet(struct Packet* pkt, int seed) {
  int i = 0;
  for (i = 0; i < kDataLen; i = i + 1) {
    pkt->data[i] = seed + i;
  }
  pkt->cursor = &pkt->data[kIndexMid];
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
  int mid = fill_packet(&local, kSeed);
  int total = sum_packet(&g_packet);
  g_packet.cursor = &g_packet.data[kIndexPick];
  return mid + total + *g_packet.cursor;
}
