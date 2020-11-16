#include "datetime.hpp"
#include "stringutils.hpp"

#include <time.h>

std::string DateTime::timeStr()
{
    time_t unixTime;
    struct tm timeStruct;
    time(&unixTime);
    localtime_r(&unixTime, &timeStruct);
    return StringUtils::format("%02i:%02i:%02i", timeStruct.tm_hour, timeStruct.tm_min, timeStruct.tm_sec);
}
std::string DateTime::dateTimeStr()
{
    time_t unixTime;
    struct tm timeStruct;
    time(&unixTime);
    localtime_r(&unixTime, &timeStruct);
    return StringUtils::format("%04i%02i%02i-%02i%02i%02i", timeStruct.tm_year + 1900, timeStruct.tm_mon + 1, timeStruct.tm_mday, timeStruct.tm_hour,
        timeStruct.tm_min, timeStruct.tm_sec);
}
std::string DateTime::logDateTime()
{
    time_t unixTime;
    struct tm timeStruct;
    time(&unixTime);
    localtime_r(&unixTime, &timeStruct);
    return StringUtils::format("%04i-%02i-%02i %02i:%02i:%02i", timeStruct.tm_year + 1900, timeStruct.tm_mon + 1, timeStruct.tm_mday,
        timeStruct.tm_hour, timeStruct.tm_min, timeStruct.tm_sec);
}
