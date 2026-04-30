/*
 * wrong_prob2.cpp — Wrong Answer submission for Problem 2: Two Sum.
 * Bug: always prints YES regardless of whether a valid pair exists.
 * Expected: WRONG ANSWER verdict.
 */
#include <iostream>
#include <vector>
using namespace std;

int main() {
    int n;
    cin >> n;
    vector<int> nums(n);
    for (int i = 0; i < n; ++i) cin >> nums[i];
    int k;
    cin >> k;
    // BUG: always outputs YES
    cout << "YES" << endl;
    return 0;
}
