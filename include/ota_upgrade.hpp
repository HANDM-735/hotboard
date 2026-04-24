#ifndef _OTA_UPGRADE_H_
#define _OTA_UPGRADE_H_

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <xconfig.hpp>
#include <mgr_upgrade.h>
#include <mgr_network.h>
#include <mgr_device.h>
#include <xcrypto.hpp>
#include "mgr_log.h"
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mongoose.h"

#ifdef __cplusplus
}
#endif

using json = nlohmann::json;
using namespace std;
static std::string g_filepath = "";
static std::string g_filename = "";
static std::map<int, std::pair<std::string, std::string>> upgrade_info;
static std::map<int, std::pair<std::string, int>> upgrade_progress;
static std::mutex mtx;
std::atomic<bool> upgrade_running(false);

// MD5 计算函数（纯 C++11 实现）
class MD5 {
private:
    uint32_t h0, h1, h2, h3;

    // 左旋转函数
    uint32_t left_rotate(uint32_t x, uint32_t n) {
        return (x << n) | (x >> (32 - n));
    }

    // MD5 基本函数
    uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

    // 处理一个 512 位块
    void process_block(const uint8_t* block) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++) {
            w[i] = (block[i*4]) | (block[i*4+1] << 8) | (block[i*4+2] << 16) | (block[i*4+3] << 24);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3;

        // 第1轮
        for (int i = 0; i < 16; i++) {
            uint32_t f = F(b, c, d);
            uint32_t g = i;
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + left_rotate((a + f + 0x5A827999 + w[g]), 7);
            a = temp;
        }

        // 第2轮
        for (int i = 0; i < 16; i++) {
            uint32_t f = G(b, c, d);
            uint32_t g = (5*i + 1) % 16;
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + left_rotate((a + f + 0x6ED9EBA1 + w[g]), 12);
            a = temp;
        }

        // 第3轮
        for (int i = 0; i < 16; i++) {
            uint32_t f = H(b, c, d);
            uint32_t g = (3*i + 5) % 16;
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + left_rotate((a + f + 0x8F1BBCDC + w[g]), 17);
            a = temp;
        }

        // 第4轮
        for (int i = 0; i < 16; i++) {
            uint32_t f = I(b, c, d);
            uint32_t g = (7*i) % 16;
            uint32_t temp = d;
            d = c;
            c = b;
            b = b + left_rotate((a + f + 0xCA62C1D6 + w[g]), 22);
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
    }

public:
    MD5() {
        reset();
    }

    void reset() {
        h0 = 0x01234567;
        h1 = 0x89ABCDEF;
        h2 = 0xFEDCBA98;
        h3 = 0x76543210;
    }

    string calculate(const string& data) {
        reset();

        // 预处理：填充数据
        vector<uint8_t> padded_data(data.begin(), data.end());
        uint64_t original_bits = data.length() * 8;

        // 添加一个1位
        padded_data.push_back(0x80);

        // 填充0直到长度 mod 512 = 448
        while ((padded_data.size() * 8) % 512 != 448) {
            padded_data.push_back(0x00);
        }

        // 添加原始长度（以小端序）
        for (int i = 0; i < 8; i++) {
            padded_data.push_back((original_bits >> (i * 8)) & 0xFF);
        }

        // 处理每个512位块
        for (size_t i = 0; i < padded_data.size(); i += 64) {
            process_block(&padded_data[i]);
        }

        // 生成十六进制哈希值
        stringstream ss;
        ss << hex << setfill('0');
        ss << setw(8) << h0 << setw(8) << h1 << setw(8) << h2 << setw(8) << h3;
        return ss.str();
    }
};

