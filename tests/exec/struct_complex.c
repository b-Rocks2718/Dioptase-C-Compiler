// Purpose: Define named constants for struct/array/pointer test data.
// Inputs/Outputs: Constants are consumed by initializers and loops.
// Invariants/Assumptions: Values are small signed integers.
#define kDataLen 4
#define kSeed 10
#define kIndexMid 2
#define kIndexPick 1
#define kGlobal0 1
#define kGlobal1 2
#define kGlobal2 3
#define kGlobal3 4

// Purpose: Hold packet data for struct/array/pointer behavior.
// Inputs/Outputs: data stores integers; cursor selects an element.
// Invariants/Assumptions: cursor is either kZero or points into data.
struct Packet {
  int data[kDataLen];
  int* cursor;
};

// Purpose: Provide global storage to exercise static struct initialization.
// Inputs/Outputs: data values seed global accumulation; cursor is initialized to kZero.
// Invariants/Assumptions: data length is kDataLen.
struct Packet g_packet = { {kGlobal0, kGlobal1, kGlobal2, kGlobal3}, 0 };

// Purpose: Fill a packet with a sequence and set its cursor.
// Inputs: pkt is the target packet; seed is the base value.
// Outputs: Returns the value at kIndexMid after filling.
// Invariants/Assumptions: pkt is non-NULL and has kDataLen elements.
int fill_packet(struct Packet* pkt, int seed) {
  int i = 0;
  for (i = 0; i < kDataLen; i = i + 1) {
    pkt->data[i] = seed + i;
  }
  pkt->cursor = &pkt->data[kIndexMid];
  return *pkt->cursor;
}

// Purpose: Sum all elements in a packet.
// Inputs: pkt is the packet to sum.
// Outputs: Returns the sum of pkt->data.
// Invariants/Assumptions: pkt is non-NULL and has kDataLen elements.
int sum_packet(struct Packet* pkt) {
  int i = 0;
  int sum = 0;
  for (i = 0; i < kDataLen; i = i + 1) {
    sum = sum + pkt->data[i];
  }
  return sum;
}

// Purpose: Drive struct initialization, pointer fields, and array access.
// Inputs/Outputs: None.
// Invariants/Assumptions: Uses global and local packets for aggregation.
int main(void) {
  struct Packet local = { {0, 0, 0, 0}, 0 };
  int mid = fill_packet(&local, kSeed);
  int total = sum_packet(&g_packet);
  g_packet.cursor = &g_packet.data[kIndexPick];
  return mid + total + *g_packet.cursor;
}
