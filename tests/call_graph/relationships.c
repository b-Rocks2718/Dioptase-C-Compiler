/*
 * Verify unique direct, recursive, and indirect call-graph relationships.
 * main calls leaf twice so duplicate-edge suppression is observable.
 */

int leaf(int value) {
    return value;
}

int recursive(int value) {
    if (value == 0) {
        return 0;
    }
    return recursive(value - 1);
}

int dispatch(int (*function)(int)) {
    return function(3);
}

int main(void) {
    leaf(1);
    leaf(2);
    recursive(2);
    return dispatch(leaf);
}
