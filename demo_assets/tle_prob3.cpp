/*
 * tle_prob3.cpp — TLE submission for Problem 3: Valid Palindrome.
 * Bug: enters an infinite loop before any I/O — never terminates.
 * Expected: TLE verdict (killed by SIGXCPU after 2 seconds via setrlimit).
 */
#include <iostream>
#include <string>
using namespace std;

int main() {
    string s;
    cin >> s;
    // BUG: infinite loop — never exits
    while (true) {}
    return 0;
}
