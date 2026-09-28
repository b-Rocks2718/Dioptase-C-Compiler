/*
 * Verify that -cg marks direct recursion and recursion through a longer call
 * chain while leaving acyclic functions unmarked.
 */

int chain_a(int value);
int chain_b(int value);
int chain_c(int value);

int direct(int value) {
    if (value == 0) {
        return 0;
    }
    return direct(value - 1);
}

int chain_a(int value) {
    if (value == 0) {
        return 0;
    }
    return chain_b(value - 1);
}

int chain_b(int value) {
    if (value == 0) {
        return 0;
    }
    return chain_c(value - 1);
}

int chain_c(int value) {
    if (value == 0) {
        return 0;
    }
    return chain_a(value - 1);
}

int nonrecursive(int value) {
    return value + 1;
}

int main(void) {
    return direct(2) + chain_a(3) + nonrecursive(4);
}
