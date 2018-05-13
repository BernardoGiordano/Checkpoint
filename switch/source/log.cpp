//
// Created by lovingyoung on 18-5-13.
//

#include <log.hpp>

Logger *Logger::getInstance() {
    if (instance == 0) {
        instance = new Logger();
    }

    return instance;
}

Logger::Logger() {
    if(enabled) out.open(filepath, std::ios::app | std::ios::out);
}

Logger::~Logger() {
    if(enabled) out.close();
}

void Logger::print(std::string s) {
    if(enabled) out << s << std::endl;
}

const std::string Logger::filepath = "Checkpoint/log.txt";
Logger* Logger::instance = 0;