//split string
static std::vector<std::string> split_single_char_delim(const std::string& input, char delim)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(input);
    while (std::getline(tokenStream, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

// 计算字符串的 MD5
string calculate_md5(const string& data) {
    MD5 md5;
    return md5.calculate(data);
}

void deleteDirectory(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        std::string fullPath = path + "/" + name;
        struct stat statbuf;
        if (stat(fullPath.c_str(), &statbuf) == -1) {
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            deleteDirectory(fullPath);
            rmdir(fullPath.c_str());
        } else {
            unlink(fullPath.c_str());
        }
    }
    closedir(dir);
}

bool createDirectory(const std::string& path) {
    return mkdir(path.c_str(), 0766) == 0; // Linux/Unix权限设置
}

// 检查目录是否存在
bool dirExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

bool checkDir(const std::string& path)
{
    std::string dirpath = path.substr(0, path.find_last_of("/\\"));
    if (!dirExists(dirpath)) {
        if (!createDirectory(dirpath)) {
            std::cerr << "Failed to create directory: " << dirpath << std::endl;
            return false;
        }
    }
    return true;
}

bool saveFile(const std::string& filepath, const std::vector<char>& data) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open file for writing: " << filepath << std::endl;
        return false;
    }
    file.write(data.data(), data.size());
    file.close();
    return !file.fail();
}

int parse_and_validate_formdata(const struct mg_str& body)
{
    std::map<std::string, std::string> text_fields;
    std::map<std::string, std::vector<char>> file_data;
    std::map<std::string, std::string> filenames;

    // 使用mongoose的multipart解析功能
    struct mg_http_part part;
    size_t ofs = 0;
    while ((ofs = mg_http_next_multipart(body, ofs, &part)) != 0) {
        std::string name(part.name.buf, part.name.len);

        if (part.filename.len > 0) {
            // 这是文件字段
            std::string filename(part.filename.buf, part.filename.len);
            std::vector<char> file_content(part.body.buf, part.body.buf + part.body.len);

            file_data[name] = file_content;
            filenames[name] = filename;
            std::cout << "Found file field: " << name
                << ", filename: " << filename
                << ", size: " << file_content.size() << " bytes" << std::endl;
        } else {
            // 这是文本字段
            std::string value(part.body.buf, part.body.len);
            text_fields[name] = value;
            std::cout << "Found text field: " << name << " = " << value << std::endl;
        }
    }

    if (file_data.find("file") == file_data.end() || filenames.find("file") == filenames.end() ||
        text_fields.find("slot") == text_fields.end() || text_fields.find("ip") == text_fields.end() ||
        text_fields.find("md5") == text_fields.end()) {
        std::cerr << "Form data is missing!" << std::endl;
        return -1;
    }

    std::vector<std::string> vct_slot = split_single_char_delim(text_fields["slot"], ',');
    std::vector<std::string> vct_ip = split_single_char_delim(text_fields["ip"], ',');
    if (vct_slot.size() != vct_ip.size()) {
        std::cerr << "The number of slots and IPs is inconsistent" << std::endl;
        return -1;
    }

    // 约束传固定名称开头的固件
    if (filenames["file"].find("RDBIHOT_MCU") == std::string::npos) {
        std::cerr << "The firmware name should be RDBIHOT_MCU_<version>.bin" << std::endl;
        return -1;
    }

    g_filename = filenames["file"];
    g_filepath = std::string(xbasic::get_module_path()) + "otafile/";

    // 计算MD5
    std::string tmp_data(file_data["file"].begin(), file_data["file"].end());
    // std::string actual_md5 = calculate_md5(tmp_data);
    std::string actual_md5 = xcrypto::get_md5(tmp_data);
    if (actual_md5.length() != 32) {
        std::cerr << "Update ota file md5 calculate error" << std::endl;
    }
    cout << "实际 MD5: " << actual_md5 << endl;
    // 传递 MD5
    std::string expected_md5 = text_fields["md5"];;
    // MD5 校验
    if (actual_md5 != expected_md5) {
        cerr << "MD5 校验失败！" << endl;
        cerr << "期望: " << expected_md5 << endl;
        cerr << "实际: " << actual_md5 << endl;
        return -1;
    }
    cout << "MD5 校验成功！" << endl;

    // 检查目录是否存在
    if (!checkDir(g_filepath)) {
        return -1;
    }

    // 删除目录下所有文件
    deleteDirectory(g_filepath);
    // 重新保存文件
    if (!saveFile(g_filepath + g_filename, file_data["file"])) {
        std::cerr << "Failed to save file: " << g_filename << std::endl;
        return -1;
    }
    std::cout << "File saved: " << g_filepath << g_filename << std::endl;

    // 生成出参
    {
        std::lock_guard<std::mutex> lock(mtx);
        upgrade_info.clear();
        for (int i = 0; i < vct_slot.size(); ++i) {
            int slot_id = (int)strtol(vct_slot[i].c_str(), nullptr, 0);
            upgrade_info.emplace(slot_id, std::make_pair(vct_ip[i], g_filename));
        }
        if (upgrade_info.empty()) {
            return -1;
        }
    }
    return 0;
}

