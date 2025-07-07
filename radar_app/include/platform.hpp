// include/platform.hpp

#pragma once

#include <memory>

@class AppDelegate;

class Platform {
public:
    Platform();
    ~Platform();
    void run();
private:
    AppDelegate* m_delegate = nullptr;
};
