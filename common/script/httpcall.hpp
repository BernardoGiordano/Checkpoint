/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2026 Bernardo Giordano, FlagBrew
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#ifndef HTTPCALL_HPP
#define HTTPCALL_HPP

#include <string>

// The single HTTP transfer of the script engine: a request value in, a response
// value out. web_get, web_request, web_upload_file and url_encode are argument
// marshalling on top of it, so the option set cannot drift between them and the
// abort seam, the progress bar and the allocation-safe write callback are stated
// once.
//
// Runs on the script worker thread only: the transfer polls
// ckpt_script_abort_requested() from curl's progress callback (the per-statement
// kill switch cannot reach a thread parked inside curl_easy_perform), and an
// upload drives ScriptConsole's reserved innermost bar.
namespace Http {
    enum class Result {
        Ok,             // the transfer completed; httpStatus is meaningful
        Unavailable,    // curl_global_init/curl_easy_init failed
        FileError,      // uploadPath could not be opened or sized
        TransferFailed, // curl returned an error; see code
        OutOfMemory,    // the response did not fit in memory (CURLE_WRITE_ERROR)
    };

    struct Request {
        // nullptr/"" leaves curl's default verb (GET, or PUT when uploadPath is
        // set); anything else becomes CURLOPT_CUSTOMREQUEST.
        const char* method = nullptr;
        const char* url    = nullptr;
        // "\n"-separated "Key: Value" lines; empty lines and a trailing newline
        // are skipped.
        const char* headers = nullptr;
        // In-memory request body, ignored when uploadPath is set.
        const char* body = nullptr;
        long bodySize    = 0;
        // Streams this file as the request body (curl's default fread reader, so
        // a multi-MB save never enters the interpreter heap) and drives the
        // console's io progress bar for its duration.
        const char* uploadPath = nullptr;
        // Off for uploads: following a redirect would re-send the body without
        // rewinding the stream.
        bool followRedirects = true;
        // Collect the response headers into Response::headers.
        bool captureHeaders = false;
    };

    struct Response {
        Result result   = Result::Ok;
        int code        = 0; // CURLcode, meaningful when result == TransferFailed
        long httpStatus = 0;
        std::string body;
        std::string headers;
    };

    // Never throws and never longjmps: safe to call with C++ scopes alive.
    Response perform(const Request& req);

    // Percent-encodes s. Empty string if curl is unavailable.
    std::string encode(const char* s);

    // curl_easy_strerror for a code carried in a Response.
    const char* strerror(int code);
}

#endif