static const char* soft_version()
{
#if defined(SOFT_VERSION) && defined(DEV_TYPE)
    const char* version_info = "version_module:" STR(SOFT_VERSION) "(" __DATE__ " " __TIME__ ")(" STR(DEV_TYPE) ")";
#elif defined(SOFT_VERSION)
    const char* version_info = "version_module:" STR(SOFT_VERSION) "(" __DATE__ " " __TIME__ ")(" "general" ")";
#elif defined(DEV_TYPE)
    const char* version_info = "version_module:" "unknown" "(" __DATE__ " " __TIME__ ")(" STR(DEV_TYPE) ")";
#else
    const char* version_info = "version_module:" "unknown" "(" __DATE__ " " __TIME__ ")(" "general" ")";
#endif
    return version_info;
}

static int get_loglevel(const std::string& loglevel)
{
    int level = MSG_LOG;
    if(loglevel == std::string("debug")) {
        level = MSG_LOG;
    } else if(loglevel == std::string("warning")) {
        level = WRN_LOG;
    } else if(loglevel == std::string("error")) {
        level = ERR_LOG;
    } else {
        level = MSG_LOG;
    }
    return level;
}

static int get_logmode(const std::string& logmode)
{
    int mode = LOG_MODE_NORMAL;
    if(logmode == std::string("normal")) {
        mode = LOG_MODE_NORMAL;
    } else if(logmode == std::string("debug")) {
        mode = LOG_MODE_DEBUG;
    } else if(logmode == std::string("verbose")) {
        mode = LOG_MODE_VERBOSE;
    } else {
        mode = LOG_MODE_NORMAL;
    }
    return mode;
}

static void load_config() //加载配置文件
{
    xini_config xini_cfg;
    xini_cfg.set_file(std::string(xbasic::get_module_path())+"config.ini");
    xconfig *sys_config = xconfig::get_instance();
    sys_config->set_data("debug",xini_cfg.get_data("SYS_CONFIG.debug",1));

    std::string loglevel = xini_cfg.get_data("SYS_CONFIG.log_level","debug");
    std::string logmode = xini_cfg.get_data("SYS_CONFIG.log_mode","normal");
    std::string logpath  = xini_cfg.get_data("SYS_CONFIG.log_path",xbasic::get_module_path());
    int hotsimulation_port = xini_cfg.get_data("SYS_HOTSIMULATION.hotsimulation_ip_addr","0.0.0.0");
    int hotsimulation_port = xini_cfg.get_data("SYS_HOTSIMULATION.hotsimulation_port",13083);
    int hotsimulation_timeout = xini_cfg.get_data("SYS_HOTSIMULATION.hotsimulation_timeout",30);
    std::string load_ip_from_config = xini_cfg.get_data("SYS_HOTSIMULATION.load_ip_from_config","yes");

    std::string ota_path =xini_cfg.get_data("SYS_CONFIG.ota_path",xbasic::get_module_path());
    if(ota_path.length() <= 0)  ota_path = xbasic::get_module_path();
    sys_config->set_data("ota_path",ota_path);
    sys_config->set_data("hotsimulation_ip_addr", hotsimulation_ip_addr);
    sys_config->set_data("hotsimulation_port", hotsimulation_port);
    sys_config->set_data("hotsimulation_timeout", hotsimulation_timeout);
    sys_config->set_data("load_ip_from_config", load_ip_from_config);

    if(logpath.length() <= 0) logpath = xbasic::get_module_path() + std::string("../log");
    sys_config->set_data("log_level",get_loglevel(loglevel));
    sys_config->set_data("log_mode",get_logmode(logmode));
    sys_config->set_data("log_path",logpath);

    std::string self_id_str = xini_cfg.get_data("SYS_CONFIG.self_id","");
    if(!self_id_str.empty())
    {
        int self_id_value = 0;
        xbasic::trim(self_id_str); //去掉前后的空白字符
        sscanf(self_id_str.c_str(),"%x",&self_id_value);
        sys_config->set_data("self_id",self_id_value);
    }
}

