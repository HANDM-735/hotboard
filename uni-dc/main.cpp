// Copyright (c) 2020 Cesanta Software Limited
// All rights reserved

#include <iostream>
#include <atomic>
#include <signal.h>
#include <string>
#include <nlohmann/json.hpp>
#include "xboarddata.hpp"
#include "udpserver.h"
#include "ota_upgrade.hpp"

#define OTA_TEST
#include <xconfig.hpp>
#include <mgr_upgrade.h>
#include <mgr_network.h>
#include <mgr_device.h>
#include <mgr_log.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mongoose.h"

#ifdef __cplusplus
}
#endif

const char* APP_SOFT_VERSION = "version_module:V0.0.2";
const char* BUILD_TIME = "build_time:" __DATE__ " " __TIME__ "";

using json = nlohmann::json;

UDPServer* g_udp_server = nullptr;
std::atomic<bool> running(true);
std::atomic<bool> thread_running(false);

static int s_debug_level = MG_LL_INFO;
static const char *s_root_dir = ".";
static const char *s_addr1 = "http://0.0.0.0:11000";
static const char *s_addr2 = "https://0.0.0.0:8443";
static const char *s_enable_hexdump = "no";
static const char *s_ssi_pattern = "#.html";
static const char *s_upload_dir = NULL;  // File uploads disabled by default

// Self signed certificates, see
// https://github.com/cesanta/mongoose/blob/master/test/certs/generate.sh
#ifdef TLS_TWOWAY
static const char *s_tls_ca =
"-----BEGIN CERTIFICATE-----\n"
"MIIBfTCBvAIJAMNTFtpfcq8NMMAoGCCqGSM49BAMCMBMxETAPBgNVBAMMCE1vbvmdv\n"
"b3N1MB4XDTI0UxwNTZ0NzNl0NloXDTM0MDUwWTE0MzczNlowEzERMA8GA1UEAwwI\n"
"TW9uZ2VubW9ndW0TBgcqhkjOPQIBBggqhkjOPQMBBwNCAASuP+86T/rOwnGpEVhl\n"
"fxYZ+z9pjMcwDZ+vdnP0rjoxudwRMRQCv5slR1DK7Lxue761sdvqxWr0Ma6TFGTNg\n"
"epsRMAoGCCqGSM49BAMCA0gAMEUCIQCwb2CxuAKm51s81S6BIoy1IcandXSohnqs\n"
"us64BAA7QgIgGGtUrpkgFSS0oPB1CUG6YPHFVw42vTfpTC0ySwAS0M4=\n"
"-----END CERTIFICATE-----\n";
#endif
static const char *s_tls_cert =
"-----BEGIN CERTIFICATE-----\n"
"MIIBMTCB2aADAgECAgkAluqkgveuV/zUwCgYIKoZIzj0EAwIwEzERMA8GA1UEAwwI\n"
"TW9uZ2VubW9ndW0jQwNTA3MTQzNzMzNzMwNzMwNzIwMjARMQ8GA1UEAwwDQYD\n"
"VQQDDAZzZXJ2ZXJwIwTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASo3oEiG+BuTt5y\n"
"ZRyfwNr0C+SP+4M0RG2pYkb2v+ivbpf172NHkmXif/kbHXtgmSrn/PeTqiA8M+mg\n"
"BhYjDX+zoxgwFjAUBgNVHREEDTALgglsb2NbhbGhvc3QwCgYIKoZIzj0EAwIDRwAw\n"
"RAIgTXW9MITQSwzqbNTxUUdt9DcB+8pPUTbwZpiXcA26GMYICIBiYw+DSFMLHmkHF\n"
"+SU3NXW3gVCLN9ntD5DAx8LTG8sB\n"
"-----END CERTIFICATE-----\n";
static const char *s_tls_key =
"-----BEGIN EC PRIVATE KEY-----\n"
"MIIBCAKQEIAD8UASGxjUYIUZESNX/KPHK8hqlBuOgGMMAsZ49\n"
"AwEHUDoQAgE6NBhvq8y7ecmU8n8oa9Avkj/uDNERtqWd9r/or26X4u9jR53I1n\n"
"4hfxG17YlKqS/z3k6gpPPpoAWXhd/sw=\n"
"-----END EC PRIVATE KEY-----\n";

// Handle interrupts, like Ctrl-C
static int s_signo;
static void signal_handler(int signo)
{
    s_signo = signo;
}

