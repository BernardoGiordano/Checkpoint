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

#include "http.h"

static const char* url = "https://api.titledb.com/v1/cia?only=titleid";

Result callTitleDB(std::string &json)
{
	Result ret = 0;
    httpcInit(0);
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
    if (ret != 0) {
		httpcCloseContext(&context);
        return ret;
    }

	ret = httpcAddRequestHeaderField(&context, "User-Agent", "Checkpoint");
    if (ret != 0) {
		httpcCloseContext(&context);
        return ret;
    }
	
	ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
	if (ret != 0) {
		httpcCloseContext(&context);
		return ret;
	}
	
	ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
	if (ret != 0) {
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcBeginRequest(&context);
    if (ret != 0) {
		httpcCloseContext(&context);
        return ret;
    }

	ret = httpcGetResponseStatusCode(&context, &statuscode);
    if (ret != 0) {
        httpcCloseContext(&context);
        return ret;
    }

    if (statuscode != 200) {
        if (statuscode >= 300 && statuscode < 400) {
            char newUrl[0x1000];
			
			ret = httpcGetResponseHeader(&context, (char*)"Location", newUrl, 0x1000);
            if (ret != 0) {
				httpcCloseContext(&context);
                return ret;
            }
			
            httpcCloseContext(&context);
            return callTitleDB(json);
        } else {
            httpcCloseContext(&context);
            return ret;
        }
    }

	ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (ret != 0) {
        httpcCloseContext(&context);
        return ret;
    }

    char *buf = (char*)malloc(contentsize);
    if (buf == NULL) {
		free(buf);
		httpcCloseContext(&context);
        return -1;
    }
    memset(buf, 0, contentsize);

	ret = httpcDownloadData(&context, (u8*)buf, contentsize, NULL);
    if (ret != 0) {
        free(buf);
        httpcCloseContext(&context);
        return ret;
    }
	
	std::string str(buf);
	json = str;
	
    free(buf);

    httpcCloseContext(&context);
    httpcExit();
    return 0;
}
