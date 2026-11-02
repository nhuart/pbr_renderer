#pragma once

#include <chrono>
#include <iostream>
#include <string_view>

class LoadingTimer {
public:
    void log(std::string_view label) const {
        auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - mStart);
        std::cout << label << ": " << elapsed.count() << " ms\n";
    }

private:
    std::chrono::steady_clock::time_point mStart = std::chrono::steady_clock::now();
};