void processUDPData(UDPServer& udp_server) {
    XBoardManager* BoardManager = XBoardManager::get_instance();
    while (running) {
        ReceivedData revdata = udp_server.get_received_data();
        if (revdata.is_valid == false) {
            printf("Invalid ReceivedData data!\n");
            continue;
        }
        BoardManager->OnUdpReceive(revdata.address, revdata.message.data(), revdata.message.size());
    }
}

void startSaveBibTemp(int boardid) {
    XBoardManager::get_instance()->startSaveBibTempData(boardid);
}

// Event handler for the listening connection.
// Simply serve static files from `s_root_dir`
static void cb(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT && c->fn_data != NULL)
    {
        struct mg_tls_opts opts;
        memset(&opts, 0, sizeof(opts));
#ifdef TLS_TWOWAY
        opts.ca = mg_str(s_tls_ca);
#endif
        opts.cert = mg_str(s_tls_cert);
        opts.key = mg_str(s_tls_key);
        mg_tls_init(c, &opts);
    }

    if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        std::string method_str(reinterpret_cast<char*>(hm->method.buf, hm->method.len));
        if ("POST" == method_str) {
            int slot_id = -1;
            int value = 0;
            std::string ip;
            XBoardManager* BoardManager = XBoardManager::get_instance();
            if (mg_match(hm->uri, mg_str("/Uni/SearchBoard"), NULL)) {
                MG_INFO(("Debug: post SerchBoard start:%ld\n", std::time(nullptr)));
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("ip") && jmsg["ip"].is_string()) {
                        ip = jmsg["ip"].get<std::string>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json ip\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg ip:%s", ip.c_str()));
                    if (BoardManager->SerchBoard(ip, value) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"SerchBoard successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
                MG_INFO(("Debug: post SerchBoard end:%ld\n", std::time(nullptr)));
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnWarm1"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d, value:%d", slot_id, value));
                    if (BoardManager->SendRequest(slot_id, 0x81, value) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnWarm1 successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnWarm2"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d, value:%d", slot_id, value));
                    if (BoardManager->SendRequest(slot_id, 0x82, value) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnWarm2 successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnSetReportCy"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d", slot_id));
                    if (BoardManager->SendRequest(slot_id, 0x83, value) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnResetI2c successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnResetI2c"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d", slot_id));
                    if (BoardManager->SendRequest(slot_id, 0x84, 0x02) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnResetI2c successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnSetVol"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d %d", slot_id, value));
                    if (BoardManager->SendRequest(slot_id, 0x85, value*10) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnSetVol successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/BtnSetBibVol"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d %d", slot_id, value));
                    if (BoardManager->SendRequest(slot_id, BIB_POWER_CONFIG, value * 10) != 0) {
                        MG_INFO(("Error: sendRequest failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Server SendRequest failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"BtnSetBibVol successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/GetBoardDate"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d", slot_id));
                    json jres = BoardManager->getJsonData(slot_id);
                    json response_body;
                    response_body["statusCode"] = "200";
                    response_body["message"] = "succeed";
                    response_body["data"] = jres;
                    std::string json_str = response_body.dump();
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", json_str.c_str());
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/SaveBibTemp"), NULL)) {
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (jmsg.contains("value") && jmsg["value"].is_number_integer()) {
                        value = jmsg["value"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json value\"}");
                        return;
                    }
                    MG_INFO(("dbg slot_id:%d %d", slot_id, value));
                    if (value == 0) {
                        if (thread_running) {
                            BoardManager->stopSaveBibTempData();
                            thread_running = false;
                            mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"stop SaveBibTemp successfully\"}");
                            return;
                        }
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"stop SaveBibTemp already\"}");
                    } else {
                        if (!thread_running) {
                            std::thread saveFileThread(startSaveBibTemp, slot_id);
                            saveFileThread.detach();
                            thread_running = true;
                            mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"start SaveBibTemp successfully\"}");
                        } else {
                            mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"start SaveBibTemp already\"}");
                        }
                    }
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/load_cal"), NULL)) {
                try {
                    struct mg_str* content_type_header = mg_http_get_header(hm, "Content-Type");
                    if (content_type_header == nullptr) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Message\"}");
                        return;
                    }
                    std::string content_type(content_type_header->buf, content_type_header->len);
                    // 检查是否为multipart/form-data
                    if (content_type.find("multipart/form-data") == std::string::npos) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\",\"message\":\"Content-Type does not multipart/form-data\"}");
                        return;
                    }
                    int ret = BoardManager->load_config_calibration_file(hm->body);
                    if (ret != 0) {    
                        mg_http_reply(c, 400, "Content-Type: application/json\r\n", "{\"statusCode\":\"400\",\"message\":\"LoadCalibrationConfig failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "{\"statusCode\":\"200\",\"message\":\"LoadCalibrationConfig successfully\"}");
                } catch (const json::parse_error& e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\",\"message\":\"Invalid Form-data\"}");
                } catch (const std::exception &e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\",\"message\":\"Unknow Error\"}");
                }
            }
            /* 以下数据处理只用于MCU固件升级 */
            else if (mg_match(hm->uri, mg_str("/Uni/upgrade"), NULL)) {
                try {
                    struct mg_str* content_type_header = mg_http_get_header(hm, "Content-Type");
                    if (content_type_header == nullptr) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Message\"}");
                        return;
                    }
                    std::string content_type(content_type_header->buf, content_type_header->len);
                    // 检查是否为multipart/form-data
                    if (content_type.find("multipart/form-data") == std::string::npos) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Content-Type does not multipart/form-data\"}");
                        return;
                    }
                    int ret = start_ota_upgrade(hm->body);
                    if (ret == 0) {
                        mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"Upgrade Success\"}");
                    } else if (ret == -1) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Upgrade Parse Erron\"}");
                    } else if (ret == -2) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Upgrade Is Running\"}");
                    }
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Form-data\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/ClearIP"), NULL)) {
                MG_INFO(("Debug: post ClearIP start:%ld\n", std::time(nullptr)));
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    if (jmsg.contains("ip") && jmsg["ip"].is_string()) {
                        ip = jmsg["ip"].get<std::string>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json ip\"}");
                        return;
                    }
                    if (jmsg.contains("slot_id") && jmsg["slot_id"].is_number_integer()) {
                        slot_id = jmsg["slot_id"].get<int>();
                    } else {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    MG_INFO(("ClearIP slot_id:%d IP:%s", slot_id, ip.c_str()));
                    if (BoardManager->clear_mcu_ipinfo(slot_id, ip) != 0) {
                        MG_INFO(("Error: Clear MCU IP failed\n"));
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Clear MCU IP failed\"}");
                        return;
                    }
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"statusCode\":\"200\"},{\"message\":\"Clear MCU IP successfully\"}");
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid JSON\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
                MG_INFO(("Debug: post ClearIP end:%ld\n", std::time(nullptr)));
            } else if (mg_match(hm->uri, mg_str("/eeprom/read"), NULL)) {
                MG_INFO(("Debug: post eeprom/read start:%ld\n", std::time(nullptr)));
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    
                    if (!jmsg.contains("ip") || !jmsg["ip"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json ip\"}");
                        return;
                    }
                    if (!jmsg.contains("slot_id") || !jmsg["slot_id"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (!jmsg.contains("data") || !jmsg["data"].is_array()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json data\"}");
                        return;
                    }
                    
                    ip = jmsg["ip"].get<std::string>();
                    int boardId = std::stoi(jmsg["slot_id"].get<std::string>());
                    if (boardId < 0) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid slot_id\"}");
                        return;
                    }
                    
                    auto& data_arr = jmsg["data"];
                    std::vector<uint32_t> addresses, lengths;
                    for (auto& item : data_arr) {
                        if (!item.contains("address") || !item.contains("length")) {
                            mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"400\",\"message\":\"Invalid data item, need address and length\"}");
                            return;
                        }
                        uint32_t addr = std::stoul(item["address"].get<std::string>(), nullptr, 16);
                        uint32_t len = item["length"].get<uint32_t>();
                        if (addr > 0xFFFF || addr + len > 0x10000) {
                            mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"400\",\"message\":\"Address out of range (0x0000-0xFFFF)\"}");
                            return;
                        }
                        addresses.push_back(addr);
                        lengths.push_back(len);
                    }
                    
                    int count = addresses.size();
                    if (BoardManager->ReadEeprom(boardId, addresses, lengths, count) != 0) {
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"500\",\"message\":\"Send Eeprom read request failed\"}");
                        return;
                    }
                    
                    EepromReadCache result;
                    if (BoardManager->WaitEepromReadResult(boardId, result, 5000)) {
                        json response_data = json::array();
                        for (auto& entry : result.entries) {
                            json item;
                            char addr_str[16];
                            snprintf(addr_str, sizeof(addr_str), "%08X", entry.address);
                            item["address"] = std::string(addr_str);
                            item["data"] = entry.data;
                            response_data.push_back(item);
                        }
                        json response_body;
                        response_body["statusCode"] = "200";
                        response_body["message"] = "read success";
                        response_body["data"] = response_data;
                        std::string json_str = response_body.dump();
                        mg_http_reply(c, 200, "Content-Type:application/json\r\n", json_str.c_str());
                    } else {
                        mg_http_reply(c, 504, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"504\",\"message\":\"Eeprom read timeout\"}");
                    }
                } catch (const json::parse_error& e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"400\",\"message\":\"Invalid JSON\"}");
                } catch (const std::exception& e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"500\",\"message\":\"Unknown Error\"}");
                }
                MG_INFO(("Debug: post eeprom/read end:%ld\n", std::time(nullptr)));
                        } else if (mg_match(hm->uri, mg_str("/eeprom/write"), NULL)) {
                MG_INFO(("Debug: post eeprom/write start:%ld\n", std::time(nullptr)));
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    
                    if (!jmsg.contains("ip") || !jmsg["ip"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json ip\"}");
                        return;
                    }
                    if (!jmsg.contains("slot_id") || !jmsg["slot_id"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (!jmsg.contains("data") || !jmsg["data"].is_array()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json data\"}");
                        return;
                    }
                    
                    ip = jmsg["ip"].get<std::string>();
                    int boardId = std::stoi(jmsg["slot_id"].get<std::string>());
                    if (boardId < 0) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid slot_id\"}");
                        return;
                    }
                    
                    auto& data_arr = jmsg["data"];
                    std::vector<uint32_t> addresses;
                    std::vector<std::string> hexData;
                    for (auto& item : data_arr) {
                        if (!item.contains("address") || !item.contains("data")) {
                            mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"400\",\"message\":\"Invalid data item, need address and data\"}");
                            return;
                        }
                        uint32_t addr = std::stoul(item["address"].get<std::string>(), nullptr, 16);
                        std::string data = item["data"].get<std::string>();
                        if (addr > 0xFFFF) {
                            mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"400\",\"message\":\"Address out of range (0x0000-0xFFFF)\"}");
                            return;
                        }
                        if (data.length() % 2 != 0) {
                            mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"400\",\"message\":\"Data length must be even (hex string)\"}");
                            return;
                        }
                        addresses.push_back(addr);
                        hexData.push_back(data);
                    }
                    
                    int count = addresses.size();
                    if (BoardManager->WriteEeprom(boardId, addresses, hexData, count) != 0) {
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"500\",\"message\":\"Send Eeprom write request failed\"}");
                        return;
                    }
                    
                    EepromWriteResponse result;
                    if (BoardManager->WaitEepromWriteResult(boardId, result, 5000)) {
                        if (result.error_code == 0) {
                            mg_http_reply(c, 200, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"200\",\"message\":\"write success\"}");
                        } else {
                            json response_body;
                            response_body["statusCode"] = "500";
                            response_body["message"] = "write failed: " + result.error_msg;
                            std::string json_str = response_body.dump();
                            mg_http_reply(c, 500, "Content-Type:application/json\r\n", json_str.c_str());
                        }
                    } else {
                        mg_http_reply(c, 504, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"504\",\"message\":\"Eeprom write timeout\"}");
                    }
                } catch (const json::parse_error& e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"400\",\"message\":\"Invalid JSON\"}");
                } catch (const std::exception& e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"500\",\"message\":\"Unknown Error\"}");
                }
                MG_INFO(("Debug: post eeprom/write end:%ld\n", std::time(nullptr)));
                        } else if (mg_match(hm->uri, mg_str("/eeprom/erase"), NULL)) {
                MG_INFO(("Debug: post eeprom/erase start:%ld\n", std::time(nullptr)));
                try {
                    std::string body_str(reinterpret_cast<char*>(hm->body.buf, hm->body.len));
                    json jmsg = json::parse(body_str);
                    
                    if (!jmsg.contains("ip") || !jmsg["ip"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json ip\"}");
                        return;
                    }
                    if (!jmsg.contains("slot_id") || !jmsg["slot_id"].is_string()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json slot_id\"}");
                        return;
                    }
                    if (!jmsg.contains("data") || !jmsg["data"].is_array()) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid Json data\"}");
                        return;
                    }
                    
                    ip = jmsg["ip"].get<std::string>();
                    int boardId = std::stoi(jmsg["slot_id"].get<std::string>());
                    if (boardId < 0) {
                        mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"400\",\"message\":\"Invalid slot_id\"}");
                        return;
                    }
                    
                    auto& data_arr = jmsg["data"];
                    std::vector<uint32_t> addresses, lengths;
                    int count = data_arr.size();
                    
                    if (count == 0) {
                        addresses.push_back(0x00000000);
                        lengths.push_back(0x00010000);
                        count = 1;
                    } else {
                        for (auto& item : data_arr) {
                            if (!item.contains("address") || !item.contains("length")) {
                                mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                    "{\"statusCode\":\"400\",\"message\":\"Invalid data item, need address and length\"}");
                                return;
                            }
                            uint32_t addr = std::stoul(item["address"].get<std::string>(), nullptr, 16);
                            uint32_t len = item["length"].get<uint32_t>();
                            if (addr > 0xFFFF || addr + len > 0x10000) {
                                mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                                    "{\"statusCode\":\"400\",\"message\":\"Address out of range (0x0000-0xFFFF)\"}");
                                return;
                            }
                            addresses.push_back(addr);
                            lengths.push_back(len);
                        }
                    }
                    
                    if (BoardManager->EraseEeprom(boardId, addresses, lengths, count) != 0) {
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"500\",\"message\":\"Send Eeprom erase request failed\"}");
                        return;
                    }
                    
                    EepromEraseResponse result;
                    if (BoardManager->WaitEepromEraseResult(boardId, result, 5000)) {
                        if (result.error_code == 0) {
                            mg_http_reply(c, 200, "Content-Type:application/json\r\n", 
                                "{\"statusCode\":\"200\",\"message\":\"erase success\"}");
                        } else {
                            json response_body;
                            response_body["statusCode"] = "500";
                            response_body["message"] = "erase failed: " + result.error_msg;
                            std::string json_str = response_body.dump();
                            mg_http_reply(c, 500, "Content-Type:application/json\r\n", json_str.c_str());
                        }
                    } else {
                        mg_http_reply(c, 504, "Content-Type:application/json\r\n", 
                            "{\"statusCode\":\"504\",\"message\":\"Eeprom erase timeout\"}");
                    }
                } catch (const json::parse_error& e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"400\",\"message\":\"Invalid JSON\"}");
                } catch (const std::exception& e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", 
                        "{\"statusCode\":\"500\",\"message\":\"Unknown Error\"}");
                }
                MG_INFO(("Debug: post eeprom/erase end:%ld\n", std::time(nullptr)));
            } else {
                mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid uri\"}");
            }
        } else if ("GET" == method_str) {
            MG_INFO(("dbg GET"));
            XBoardManager* BoardManager = XBoardManager::get_instance();
            if (mg_match(hm->uri, mg_str("/Uni/SearchBoard"), NULL)) {
                MG_INFO(("dbg SerchBoard start:%ld\n", std::time(nullptr)));
                json jres = BoardManager->getAllJsonData();
                json response_body;
                response_body["statusCode"] = "200";
                response_body["message"] = "succeed";
                response_body["data"] = jres;
                std::string json_str = response_body.dump();
                mg_http_reply(c, 200, "Content-Type:application/json\r\n", json_str.c_str());
                MG_INFO(("dbg SerchBoard end:%ld\n", std::time(nullptr)));
            } else if (mg_match(hm->uri, mg_str("/Uni/GetVoutAlarm"), NULL)) {
                json jres = BoardManager->getAllVoutAlarm();
                json response_body;
                response_body["statusCode"] = "200";
                response_body["message"] = "succeed";
                response_body["data"] = jres;
                std::string json_str = response_body.dump();
                mg_http_reply(c, 200, "Content-Type:application/json\r\n", json_str.c_str());
            } else if (mg_match(hm->uri, mg_str("/Uni/DownloadBibTempFile"), NULL)) {
                const char *filepath = m_save_path.c_str();
                FILE *file = fopen(filepath, "rb");
                if (file == NULL) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"File not found\"}");
                } else {
                    fseek(file, 0, SEEK_END);
                    long filesize = ftell(file);
                    fseek(file, 0, SEEK_SET);
                    char *buffer = (char *)malloc(filesize + 1);
                    if (buffer == NULL) {
                        fclose(file);
                        mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"malloc buffer failed\"}");
                    } else {
                        fread(buffer, 1, filesize, file);
                        buffer[filesize] = '\0';
                        mg_printf(c,
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type:application/octet-stream\r\n"    //Content-Type: text/csv\r\n
                            "Content-Disposition: attachment; filename=\"zqdemouni.csv\"\r\n"
                            "\r\n"
                            "%ld\r\n",
                            filesize);
                        mg_printf(c, "%s", buffer);
                        free(buffer);
                        fclose(file);
                    }
                }
            } else if (mg_match(hm->uri, mg_str("/Uni/upgradeProgress"), NULL)) {
                try {
                    json jres = get_upgrade_progress();
                    json response_body;
                    response_body["statusCode"] = "200";
                    response_body["message"] = "succeed";
                    response_body["data"] = jres;
                    std::string json_str = response_body.dump();
                    mg_http_reply(c, 200, "Content-Type:application/json\r\n", json_str.c_str());
                } catch (const json::parse_error & e) {
                    mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid Form-data\"}");
                } catch (const std::exception & e) {
                    mg_http_reply(c, 500, "Content-Type:application/json\r\n", "{\"statusCode\":\"500\"},{\"message\":\"Unknow Error\"}");
                }
            } else {
                mg_http_reply(c, 400, "Content-Type:application/json\r\n", "{\"statusCode\":\"400\"},{\"message\":\"Invalid uri\"}");
            }
        } else if (mg_match(hm->uri, mg_str("/Uni/gettest"), NULL)) {
            mg_http_reply(c, 200, "Content-Type:application/json\r\n", "{\"test_data\":\"salazar\",\"return\": 0}");
        }
        // Log request
        MG_INFO(("%.*s %.*s %.*s %.*s", hm->method.len, hm->method.buf, hm->uri.len, hm->uri.buf, hm->body.len, hm->body.buf, 9, c->send.buf + 9, c->send.len));
        //MG_INFO(("dbg %s", hm->body.buf));
    }
}

