/*
 * solution_prob2.cpp
 * Solution for Problem ID 2: Two Sum
 */
#include <iostream>
#include <vector>
#include <unordered_set>

using namespace std;

int main() {
    int n;
    // Read N
    if (!(cin >> n)) return 0;
    
    // Read the N integers
    vector<int> nums(n);
    for (int i = 0; i < n; ++i) {
        cin >> nums[i];
    }
    
    int k;
    // Read target K
    cin >> k;
    
    // Check for Two Sum using a Hash Set
    unordered_set<int> seen;
    bool found = false;
    
    for (int i = 0; i < n; ++i) {
        int complement = k - nums[i];
        if (seen.count(complement)) {
            found = true;
            break;
        }
        seen.insert(nums[i]);
    }
    
    if (found) {
        cout << "YES" << endl;
    } else {
        cout << "NO" << endl;
    }
    
    return 0;
}
