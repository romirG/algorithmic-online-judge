/*
 * wrong_prob3.cpp — Wrong Answer submission for Problem 3: Valid Palindrome.
 * Bug: performs a case-insensitive comparison, violating the problem's
 *      case-sensitive constraint. E.g., "Racecar" prints "true" but should be "false".
 * Expected: WRONG ANSWER verdict.
 */
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
using namespace std;

int main() {
    string s;
    if (cin >> s) {
        string lower = s;
        // BUG: converts to lowercase before comparing (case-insensitive)
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        string rev = lower;
        reverse(rev.begin(), rev.end());
        if (lower == rev) cout << "true";
        else cout << "false";
    }
    return 0;
}