static void start_log()
{
    *log_mgr  = mgr_log::get_instance();
    *sys_config = xconfig::get_instance();

    //设置log
    std::string logprefix = std::string(sys_config->get_data("log_path"))+std::string("/")+std::string("HOTSIMULATIONDEVICEIGMGR");
    std::string log_filename = std::string(sys_config->get_data("log_path"))+std::string("/")+std::string("hotsimulationdeviceegr.log");
    int loglevel = std::stoi(sys_config->get_data("log_level"));
    int logmode = std::stoi(sys_config->get_data("log_mode"));
    printf("log_filename=%s logmode=%d logprefix=%s\n",log_filename.c_str(),loglevel,logprefix.c_str());
    log_mgr->log_config(log_filename.c_str(), logprefix.c_str(), loglevel, logmode, 50000);
    //设置log工作线程工作周期为1毫秒
    int work_cycle_ms = 1;
    log_mgr->start_work(work_cycle_ms);
    ussleep(10*1000);
    return ;
}

static void stop_log()
{
    log_mgr = mgr_log::get_instance();
    log_mgr->stop_work();
    ussleep(10*1000);
    log_mgr->release_instance();
    return ;
}

static int get_ota_type_param(const std::string str)
{
    if(str == std::string("FTTH_MCU"))
    {
        return OTA_TYPE_FTTH_MCU;
    }
    else if(str == std::string("FTMF_MCU"))
    {
        return OTA_TYPE_FTMF_MCU;
    }
    else if(str == std::string("RDBIHOT_MCU"))
    {
        return OTA_TYPE_RDBIHOT_MCU;
    }
    else
    {
        return -1;
    }
    return -1;
}

static int test_start_ota(const std::string boardid, std::string otatype_str)
{
    int ret = 0;
    xbasic::debug_output("enter into test_start_ota()\n");
    mgr_device  *device_mgr = mgr_device::get_instance();
    int board_id = std::stoi(boardid);
    std::transform(otatype_str.begin(),otatype_str.end(),otatype_str.begin(),::toupper); //将ota type参数转换成大写
    int ota_type = get_ota_type_param(otatype_str);
    if(ota_type == -1)
    {
        xbasic::debug_output("test_start_ota() ota type is failed!\n");
        ret = -1;
        return ret;
    }
    boost::shared_ptr<xmbboard> board = device_mgr->find_board(board_id);
    mgr_upgrade *upgrade_mgr = mgr_upgrade::get_instance();
    upgrade_mgr->load_otafiles();
    if(board != nullptr)
    {
        int ret = board->send_ota_start(board_id,ota_type);
        xbasic::debug_output("test_start_ota() send_ota_start(%d, %d) ret:%d\n", board_id, ota_type, ret);
    }
    else
    {
        xbasic::debug_output("test_start_ota() board:%d not find\n", board_id);
        ret = -1;
    }
    xbasic::debug_output("exited test_start_ota()\n");
    return ret;
}

static void test_cancel_ota(const std::string boardid, std::string otatype_str)
{
    xbasic::debug_output("enter into test_cancel_ota()\n");
    mgr_device  *device_mgr = mgr_device::get_instance();
    int board_id = std::stoi(boardid);
    std::transform(otatype_str.begin(),otatype_str.end(),otatype_str.begin(),::toupper); //将ota type参数转换成大写
    int ota_type = get_ota_type_param(otatype_str);
    if(ota_type == -1)
    {
        xbasic::debug_output("test_cancel_ota() ota type is failed!\n");
        return ;
    }
    boost::shared_ptr<xmbboard> board = device_mgr->find_board(board_id);
    int ret = board->send_ota_cancel(board_id,ota_type);
    xbasic::debug_output("exited test_cancel_ota() ret=%d\n",ret);
}

