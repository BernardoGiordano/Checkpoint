/*  This file is part of Checkpoint
>	Copyright (C) 2017 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "datetime.h"

std::string getDate(void)
{
	char buf[80];
	time_t unixTime = time(NULL);
	struct tm* timeStruct = gmtime((const time_t*)&unixTime);
	
	sprintf(buf, "%i-%i-%i", timeStruct->tm_mday, timeStruct->tm_mon + 1, timeStruct->tm_year + 1900);
	
	std::string ret(buf);
	return ret;
}

std::string getTime(void)
{
	char buf[80];
	time_t unixTime = time(NULL);
	struct tm* timeStruct = gmtime((const time_t*)&unixTime);
	
	sprintf(buf, "%02i:%02i:%02i", timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
	
	std::string ret(buf);
	return ret;
}

std::string getDateTime(void)
{
	return getDate() + " " + getTime();
}

std::string getPathDateTime(void)
{
	char buf[80];
	time_t unixTime = time(NULL);
	struct tm* timeStruct = gmtime((const time_t*)&unixTime);
	
	sprintf(buf, "%02i%02i%02i-%02i%02i%02i", timeStruct->tm_mday, timeStruct->tm_mon + 1, timeStruct->tm_year + 1900, timeStruct->tm_hour, timeStruct->tm_min, timeStruct->tm_sec);
	
	std::string ret(buf);
	return ret;	
}