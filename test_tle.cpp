/*
 * test_tle.cpp
 * Sample submission with an infinite loop.
 * The sandbox's setrlimit(RLIMIT_CPU, 2s) will kill this
 * process, resulting in a TLE (Time Limit Exceeded) verdict.
 */
#include <iostream>
using namespace std;
int main() {
    while (true) {
        // Infinite loop — will be killed by SIGXCPU
    }
    return 0;
}