int ota_query_progress(const std::string& boardid, float* progress)
{
    int ret = 0;
    int board_id = std::stoi(boardid);
    mgr_device  *device_mgr = mgr_device::get_instance();
    boost::shared_ptr<xmbboard> board = device_mgr->find_board(board_id);
    if(board != nullptr)
    {
        boost::shared_ptr<ota_session> ota_session = board->get_ota_session();
        int total_len = 0XFFFFFFFF; // 总长度
        int trans_len = 0; // 已经传输的长度
        if(ota_session)
        {
            // 存在
            trans_len = ota_session->get_ota_transed_len();
            total_len = ota_session->get_ota_total_len();
            xbasic::debug_output("total_len:%d, trans_len:%d\n", total_len, trans_len);
            *progress = (total_len == 0 ? 0 : trans_len/static_cast<float>(total_len));
        }
        else
        {
            xbasic::debug_output("ota_query_progress() ota session is not existed!\n");
            ret = -1;
        }
    }
    else
    {
        xbasic::debug_output("ota_query_progress() board:%d not find\n", board_id);
        ret = -1;
    }
    return ret;
}

void ota()
{
    const char* prog_path = xbasic::get_module_path();
    std::string logo_file = std::string(prog_path)+"logo.txt";
    std::string logo_text = xbasic::read_data_from_file(logo_file);
    printf("%s\nInput command, '?' for help:\n",logo_text.c_str());

    load_config(); //加载配置文件
    *sys_config = xconfig::get_instance();
    //启动日志模块必须是第一个模块
    start_log();

    mgr_upgrade *upgrade_mgr = mgr_upgrade::get_instance();
    mgr_network *network_mgr = mgr_network::get_instance();
    //设置ota文件目录
    upgrade_mgr->set_ota_path(sys_config->get_data("ota_path"));
    network_mgr->start_work();
    mgr_device  *device_mgr = mgr_device::get_instance();
    device_mgr->start_work();

    LOG_MSG(WRN_LOG,"main) program path:%s",prog_path);
    LOG_MSG(WRN_LOG,"========== print config.ini info ==========");
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.debug=%s",sys_config->get_data("debug").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.log_level=%s",sys_config->get_data("log_level").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.log_mode=%s",sys_config->get_data("log_mode").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.log_path=%s",sys_config->get_data("log_path").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.ota_path=%s",sys_config->get_data("ota_path").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_CONFIG.load_ip_from_config=%s",sys_config->get_data("load_ip_from_config").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_HOTSIMULATION.hotsimulation_ip_addr=%s",sys_config->get_data("hotsimulation_ip_addr").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_HOTSIMULATION.hotsimulation_port=%s",sys_config->get_data("hotsimulation_port").c_str());
    LOG_MSG(WRN_LOG,"main) SYS_HOTSIMULATION.hotsimulation_timeout=%s",sys_config->get_data("hotsimulation_timeout").c_str());
    LOG_MSG(WRN_LOG,"===========================================");

    while(1)
    {
        printf("$hotsimulation-device-mgr:debug:%d>",xconfig::debug());
        fflush(stdin);
        char cmd_buff[512] = {0};
        while(!fgets(cmd_buff,sizeof(cmd_buff)-4,stdin)) {ssleep(2);} //获得终端输入
        std::string input_str(cmd_buff);
        xbasic::trim(input_str); //去掉前后的空白字符
        std::transform(input_str.begin(),input_str.end(),input_str.begin(),::tolower); //全部转小写
        std::vector<std::string> vct_param;
        xbasic::split_string(input_str,std::string(" "),&vct_param);
        if(input_str.length()==0) continue; //嵌入了空
        if(vct_param.size() >0 && vct_param[0].length() >0)
        {
            if(vct_param[0] == "?" || vct_param[0] == "help") //帮助
            {
                printf("\tquit\t\t\t\t--exit the program.\r\n");
            }
            else if(vct_param[0] == "start_ota")
            {
                if(vct_param.size() == 3)
                {
                    int ret = 0;
                    ret = test_start_ota(vct_param[1],vct_param[2]);
                    if(ret != 0) continue; // 说明启动升级有问题
                    while(true)
                    {
                        sleep(5);
                        float progress = 0.0;
                        ret = ota_query_progress(vct_param[1],&progress);
                        if(ret != 0) break; // 板子没找到或者ota升级会话存在问题
                        xbasic::debug_output("current upgrade progress value is %.2f.\n",progress);
                        if(progress >= 1.0)
                        {
                            xbasic::debug_output("ota upgrade is completed.\n");
                            break;
                        }
                    }
                }
            }
            else if(vct_param[0] == "cancel_ota")
            {
                if(vct_param.size() == 3)
                {
                    test_cancel_ota(vct_param[1],vct_param[2]);
                }
            }
            else if(vct_param[0] == "quit") //退出程序
            {
                //exit(0);
                break;
            }
            else
            {
                printf("%s --unknow command.input '?' for help.\n",vct_param[0].c_str());
            }
        }
        ussleep(10*1000);
    }
    ussleep(10*1000);
    network_mgr->stop_work();
    device_mgr->stop_work();
    mgr_upgrade::release_instance();
    mgr_network::release_instance();
    ussleep(10*1000);
    //停止日志模块
    stop_log();
}