int main(int argc, char *argv[])
{
    char path[MG_PATH_MAX] = ".";
    struct mg_mgr mgr;
    struct mg_connection *c;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    boost::asio::io_service io_service;
    UDPServer udp_server(io_service, LOCAL_PORT);
    g_udp_server = &udp_server;
    udp_server.start();
    MG_INFO(("argc:%d argv:%s, %s", argc, argv[0], argv[1]));
    if (argc == 2 && strcmp(argv[1], "test_udp") == 0) {
        sleep(1);
        MG_INFO(("test udp server!!!"));
        udp_server.self_test();
        udp_server.stop();
        return 0;
    } else if (argc == 2 && strcmp(argv[1], "ota") == 0) {
        // 进入 ota 相关的操作
        ota();
        udp_server.stop();
        return 0;
    }
    std::thread processing_thread(processUDPData, std::ref(udp_server));
    MG_INFO(("Configure http"));
    // Root directory must not contain double dots. Make it absolute
    // Do the conversion only if the root dir spec does not contain overrides
    if (strchr(s_root_dir, ':') == NULL)
    {
        realpath(s_root_dir, path);
        s_root_dir = path;
    }
    // Initialise stuff
    mg_log_set(s_debug_level);
    mg_mgr_init(&mgr);
    if ((c = mg_http_listen(&mgr, s_addr1, cb, NULL)) == NULL)
    {
        MG_ERROR(("Cannot listen on %s. Use http://ADDR:PORT or :PORT", s_addr1));
        exit(EXIT_FAILURE);
    }
    if ((c = mg_http_listen(&mgr, s_addr2, cb, (void *)1)) == NULL)
    {
        MG_ERROR(("Cannot listen on %s. Use http://ADDR:PORT or :PORT", s_addr2));
        exit(EXIT_FAILURE);
    }
    if (mg_casecmp(s_enable_hexdump, "yes") == 0) c->is_hexdumping = 1;
    // Start infinite event loop
    MG_INFO(("Mongoose version : %s", MG_VERSION));
    MG_INFO(("HTTP listener    : %s", s_addr1));
    MG_INFO(("HTTPS listener   : %s", s_addr2));
    MG_INFO(("Web root         : [%s]", s_root_dir));
    MG_INFO(("Upload dir       : [%s]", s_upload_dir ? s_upload_dir : "unset"));
    while (s_signo == 0) mg_mgr_poll(&mgr, 1000);
    MG_INFO(("Exiting on signal %d", s_signo));
    running = false;
    udp_server.stop();
    if (processing_thread.joinable()) {
        processing_thread.join();
    }
    MG_INFO(("Exiting on signal2 %d", s_signo));
    mg_mgr_free(&mgr);
    MG_INFO(("Exiting on signal %d", s_signo));
    return 0;
}
