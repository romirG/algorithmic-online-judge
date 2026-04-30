/*
 * tle_prob2.cpp — TLE submission for Problem 2: Two Sum.
 * Bug: infinite loop — never terminates.
 * Expected: TLE verdict (killed by SIGXCPU after 2 seconds via setrlimit).
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
    // BUG: infinite loop — never exits
    while (true) {}
    return 0;
}