void save_upgrade_progress(int slot_id, const std::string& ip, float progress)
{
    auto item = upgrade_progress.find(slot_id);
    if (item != upgrade_progress.end()) {
        item->second.first = ip;
        item->second.second = (int)(progress * 100);
        return;
    }
    upgrade_progress.emplace(slot_id, std::make_pair(ip, (int)(progress * 100)));
}

json get_upgrade_progress()
{
    json jsonData = json::array();
    std::lock_guard<std::mutex> lock(mtx);
    for (auto item : upgrade_progress) {
        json jtmp;
        jtmp["slotId"] = item.first;
        jtmp["IP"] = item.second.first;
        jtmp["completion"] = item.second.second;
        jsonData.push_back(jtmp);
    }
    return jsonData;
}

int wait_transmission_complete(int& flag)
{
    int ret = 0;
    std::lock_guard<std::mutex> lock(mtx);
    upgrade_progress.clear();
    for (auto item : upgrade_info)
    {
        float progress = 0.0;
        int ret = ota_query_progress(std::to_string(item.first), &progress);
        if (ret != 0) // 板子没找到或者ota升级会话存在问题
            continue;
        xbasic::debug_output("slotId %d ip %s filename %s current upgrade progress value is %.2f.\n", item.first, item.second.first.c_str(), item.second.second.c_str(), progress);
        if (progress >= 1.0) {
            flag |= 1; 
        }
        save_upgrade_progress(item.first, item.second.first, progress);
    }
    return 0;
}

