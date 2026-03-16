#include <cstdio>

#include "Application.hpp"

int main() {
    Application app;
    if (!app.init()) {
        std::fprintf(stderr, "Failed to init application\n");
        return 1;
    }
    app.run();
    return 0;
}

