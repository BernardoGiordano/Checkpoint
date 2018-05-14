#ifndef CHECKPOINT_LOG_HPP
#define CHECKPOINT_LOG_HPP

#include <fstream>
#include <string>

class Logger {
private:
    static Logger *instance;
    static const std::string filepath;

    // Set this to false if you don't want a log
    static const bool enabled = true;

    std::fstream out;

    Logger();
    ~Logger();

public:
    static Logger *getInstance();
    void print(std::string s);
};


#endif