static int ota_start()
{
    auto timeout_time = std::chrono::steady_clock::now() + std::chrono::minutes(3); // 超时时间3分钟
    const char* prog_path = xbasic::get_module_path();
    std::string logo_file = std::string(prog_path)+"logo.txt";
    std::string logo_text = xbasic::read_data_from_file(logo_file);
    printf("%s\n",logo_text.c_str());

    load_config();//加载配置文件
    xconfig  *sys_config = xconfig::get_instance();
    //启动日志模块必须是第一个模块
    start_log();

    mgr_upgrade *upgrade_mgr = mgr_upgrade::get_instance();
    mgr_network *network_mgr = mgr_network::get_instance();
    //设置ota文件目录
    upgrade_mgr->set_ota_path(g_filepath);
    network_mgr->start_work();
    mgr_device  *device_mgr = mgr_device::get_instance();
    device_mgr->start_work();

    LOG_MSG(WRN_LOG,"ota_start() program path:%s",prog_path);
    LOG_MSG(WRN_LOG,"========== print config.ini info ==========");
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.debug=%s",sys_config->get_data("debug").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.log_level=%s",sys_config->get_data("log_level").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.log_mode=%s",sys_config->get_data("log_mode").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.log_path=%s",sys_config->get_data("log_path").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.self_id=%s",sys_config->get_data("self_id").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.ota_path=%s",g_filepath.c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_CONFIG.load_ip_from_config=%s",sys_config->get_data("load_ip_from_config").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_HOTSIMULATION.hotsimulation_ip_addr=%s",sys_config->get_data("hotsimulation_ip_addr").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_HOTSIMULATION.hotsimulation_port=%s",sys_config->get_data("hotsimulation_port").c_str());
    LOG_MSG(WRN_LOG,"ota_start() SYS_HOTSIMULATION.hotsimulation_timeout=%s",sys_config->get_data("hotsimulation_timeout").c_str());
    LOG_MSG(WRN_LOG,"===========================================");

    int ret = 0;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto item : upgrade_info) {
            ret = network_mgr->set_network_connect(item.second.first); // 建连、加入client心跳监控
            if (ret != 0) {
                LOG_MSG(ERR_LOG, "ota_start() slotId:%d ip:%s filename:%s connect network failed!", item.first, item.second.first.c_str(), item.second.second.c_str());
                goto END;
            }
        }
    }

    while (1) {
        ussleep(100 * 1000); // 延时100ms等待建连成功
        bool connect = network_mgr->wait_network_complete();
        if (connect) {
            LOG_MSG(MSG_LOG, "ota_start() ALL IP connect SUCCESS!");
            break;
        }
        if (std::chrono::steady_clock::now() > timeout_time) {
            LOG_MSG(ERR_LOG, "ota_start() wait connect network timeout!");
            goto END;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto item : upgrade_info) {
            ret = test_start_ota(std::to_string(item.first), "RDBIHOT_MCU"); // 传输固件信息数据
            if (ret != 0) {
                LOG_MSG(ERR_LOG, "ota_start() slotId:%d ip:%s filename:%s send fileinfo failed!", item.first, item.second.first.c_str(), item.second.second.c_str());
                goto END;
            }
        }
    }

    while (1) {
        ussleep(100 * 1000); // 每100ms去查询进度一次进度
        int flag = 0;
        ret = wait_transmission_complete(flag);
        if (flag == 0) {
            LOG_MSG(MSG_LOG, "ota_start() ALL SLOT UPGRADE SUCCESS!");
            break; // 所有的单板数据都已经传输完成
        }

        if (std::chrono::steady_clock::now() > timeout_time) {
            LOG_MSG(ERR_LOG, "ota_start() wait transmission complete timeout!");
            goto END;
        }
    }

END:
    ussleep(100 * 1000); // 等待100ms之后再停止服务
    network_mgr->stop_work();
    network_mgr->stop_client();
    device_mgr->stop_work();
    ussleep(10 * 1000); // 等待10ms，等待stop_work完成
    mgr_upgrade::release_instance();
    mgr_network::release_instance();
    mgr_device::release_instance();
    ussleep(10*1000);
    //停止日志模块
    stop_log();
    return ret;
}

void ota_upgrade_process()
{
    int ret = 0;
    ret = ota_start();
    if (ret != 0) {
        std::cout << "OTA UPGRADE FAILED!!!" << std::endl;
    }
    upgrade_running.store(false);
    return;
}

int start_ota_upgrade(const struct mg_str& body)
{
    int ret = 0;
    if (!upgrade_running) {
        ret = parse_and_validate_formdata(body);
        if (ret != 0) {
            upgrade_running.store(false);
            std::cout << "OTA UPGRADE PARSE FAILED!!!" << std::endl;
            return -1;
        }

        upgrade_running.store(true);
        std::thread upgradeThread(ota_upgrade_process);
        upgradeThread.detach();
        return 0;
    } else {
        return -2;
    }
}

#endif