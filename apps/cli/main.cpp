#include <cstdio>
#include "dejaview/version.hpp"

int main() {
    std::printf("DejaView %s — finds the photos you've seen before\n",
                dejaview::version());
    return 0;
}
