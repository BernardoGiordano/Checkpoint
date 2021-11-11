/*
 *   This file is part of Checkpoint
 *   Copyright (C) 2017-2019 Bernardo Giordano, FlagBrew
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

#include "configuration.hpp"

static struct mg_mgr mgr;
static struct mg_connection* nc;
static struct mg_serve_http_opts s_http_server_opts;
static const char* s_http_port = "8000";

static void handle_populate(struct mg_connection* nc, struct http_message* hm)
{
    (void)hm;
    // populate gets called at startup, assume a new connection has been started
    blinkLed(2);

    auto json          = Configuration::getInstance().getJson();
    auto map           = getCompleteTitleList();
    json["title_list"] = map;
    std::string body   = json.dump();
    mg_printf(nc, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n%.*s", (unsigned long)body.length(), (int)body.length(), body.c_str());
    Logger::getInstance().log(Logger::INFO, "A new Configuration connection has been handled.");
}

static void handle_save(struct mg_connection* nc, struct http_message* hm)
{
    FILE* f = fopen(Configuration::getInstance().BASEPATH.c_str(), "w");
    if (f != NULL) {
        fwrite(hm->body.p, 1, hm->body.len, f);
        fclose(f);
        Logger::getInstance().log(Logger::INFO, "Configurations have been updated.");
    }
    else {
        Logger::getInstance().log(Logger::ERROR, "Failed to write to configuration file with errno %d.", errno);
    }
    Configuration::getInstance().load();
    Configuration::getInstance().parse();
    // Send response
    mg_printf(nc, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n%.*s", (unsigned long)hm->body.len, (int)hm->body.len, hm->body.p);
}

static void ev_handler(struct mg_connection* nc, int ev, void* ev_data)
{
    struct http_message* hm = (struct http_message*)ev_data;

    switch (ev) {
        case MG_EV_HTTP_REQUEST:
            if (mg_vcmp(&hm->uri, "/save") == 0) {
                handle_save(nc, hm);
            }
            else if (mg_vcmp(&hm->uri, "/populate") == 0) {
                handle_populate(nc, hm);
            }
            else {
                mg_serve_http(nc, hm, s_http_server_opts);
            }
            break;
        default:
            break;
    }
}

Configuration::Configuration(void)
{
    // check for existing config.json files on the sd card, BASEPATH
    if (!io::fileExists(BASEPATH)) {
        store();
    }

    load();

    // check if json is valid
    if (!mJson.is_object()) {
        store();
    }

    bool updateJson = false;
    if (mJson.find("version") == mJson.end()) {
        // if config is present but is < 3.4.2, override it
        store();
    }
    else {
        if (mJson["version"] < CONFIG_VERSION) {
            mJson["version"] = CONFIG_VERSION;
            updateJson       = true;
        }
        if (!(mJson.contains("pksm-bridge") && mJson["pksm-bridge"].is_boolean())) {
            mJson["pksm-bridge"] = false;
            updateJson           = true;
        }
        if (!(mJson.contains("ftp-enabled") && mJson["ftp-enabled"].is_boolean())) {
            mJson["ftp-enabled"] = false;
            updateJson           = true;
        }
        if (!(mJson.contains("filter") && mJson["filter"].is_array())) {
            mJson["filter"] = nlohmann::json::array();
            updateJson      = true;
        }
        if (!(mJson.contains("favorites") && mJson["favorites"].is_array())) {
            mJson["favorites"] = nlohmann::json::array();
            updateJson         = true;
        }
        if (!(mJson.contains("additional_save_folders") && mJson["additional_save_folders"].is_object())) {
            mJson["additional_save_folders"] = nlohmann::json::object();
            updateJson                       = true;
        }
        // check every single entry in the arrays...
        for (auto& obj : mJson["filter"]) {
            if (!obj.is_string()) {
                mJson["filter"] = nlohmann::json::array();
                updateJson      = true;
                break;
            }
        }
        for (auto& obj : mJson["favorites"]) {
            if (!obj.is_string()) {
                mJson["favorites"] = nlohmann::json::array();
                updateJson         = true;
                break;
            }
        }
        for (auto& obj : mJson["additional_save_folders"]) {
            if (!obj.is_object()) {
                mJson["additional_save_folders"] = nlohmann::json::object();
                updateJson                       = true;
                break;
            }
        }
    }

    if (updateJson) {
        mJson["version"] = CONFIG_VERSION;
        save();
    }

    parse();

    // load server
    mg_mgr_init(&mgr, NULL);
    nc = mg_bind(&mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.document_root = "romfs:/web_root";
    s_http_server_opts.auth_domain   = "flagbrew.org";
}

Configuration::~Configuration(void)
{
    mg_mgr_free(&mgr);
}

void Configuration::store(void)
{
    FILE* in = fopen("romfs:/config.json", "rt");
    if (in != NULL) {
        nlohmann::json src = nlohmann::json::parse(in, nullptr, false);
        fclose(in);

        std::string writeData = src.dump(2);
        writeData.shrink_to_fit();
        size_t size = writeData.size();

        FILE* out = fopen(BASEPATH.c_str(), "wt");
        if (out != NULL) {
            fwrite(writeData.c_str(), 1, size, out);
            fclose(out);
        }
    }
}

bool Configuration::filter(u64 id)
{
    return mFilterIds.find(id) != mFilterIds.end();
}

bool Configuration::favorite(u64 id)
{
    return mFavoriteIds.find(id) != mFavoriteIds.end();
}

std::vector<std::string> Configuration::additionalSaveFolders(u64 id)
{
    std::vector<std::string> emptyvec;
    auto folders = mAdditionalSaveFolders.find(id);
    return folders == mAdditionalSaveFolders.end() ? emptyvec : folders->second;
}

bool Configuration::isPKSMBridgeEnabled(void)
{
    return PKSMBridgeEnabled;
}

void Configuration::pollServer(void)
{
    mg_mgr_poll(&mgr, 1000 / 60);
}

void Configuration::save(void)
{
    std::string writeData = mJson.dump(2);
    writeData.shrink_to_fit();
    size_t size = writeData.size();

    FILE* out = fopen(BASEPATH.c_str(), "wt");
    if (out != NULL) {
        fwrite(writeData.c_str(), 1, size, out);
        fclose(out);
    }
}

void Configuration::load(void)
{
    FILE* in = fopen(BASEPATH.c_str(), "rt");
    if (in != NULL) {
        mJson = nlohmann::json::parse(in, nullptr, false);
        fclose(in);
    }
}

void Configuration::parse(void)
{
    mFilterIds.clear();
    mFavoriteIds.clear();
    mAdditionalSaveFolders.clear();

    // parse filters
    std::vector<std::string> filter = mJson["filter"];
    for (auto& id : filter) {
        mFilterIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    // parse favorites
    std::vector<std::string> favorites = mJson["favorites"];
    for (auto& id : favorites) {
        mFavoriteIds.emplace(strtoull(id.c_str(), NULL, 16));
    }

    // parse additional save folders
    auto js = mJson["additional_save_folders"];
    for (auto it = js.begin(); it != js.end(); ++it) {
        std::vector<std::string> folders = it.value()["folders"];
        std::vector<std::string> sfolders;
        for (auto& folder : folders) {
            sfolders.push_back(folder);
        }
        mAdditionalSaveFolders.emplace(strtoull(it.key().c_str(), NULL, 16), sfolders);
    }

    // parse PKSM Bridge flag
    PKSMBridgeEnabled = mJson["pksm-bridge"];
    // parse FTP flag
    FTPEnabled = mJson["ftp-enabled"];
}

const char* Configuration::c_str(void)
{
    return mJson.dump().c_str();
}

nlohmann::json Configuration::getJson(void)
{
    return mJson;
}

bool Configuration::isFTPEnabled(void)
{
    return FTPEnabled;
}
