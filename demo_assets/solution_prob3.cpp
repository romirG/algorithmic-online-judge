#include <iostream>
#include <string>
#include <algorithm>

using namespace std;

int main() {
    string s;
    if (cin >> s) {
        string rev = s;
        reverse(rev.begin(), rev.end());
        if (s == rev) cout << "true";
        else cout << "false";
    }
    return 0;
}
