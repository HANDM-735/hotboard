#ifndef XBOARDDATA_H
#define XBOARDDATA_H

#include "xcrypto.hpp"
#include <unistd.h>
#include <fstream>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <cstddef>
#include <net/if.h>
#include <string>
#include <vector>
#include <iostream>
#include <vector>
#include <array>
#include <limits>
#include <string>
#include <ctime>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include "udpserver.h"

#ifdef __cplusplus
extern "C" {
#endif
 
#include "mongoose.h"

#ifdef __cplusplus
}
#endif

#define PGB_WARM_SW  0x81
#define BIB_WARM_SW  0x82
#define REPORT_CYCLE  0x83
#define PGB_POWER_CONFIG  0x85
#define BIB_POWER_CONFIG  0x86
#define EEPROM_READ   0x87
#define EEPROM_WRITE  0x88
#define EEPROM_ERASE  0x89

using json = nlohmann::json;

extern UDPServer* g_udp_server;
std::string m_save_path;

// EEPROM响应缓存结构
struct EepromReadEntry {
    uint32_t address;
    std::string data;
    std::time_t timestamp;
};

struct EepromReadCache {
    std::vector<EepromReadEntry> entries;
    std::time_t timestamp;
    bool ready;
    int error_code;
    std::string error_msg;
    std::condition_variable cv;

    EepromReadCache() : timestamp(0), ready(false), error_code(0), error_msg("") {}
};

struct EepromWriteResponse {
    int error_code;
    std::string error_msg;
    bool ready;
    std::condition_variable cv;

    EepromWriteResponse() : error_code(-1), error_msg(""), ready(false) {}
};

struct EepromEraseResponse {
    int error_code;
    std::string error_msg;
    bool ready;
    std::condition_variable cv;

    EepromEraseResponse() : error_code(-1), error_msg(""), ready(false) {}
};

struct DataRecord {
    std::string time;
    int slot;
    int BibId;
    std::vector<double> bib_temp;
    std::vector<double> thermocouple_temp;
    std::vector<double> bib_origin_temp;
    std::vector<double> thermocouple_origin_temp;
    std::vector<std::pair<double, double>> bib_temp_coefficient;
    std::vector<std::pair<double, double>> thermocouple_temp_coefficient;
};

struct XBoardData {
    int m_iPgbPowerVol;                                // PGB power电压
    int m_iBibPowerVol;                               // BIB power电压
    int m_iSlotId;                                    // slot id
    int m_iPgbId;                                     // PGB板号
    int m_iBibId;                                     // BIB板号
    int m_iPayState;                                  // 板负载状态
    int m_iReportCyc;                                 // 数据上报周期
    int m_iPgbWarm;                                   // PGB加热量
    int m_iBibWarm;                                   // BIB加热量
    int m_iaPgbSmokeTmp;                              // PGB烟感温度
    int m_iaPgbHumidity;                              // PGB湿度
    std::array<float, 2> m_iaBdVol;                   // 板子电压
    std::array<float, 2> m_iaBdpw;                    // 板子功率
    std::array<int, 8> m_iaPgbTmp_lm;                 // PGBLM75B传感器温度
    std::array<int, 9> m_iaThermocoupleTmp;           // PGB热电偶传感器温度
    std::array<int, 9> m_iaBibTmp;                    // BIB温度
    std::array<float, 8> m_iaAdcData;                 // BIB 8个通道ADC数据
    std::string m_Ip;                                 // IP地址
    bool m_iBoardStatus;                              // 通信单板在位状态
    bool m_iBibStatus;                                // bib单板在位状态
    bool m_iVoutAlarm;                                // Vout电压告警
    std::time_t m_iLastTime;

    // 构造函数
    XBoardData() {
        m_iSlotId = -9999;
        m_iPgbId = -9999;
        m_iBibId = -9999;
        m_iPayState = -9999;
        m_iReportCyc = -9999;
        m_iPgbWarm = -9999;
        m_iBibWarm = -9999;
        m_iaPgbSmokeTmp = -9999;
        m_iaPgbHumidity = -9999;
        m_iaBdVol = {-9999.0, -9999.0};
        m_iaBdpw = {-9999.0, -9999.0};
        m_iaPgbTmp_lm = {-9999, -9999, -9999, -9999, -9999, -9999, -9999, -9999};
        m_iaThermocoupleTmp = {-9999, -9999, -9999, -9999, -9999, -9999, -9999, -9999, -9999};
        m_iaBibTmp = {-9999, -9999, -9999, -9999, -9999, -9999, -9999, -9999, -9999};
        m_iaAdcData = {-9999.0, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0, -9999.0};
#if 0
        for (auto& inner_array : m_iaBibTmp) {
            for (auto& elem : inner_array) {
                elem = 0;
            }
        }
#endif
        m_Ip = "0.0.0.0";
        m_iBoardStatus = false;
        m_iBibStatus = false;
        m_iVoutAlarm = false;
        m_iLastTime = 0;
    }
};

struct XTmpData {
    int                data;
    double             factor; // 系数
    double             offset; // 偏差
    XTmpData() {
        data = -9999;
        factor = -9999.0;
        offset = -9999.0;
    }
};

struct XBoardTmpData {
    std::array<XTmpData, 9> m_iaThermocoupleTmp;       // PGB热电偶传感器温度
    std::array<XTmpData, 9> m_iaBibTmp;                // BIB温度
};

class XBoardManager {
private:
    XBoardManager() {}
    ~XBoardManager() {
        std::lock_guard<std::mutex> lock_(data_rw_mutex);
        mboards.clear();
    }
public:
    XBoardManager(const XBoardManager&) = delete;
    XBoardManager& operator=(const XBoardManager&) = delete;

    static XBoardManager* get_instance() {
        static XBoardManager instance; // 局部静态变量，保证线程安全和延迟初始化
        return &instance;
    }

    static int HexToStr(unsigned char *pbData,char *pszRetStr,int iDataLen, bool bSpace=false)
    {
        static const char chStr[256][4] = {
            "00","01","02","03","04","05","06","07","08","09","0A","0B","0C","0D","0E","0F",
            "10","11","12","13","14","15","16","17","18","19","1A","1B","1C","1D","1E","1F",
            "20","21","22","23","24","25","26","27","28","29","2A","2B","2C","2D","2E","2F",
            "30","31","32","33","34","35","36","37","38","39","3A","3B","3C","3D","3E","3F",
            "40","41","42","43","44","45","46","47","48","49","4A","4B","4C","4D","4E","4F",
            "50","51","52","53","54","55","56","57","58","59","5A","5B","5C","5D","5E","5F",
            "60","61","62","63","64","65","66","67","68","69","6A","6B","6C","6D","6E","6F",
            "70","71","72","73","74","75","76","77","78","79","7A","7B","7C","7D","7E","7F",
            "80","81","82","83","84","85","86","87","88","89","8A","8B","8C","8D","8E","8F",
            "90","91","92","93","94","95","96","97","98","99","9A","9B","9C","9D","9E","9F",
            "A0","A1","A2","A3","A4","A5","A6","A7","A8","A9","AA","AB","AC","AD","AE","AF",
            "B0","B1","B2","B3","B4","B5","B6","B7","B8","B9","BA","BB","BC","BD","BE","BF",
            "C0","C1","C2","C3","C4","C5","C6","C7","C8","C9","CA","CB","CC","CD","CE","CF",
            "D0","D1","D2","D3","D4","D5","D6","D7","D8","D9","DA","DB","DC","DD","DE","DF",
            "E0","E1","E2","E3","E4","E5","E6","E7","E8","E9","EA","EB","EC","ED","EE","EF",
            "F0","F1","F2","F3","F4","F5","F6","F7","F8","F9","FA","FB","FC","FD","FE","FF"
        };
        if(bSpace)
        {
            for(int i=0; i<iDataLen; i++)
            {
                memcpy(pszRetStr+(i*3),chStr[pbData[i]],2);
                memcpy(pszRetStr+(i*3+2)," ",1);
            }
            return (iDataLen*3);
        }
        else
        {
            for(int i=0; i<iDataLen; i++)
                memcpy(pszRetStr+(i<<1),chStr[pbData[i]],2);
            return (iDataLen<<1);
        }
    }

    static std::string HexToStr(unsigned char *pbData,int iDataLen, bool bSpace=false)
    {
        int iRetLen = (bSpace?(iDataLen*3+1):(iDataLen*2+1));
        char *pTempBuff = (char *)malloc(iRetLen);
        HexToStr(pbData,pTempBuff,iDataLen,bSpace);
        pTempBuff[iRetLen-1] = '\0';
        std::string sReturn(pTempBuff);
        free(pTempBuff);
        return sReturn;
    }

    static int StrToHex(char *pszStr,unsigned char *pbRetData,int iStrLen)
    {
        static const unsigned char bByte[256] = {
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x00,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
        };
        int n=0;
        unsigned char *pSrcByte =(unsigned char *)pszStr;
        for(int i=0; i<iStrLen; i++)
        {
            if(pszStr[i] == ' ') continue;
            unsigned char uCHigh = bByte[pSrcByte[i]];
            unsigned char uCLow =0;
            if(i>=iStrLen || pszStr[i+1] == ' ') uCLow =0;
            else uCLow =bByte[pSrcByte[i+1]];
            pbRetData[n++] =((uCHigh<<4)|uCLow);
        }
        return n;
    }

    static std::string StrToHex(char *pszStr,int iStrLen)
    {
        unsigned char *pTempBuff =(unsigned char *)malloc(iStrLen/2+1);
        memset(pTempBuff,0,iStrLen/2+1);
        StrToHex(pszStr,pTempBuff,iStrLen);
        std::string sReturn((char*)pTempBuff);
        free(pTempBuff);
        return sReturn;
    }

    static void trim(std::string& src_string) //去除字符串首尾空白字符
    {
        src_string.erase(0, src_string.find_first_not_of(" \r\n\t\v\f"));
        src_string.erase(src_string.find_last_not_of(" \r\n\t\v\f") + 1);
    }

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

    static int ReadHighByteInt(const void *pMemAddr,int iBytesNum)
    {
        unsigned char *pTmpMem = (unsigned char *)pMemAddr;
        if(iBytesNum ==1)    return (int)(*pTmpMem);
        else if(iBytesNum ==2)    return (int)(((*pTmpMem)<<8)&0xFF00)|(pTmpMem[1]&0xFF);
        else if(iBytesNum ==3)    return (int)(((*pTmpMem)<<16)&0xFF0000)|((pTmpMem[1]<<8)&0xFF00)|(pTmpMem[2]&0xFF);
        else    return (int)(((*pTmpMem)<<24)&0xFF000000)|((pTmpMem[1]<<16)&0xFF0000)|((pTmpMem[2]<<8)&0xFF00)|(pTmpMem[3]&0xFF);
    }

    static int ReadLowByteInt(const void *pMemAddr,int iBytesNum)
    {
        unsigned char *pTmpMem = (unsigned char *)pMemAddr;
        if(iBytesNum ==1)    return (int)(*pTmpMem);
        else if(iBytesNum ==2)    return (int)(((*pTmpMem[1])<<8)&0xFF00)|(pTmpMem[0]&0xFF);
        else if(iBytesNum ==3)    return (int)(((*pTmpMem[2])<<16)&0xFF0000)|((pTmpMem[1]<<8)&0xFF00)|(pTmpMem[0]&0xFF);
        else    return (int)(((*pTmpMem[3])<<24)&0xFF000000)|((pTmpMem[2]<<16)&0xFF0000)|((pTmpMem[1]<<8)&0xFF00)|(pTmpMem[0]&0xFF);
    }

    static void WriteHighByteInt(void *pMemAddr, int iValue, int iBytesNum)
    {
        unsigned char *pTmpMem = (unsigned char *)pMemAddr;
        unsigned char *pValue = (unsigned char *)&iValue;
        if(iBytesNum ==1)    pTmpMem[0] =pValue[0];
        else if(iBytesNum ==2)    {pTmpMem[0] =pValue[1];pTmpMem[1] =pValue[0];}
        else if(iBytesNum ==3)    {pTmpMem[0] =pValue[2];pTmpMem[1] =pValue[1];pTmpMem[2] =pValue[0];}
        else    {pTmpMem[0] =pValue[3];pTmpMem[1] =pValue[2];pTmpMem[2] =pValue[1];pTmpMem[3] =pValue[0];}
    }

    static void WriteLowByteInt(void *pMemAddr, int iValue, int iBytesNum) {
        memcpy(pMemAddr, &iValue, iBytesNum);
    }

    static unsigned short GetCrc16(unsigned char *p, unsigned short len)
    {
        unsigned char i = 0;
        unsigned short tmp = 0xffff;
        while (len > 0)
        {
            tmp ^= *p++;
            while (i < 8)
            {
                if (tmp & 0x0001) { tmp >>= 1; tmp ^= 0xa001;}
                else              { tmp >>= 1;}
                i++;
            }
            i = 0;
            len--;
        }
        return tmp;
    }

    std::string MakePack(int iBoardId,int iChn,int iData) {
        unsigned char uaSendBuff[10] = {0xFE,0xFE,0xFE,0x68,0x01,00,00,00,00,00};
        uaSendBuff[5] = (unsigned char)iBoardId;
        uaSendBuff[6] = (unsigned char)iChn;
        uaSendBuff[7] = (unsigned char)iData;
        WriteLowByteInt(&uaSendBuff[8],GetCrc16(&uaSendBuff[4],4),2);
        return std::string((char*)uaSendBuff,sizeof(uaSendBuff));
    }

    void TestunPack(std::string& msg)
    {
        char *ptr = &msg[0];
        uint8_t boardid, chn, data, cmd;
        uint16_t crc16;
        memcpy(&cmd, ptr + 4, sizeof(uint8_t));
        memcpy(&boardid, ptr + 5, sizeof(uint8_t));
        memcpy(&chn, ptr + 6, sizeof(uint8_t));
        memcpy(&data, ptr + 7, sizeof(uint8_t));
        memcpy(&crc16, ptr + 8, sizeof(uint16_t));
        printf("DBG: [cmd: %d boardid: %d chn: %d data: %d crc16: %d]\n",
               cmd, boardid, chn, data, crc16);
    }

    std::vector<std::string> getAllNonLoopbackIPv4() {
        struct ifaddrs *ifaddr, *ifa;
        int family, s;
        char host[NI_MAXHOST];
        std::vector<std::string> ips;

        if (getifaddrs(&ifaddr) == -1) {
            perror("getifaddrs");
            return ips;
        }

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            family = ifa->ifa_addr->sa_family;
            if (family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
                s = getnameinfo(ifa->ifa_addr,
                                sizeof(struct sockaddr_in),
                                host, NI_MAXHOST,
                                NULL, 0, NI_NUMERICHOST);
                if (s != 0) {
                    printf("getnameinfo() failed: %s\n", gai_strerror(s));
                    continue;
                }
                ips.push_back(host);
            }
        }
        freeifaddrs(ifaddr);
        return ips;
    }

    int SendAllIpAddr(int iBoardId,const char *pPack,int iPackLen) {
        int iReturn = 0;
        std::vector<std::string> server_ips = getAllNonLoopbackIPv4();
        for (const auto& server_ip : server_ips) {
            std::vector<std::string> lstIpVal = split_single_char_delim(server_ip, '.');
            if (lstIpVal.size() != 4) {
                printf("Unsupport ip:%s parament!\n", server_ip.c_str());
                continue;
            }
            std::string sBdIpAddr = lstIpVal[0] + "." + lstIpVal[1] + "." + lstIpVal[2] + "." + std::string::to_string(iBoardId);
            std::string message(pPack, iPackLen);
            if (g_udp_server != nullptr) {
                g_udp_server->send_data(message,sBdIpAddr, REMOTE_PORT);
            } else {
                // 处理 UDPServer 实例未初始化的情况
                std::cerr << "UDP server is not initialized!" << std::endl;
                return -1;
            }
            iReturn++;
        }
        return iReturn;
    }

    int SendRequest(int iBoardId,int iChn,int iData) //单板发送请求
    {
        std::string sPack = MakePack(iBoardId,iChn,iData); //构造数据包
        if (iChn == 0x81 || iChn == 0x85) {
            printf("DBG: real-data[board: %d chn: %d data: %d]\n",
                   iBoardId, iChn, iData);
        }

        // 在单板数据中发现请求板
        std::lock_guard<std::mutex> lock_(data_rw_mutex);
        auto it = mboards.find(iBoardId);
        if (it != mboards.end()) {
            std::string sBdIpAddr = it->second.first.m_Ip;
            std::cout << "dbg sBdIpAddr: " << sBdIpAddr << std::endl;

            if (PGB_WARM_SW == iChn) {
                it->second.first.m_iPgbWarm = -9999;
            } else if (BIB_WARM_SW == iChn) {
                it->second.first.m_iBibWarm = -9999;
            } else if (REPORT_CYCLE == iChn) {
                it->second.first.m_iReportCyc = -9999;
            } else if (PGB_POWER_CONFIG == iChn) {
                it->second.first.m_iPgbPowerVol = -9999;
            } else if (BIB_POWER_CONFIG == iChn) {
                it->second.first.m_iBibPowerVol = -9999;
            }

            if (iChn == 0x81 || iChn == 0x85) {
                TestunPack(sPack);
            }

            if (g_udp_server != nullptr) {
                g_udp_server->send_data(sPack, sBdIpAddr, REMOTE_PORT);
            } else {
                // 处理 UDPServer 实例未初始化的情况
                std::cerr << "UDP server is not initialized!" << std::endl;
            }
        } else {
            printf("ERROR: can't find BoardId\n");
            return -1;
        }
        return 0;
    }

    int SerchBoard(std::string sLocalIp, int value) //基本本机指定网卡搜索单板
    {
        // 存在场景：UI与uni进程和热模拟板与uni进程不在同一网段的场景
        // std::vector<std::string> lstIpVal = split_single_char_delim(sLocalIp, '.'); 此方法只适用于UI与热模拟板在同一网段的场景
        std::vector<std::string> lstIpVal = split_single_char_delim(getAllNonLoopbackIPv4()[0], '.');
        if(lstIpVal.size() != 4)
        {
            std::cerr << "Ip is not valid!" << std::endl;
            return -1;
        }
        if(std::stoi(lstIpVal[0]) < 0 || std::stoi(lstIpVal[0]) > 255 ||
           std::stoi(lstIpVal[1]) < 0 || std::stoi(lstIpVal[1]) > 255 ||
           std::stoi(lstIpVal[2]) < 0 || std::stoi(lstIpVal[2]) > 255)
        {
            std::cerr << "Ip is not valid!" << std::endl;
            return -1;
        }
        std::string sBdIpSection = lstIpVal[0] + "." + lstIpVal[1] + "." + lstIpVal[2] + ".";
        auto start = std::chrono::high_resolution_clock::now();
        for(int i= 1; i<=30; i++) //遍历每一个IP尾数
        {
            std::string sPack1 = MakePack(i,0x01,value); //构造数据包
            std::string sPack2 = MakePack(i,0x02,value); //构造数据包
            std::string sPack3 = MakePack(i,0x03,value); //构造数据包
            int tmp = 150 + i; // 当前的ip分配规则
            std::string sBdIpAddr = sBdIpSection + std::to_string(tmp);
            if (g_udp_server != nullptr) {
                //auto start1 = std::chrono::high_resolution_clock::now();
                g_udp_server->send_data(sPack1, sBdIpAddr, REMOTE_PORT);
                g_udp_server->send_data(sPack2, sBdIpAddr, REMOTE_PORT);
                g_udp_server->send_data(sPack3, sBdIpAddr, REMOTE_PORT);
                //auto end1 = std::chrono::high_resolution_clock::now();
                //std::chrono::duration<double, std::milli> duration1 = end1 - start1;
                //std::cout << "Function execution time: " << duration1.count() << " milliseconds" << std::endl;
            } else {
                // 处理 UDPServer 实例未初始化的情况
                std::cerr << "UDP server is not initialized!" << std::endl;
                return -1;
            }
            usleep(1000);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        std::cout << "Function execution time: " << duration.count() << " milliseconds" << std::endl;
        return 0;
    }

    int clear_mcu_ipinfo(int iBoardId, const std::string& sBdIpAddr)
    {
        std::vector<std::string> lstIpVal = split_single_char_delim(sBdIpAddr, '.');
        if (lstIpVal.size() != 4) {
            std::cerr << "clear_mcu_ipinfo IP was invalid!" << std::endl;
            return -1;
        }
        if(std::stoi(lstIpVal[0]) < 0 || std::stoi(lstIpVal[0]) > 255 ||
           std::stoi(lstIpVal[1]) < 0 || std::stoi(lstIpVal[1]) > 255 ||
           std::stoi(lstIpVal[2]) < 0 || std::stoi(lstIpVal[2]) > 255 ||
           std::stoi(lstIpVal[3]) < 0 || std::stoi(lstIpVal[3]) > 255)
        {
            std::cerr << "clear_mcu_ipinfo IP was invalid!" << std::endl;
            return -1;
        }
        int value = 1;
        std::string sPack = MakePack(iBoardId, 0x04, value);
        printf("clear_mcu_ipinfo boardId:%x ip:%s\n", iBoardId, sBdIpAddr.c_str());
        if (g_udp_server != nullptr) {
            g_udp_server->send_data(sPack, sBdIpAddr, REMOTE_PORT);
        } else {
            std::cerr << "UDP server is not initialized!" << std::endl;
            return -1;
        }
        return 0;
    }

    // ==================== EEPROM 操作 ====================

    // 构造EEPROM读请求包
    // 协议格式: 帧头(4) + 命令字0x01(1) + 板ID(1) + 通道号0x87(1) + 数据个数(4) + [(起始地址(4) + 读取长度(4))...] + CRC16(2)
    std::string BuildEepromReadPack(int iBoardId, const std::vector<uint32_t>& addresses, const std::vector<uint32_t>& lengths, int count) {
        int data_len = 4 + count * 8; // 数据个数(4) + count * (起始地址(4) + 读取长度(4))
        int pack_len = 8 + data_len + 2; // 帧头(4) + 命令字(1) + 板ID(1) + 通道号(1) + 数据 + CRC16(2)
        unsigned char* uaSendBuff = (unsigned char*)malloc(pack_len);
        memset(uaSendBuff, 0, pack_len);
        
        // 帧头
        uaSendBuff[0] = 0xFE; uaSendBuff[1] = 0xFE;
        uaSendBuff[2] = 0xFE; uaSendBuff[3] = 0x68;
        // 命令字 0x01 请求
        uaSendBuff[4] = 0x01;
        // 板ID
        uaSendBuff[5] = (unsigned char)iBoardId;
        // 通道号 0x87 读eeprom
        uaSendBuff[6] = EEPROM_READ;
        // 数据位: 数据个数(4字节)
        WriteLowByteInt(&uaSendBuff[7], count, 4);
        // 数据位: (起始地址(4) + 读取长度(4)) * count
        for (int i = 0; i < count; i++) {
            WriteLowByteInt(&uaSendBuff[11 + i * 8], addresses[i], 4);
            WriteLowByteInt(&uaSendBuff[15 + i * 8], lengths[i], 4);
        }
        // CRC16校验: 从命令字到数据区结束
        int crc_len = 1 + 1 + 1 + data_len; // 命令字(1) + 板ID(1) + 通道号(1) + 数据
        WriteLowByteInt(&uaSendBuff[pack_len - 2], GetCrc16(&uaSendBuff[4], crc_len), 2);
        
        std::string result((char*)uaSendBuff, pack_len);
        free(uaSendBuff);
        return result;
    }

    // 构造EEPROM写请求包
    // 协议格式: 帧头(4) + 命令字0x01(1) + 板ID(1) + 通道号0x88(1) + 数据个数(4) + [(起始地址(4) + 写入长度(4) + 数据(N))...] + CRC16(2)
    std::string BuildEepromWritePack(int iBoardId, const std::vector<uint32_t>& addresses, 
                                      const std::vector<std::string>& hexData, int count) {
        // 先计算总数据长度
        int total_data_len = 4; // 数据个数
        std::vector<int> data_bytes(count);
        for (int i = 0; i < count; i++) {
            data_bytes[i] = hexData[i].length() / 2; // 十六进制字符串转字节数
            total_data_len += 4 + 4 + data_bytes[i]; // 起始地址(4) + 写入长度(4) + 数据(N)
        }
        int pack_len = 8 + total_data_len + 2; // 帧头(4) + 命令字(1) + 板ID(1) + 通道号(1) + 数据 + CRC16(2)
        unsigned char* uaSendBuff = (unsigned char*)malloc(pack_len);
        memset(uaSendBuff, 0, pack_len);
        
        // 帧头
        uaSendBuff[0] = 0xFE; uaSendBuff[1] = 0xFE;
        uaSendBuff[2] = 0xFE; uaSendBuff[3] = 0x68;
        uaSendBuff[4] = 0x01; // 命令字 请求
        uaSendBuff[5] = (unsigned char)iBoardId;
        uaSendBuff[6] = EEPROM_WRITE; // 通道号 0x88 写eeprom
        
        // 数据个数
        WriteLowByteInt(&uaSendBuff[7], count, 4);
        
        // 数据位
        int offset = 11;
        for (int i = 0; i < count; i++) {
            WriteLowByteInt(&uaSendBuff[offset], addresses[i], 4); // 起始地址
            WriteLowByteInt(&uaSendBuff[offset + 4], data_bytes[i], 4); // 写入长度
            // 将十六进制字符串转为二进制数据
            unsigned char* tmpData = (unsigned char*)malloc(data_bytes[i]);
            memset(tmpData, 0, data_bytes[i]);
            StrToHex((char*)hexData[i].c_str(), tmpData, hexData[i].length());
            memcpy(&uaSendBuff[offset + 8], tmpData, data_bytes[i]);
            free(tmpData);
            offset += 8 + data_bytes[i];
        }
        
        // CRC16校验
        int crc_len = 1 + 1 + 1 + total_data_len;
        WriteLowByteInt(&uaSendBuff[pack_len - 2], GetCrc16(&uaSendBuff[4], crc_len), 2);
        
        std::string result((char*)uaSendBuff, pack_len);
        free(uaSendBuff);
        return result;
    }

    // 构造EEPROM擦除请求包
    // 协议格式: 帧头(4) + 命令字0x01(1) + 板ID(1) + 通道号0x89(1) + 数据个数(4) + [(起始地址(4) + 擦除长度(4))...] + CRC16(2)
    std::string BuildEepromErasePack(int iBoardId, const std::vector<uint32_t>& addresses,
                                      const std::vector<uint32_t>& lengths, int count) {
        int data_len = 4 + count * 8; // 数据个数(4) + count * (起始地址(4) + 擦除长度(4))
        int pack_len = 8 + data_len + 2;
        unsigned char* uaSendBuff = (unsigned char*)malloc(pack_len);
        memset(uaSendBuff, 0, pack_len);
        
        uaSendBuff[0] = 0xFE; uaSendBuff[1] = 0xFE;
        uaSendBuff[2] = 0xFE; uaSendBuff[3] = 0x68;
        uaSendBuff[4] = 0x01;
        uaSendBuff[5] = (unsigned char)iBoardId;
        uaSendBuff[6] = EEPROM_ERASE;
        
        // 数据个数
        WriteLowByteInt(&uaSendBuff[7], count, 4);
        
        // 数据位: (起始地址(4) + 擦除长度(4)) * count
        for (int i = 0; i < count; i++) {
            WriteLowByteInt(&uaSendBuff[11 + i * 8], addresses[i], 4);
            WriteLowByteInt(&uaSendBuff[15 + i * 8], lengths[i], 4);
        }
        
        int crc_len = 1 + 1 + 1 + data_len;
        WriteLowByteInt(&uaSendBuff[pack_len - 2], GetCrc16(&uaSendBuff[4], crc_len), 2);
        
        std::string result((char*)uaSendBuff, pack_len);
        free(uaSendBuff);
        return result;
    }

    // 从IP地址中提取板ID（取IP最后一段）
    int IpToBoardId(const std::string& ip) {
        std::vector<std::string> parts = split_single_char_delim(ip, '.');
        if (parts.size() != 4) return -1;
        return std::stoi(parts[3]);
    }

    // 根据板ID获取IP地址
    std::string GetBoardIp(int iBoardId) {
        std::lock_guard<std::mutex> lock_(data_rw_mutex);
        auto it = mboards.find(iBoardId);
        if (it != mboards.end()) {
            return it->second.first.m_Ip;
        }
        return "";
    }

    // 发送EEPROM读请求
    int ReadEeprom(int iBoardId, const std::vector<uint32_t>& addresses, const std::vector<uint32_t>& lengths, int count) {
        if (count <= 0 || addresses.size() == 0 || lengths.size() == 0) {
            printf("ERROR: ReadEeprom invalid parameters\n");
            return -1;
        }
        
        // 构造UDP包
        std::string sPack = BuildEepromReadPack(iBoardId, addresses, lengths, count);
        
        // 获取板IP
        std::string sBdIpAddr = GetBoardIp(iBoardId);
        if (sBdIpAddr.empty()) {
            printf("ERROR: ReadEeprom can't find BoardId %d\n", iBoardId);
            return -1;
        }
        
        // 初始化缓存
        {
            std::lock_guard<std::mutex> lock(eeprom_mutex);
            m_eeprom_read_cache[iBoardId] = EepromReadCache();
        }
        
        printf("DBG: ReadEeprom board:%d ip:%s count:%d\n", iBoardId, sBdIpAddr.c_str(), count);
        
        if (g_udp_server != nullptr) {
            g_udp_server->send_data(sPack, sBdIpAddr, REMOTE_PORT);
        } else {
            std::cerr << "UDP server is not initialized!" << std::endl;
            return -1;
        }
        return 0;
    }

    // 发送EEPROM写请求
    int WriteEeprom(int iBoardId, const std::vector<uint32_t>& addresses, const std::vector<std::string>& hexData, int count) {
        if (count <= 0 || addresses.size() == 0 || hexData.size() == 0) {
            printf("ERROR: WriteEeprom invalid parameters\n");
            return -1;
        }
        
        std::string sPack = BuildEepromWritePack(iBoardId, addresses, hexData, count);
        
        std::string sBdIpAddr = GetBoardIp(iBoardId);
        if (sBdIpAddr.empty()) {
            printf("ERROR: WriteEeprom can't find BoardId %d\n", iBoardId);
            return -1;
        }
        
        {
            std::lock_guard<std::mutex> lock(eeprom_mutex);
            m_eeprom_write_response[iBoardId] = EepromWriteResponse();
        }
        
        printf("DBG: WriteEeprom board:%d ip:%s count:%d\n", iBoardId, sBdIpAddr.c_str(), count);
        
        if (g_udp_server != nullptr) {
            g_udp_server->send_data(sPack, sBdIpAddr, REMOTE_PORT);
        } else {
            std::cerr << "UDP server is not initialized!" << std::endl;
            return -1;
        }
        return 0;
    }

    // 发送EEPROM擦除请求
    int EraseEeprom(int iBoardId, const std::vector<uint32_t>& addresses, const std::vector<uint32_t>& lengths, int count) {
        std::string sPack = BuildEepromErasePack(iBoardId, addresses, lengths, count);
        
        std::string sBdIpAddr = GetBoardIp(iBoardId);
        if (sBdIpAddr.empty()) {
            printf("ERROR: EraseEeprom can't find BoardId %d\n", iBoardId);
            return -1;
        }
        
        {
            std::lock_guard<std::mutex> lock(eeprom_mutex);
            m_eeprom_erase_response[iBoardId] = EepromEraseResponse();
        }
        
        printf("DBG: EraseEeprom board:%d ip:%s count:%d\n", iBoardId, sBdIpAddr.c_str(), count);
        
        if (g_udp_server != nullptr) {
            g_udp_server->send_data(sPack, sBdIpAddr, REMOTE_PORT);
        } else {
            std::cerr << "UDP server is not initialized!" << std::endl;
            return -1;
        }
        return 0;
    }

    // 处理Eeprom响应（在OnUdpReceive中调用）
    void OnEepromResponse(const char *pData, int iDataLen) {
        if (iDataLen < 18) {
            printf("ERROR: OnEepromResponse data too short\n");
            return;
        }
        
        unsigned char iCmd = (unsigned char)pData[4];     // 命令字 0x81
        unsigned char iBoardId = (unsigned char)pData[6];  // 板ID（偏移6，因为第5字节是板负载状态）
        unsigned char iChn = (unsigned char)pData[11];     // 通道号（偏移11，因为7-10是包顺序号）
        
        printf("DBG: OnEepromResponse cmd:0x%02X board:%d chn:0x%02X\n", iCmd, iBoardId, iChn);
        
        if (iCmd != 0x81) {
            printf("ERROR: OnEepromResponse invalid cmd 0x%02X\n", iCmd);
            return;
        }
        
        // 数据位起始偏移: 帧头(4) + 命令字(1) + 板负载状态(1) + 板ID(1) + 包顺序号(4) + 通道号(1) + 占位字节(2) + 包数据长度(2) = 16
        int data_offset = 16;
        
        if (iChn == EEPROM_READ) {
            int offset = data_offset;
            int data_count = ReadLowByteInt(&pData[offset], 4);
            offset += 4;
            
            EepromReadCache cache;
            cache.ready = true;
            cache.error_code = 0;
            cache.timestamp = std::time(nullptr);
            
            printf("DBG: Eeprom read response count:%d\n", data_count);
            
            for (int i = 0; i < data_count && offset + 8 <= iDataLen; i++) {
                EepromReadEntry entry;
                entry.address = ReadLowByteInt(&pData[offset], 4);
                uint32_t data_len = ReadLowByteInt(&pData[offset + 4], 4);
                offset += 8;
                
                if (offset + (int)data_len <= iDataLen) {
                    entry.data = HexToStr((unsigned char*)&pData[offset], data_len, false);
                    entry.timestamp = std::time(nullptr);
                    offset += data_len;
                }
                cache.entries.push_back(entry);
                
                printf("DBG:   addr:0x%08X len:%d data:%s\n", 
                       entry.address, data_len, entry.data.c_str());
            }
            
            {
                std::lock_guard<std::mutex> lock(eeprom_mutex);
                m_eeprom_read_cache[iBoardId] = cache;
                m_eeprom_read_cache[iBoardId].cv.notify_all();
            }
            
        } else if (iChn == EEPROM_WRITE) {
            int offset = data_offset;
            int errcode = (unsigned char)pData[offset];
            
            EepromWriteResponse resp;
            resp.ready = true;
            resp.error_code = errcode;
            if (errcode == 0x00) resp.error_msg = "success";
            else if (errcode == 0x01) resp.error_msg = "address out of bounds";
            else if (errcode == 0x02) resp.error_msg = "CRC error";
            else resp.error_msg = "unknown error";
            
            printf("DBG: Eeprom write response errcode:0x%02X %s\n", errcode, resp.error_msg.c_str());
            
            {
                std::lock_guard<std::mutex> lock(eeprom_mutex);
                m_eeprom_write_response[iBoardId] = resp;
                m_eeprom_write_response[iBoardId].cv.notify_all();
            }
            
        } else if (iChn == EEPROM_ERASE) {
            int offset = data_offset;
            int errcode = (unsigned char)pData[offset];
            
            EepromEraseResponse resp;
            resp.ready = true;
            resp.error_code = errcode;
            if (errcode == 0x00) resp.error_msg = "success";
            else if (errcode == 0x01) resp.error_msg = "address out of bounds";
            else if (errcode == 0x02) resp.error_msg = "CRC error";
            else resp.error_msg = "unknown error";
            
            printf("DBG: Eeprom erase response errcode:0x%02X %s\n", errcode, resp.error_msg.c_str());
            
            {
                std::lock_guard<std::mutex> lock(eeprom_mutex);
                m_eeprom_erase_response[iBoardId] = resp;
                m_eeprom_erase_response[iBoardId].cv.notify_all();
            }
        }
    }
    // 等待Eeprom读结果（供HTTP层调用）
    bool WaitEepromReadResult(int iBoardId, EepromReadCache& result, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(eeprom_mutex);
        auto it = m_eeprom_read_cache.find(iBoardId);
        if (it == m_eeprom_read_cache.end()) return false;
        
        if (it->second.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
            [&it] { return it->second.ready; })) {
            result = it->second;
            return true;
        }
        return false; // 超时
    }

    // 等待Eeprom写结果（供HTTP层调用）
    bool WaitEepromWriteResult(int iBoardId, EepromWriteResponse& result, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(eeprom_mutex);
        auto it = m_eeprom_write_response.find(iBoardId);
        if (it == m_eeprom_write_response.end()) return false;
        
        if (it->second.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [&it] { return it->second.ready; })) {
            result = it->second;
            return true;
        }
        return false;
    }

    // 等待Eeprom擦除结果（供HTTP层调用）
    bool WaitEepromEraseResult(int iBoardId, EepromEraseResponse& result, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(eeprom_mutex);
        auto it = m_eeprom_erase_response.find(iBoardId);
        if (it == m_eeprom_erase_response.end()) return false;
        
        if (it->second.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [&it] { return it->second.ready; })) {
            result = it->second;
            return true;
        }
        return false;
    }

    std::string getCurrentTime(uint32_t mode) {
        std::time_t now = std::time(nullptr);
        struct tm* localTime = std::localtime(&now);
        char buf[32];
        if (mode == 1) {
            snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
                     localTime->tm_year + 1900,
                     localTime->tm_mon + 1,
                     localTime->tm_mday,
                     localTime->tm_hour,
                     localTime->tm_min,
                     localTime->tm_sec);
        } else if (mode == 2) {
            snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d%02d",
                     localTime->tm_year + 1900,
                     localTime->tm_mon + 1,
                     localTime->tm_mday,
                     localTime->tm_hour,
                     localTime->tm_min,
                     localTime->tm_sec);
        }
        return std::string(buf);
    }

    void writeDataToCSV(const std::string& filename, const DataRecord& record) {
        std::ofstream csvFile(filename, std::ios_base::app);
        if (!csvFile.is_open()) {
            std::cerr << "Failed to open file for writing: " << filename << std::endl;
            return;
        }

        csvFile << record.time << "," << record.slot << ",";
        csvFile << std::showbase << std::hex << record.BibId << ",";
        csvFile << std::noshowbase << std::dec;
        size_t num_bib_temp = record.bib_temp.size();
        for (size_t i = 0; i < num_bib_temp; ++i) {
            csvFile << std::setprecision(2) << std::fixed << record.bib_temp[i] << ",";
        }
        size_t num_thermocouple_temp = record.thermocouple_temp.size();
        for (size_t i = 0; i < num_thermocouple_temp; ++i) {
            csvFile << std::setprecision(2) << std::fixed << record.thermocouple_temp[i] << ",";
        }
        // 新增原始温度以及校验系数记录
        size_t num_bib_origin_temp = record.bib_origin_temp.size();
        for (size_t i = 0; i < num_bib_origin_temp; ++i) {
            csvFile << std::setprecision(2) << std::fixed << record.bib_origin_temp[i] << ",";
        }
        size_t num_thermocouple_origin_temp = record.thermocouple_origin_temp.size();
        for (size_t i = 0; i < num_thermocouple_origin_temp.size(); ++i) {
            csvFile << std::setprecision(2) << std::fixed << record.thermocouple_origin_temp[i] << ",";
        }
        size_t num_bib_temp_coefficient = record.bib_temp_coefficient.size();
        for (size_t i = 0; i < num_bib_temp_coefficient; ++i) {
            csvFile << std::setprecision(10) << std::fixed << record.bib_temp_coefficient[i].first << ","
                    << record.bib_temp_coefficient[i].second << ",";
        }
        size_t num_thermocouple_temp_coefficient = record.thermocouple_temp_coefficient.size();
        for (size_t i = 0; i < num_thermocouple_temp_coefficient; ++i) {
            csvFile << std::setprecision(10) << std::fixed << record.thermocouple_temp_coefficient[i].first << ","
                    << record.thermocouple_temp_coefficient[i].second << std::endl;
        } else {
            csvFile << std::setprecision(10) << std::fixed << record.thermocouple_temp_coefficient[i].first << ","
                    << record.thermocouple_temp_coefficient[i].second << " ,";
        }
        csvFile.close();
    }

    int stopSaveBibTempData(const int boardid = 0xFF)
    {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
        cv.notify_one();
        return 0;
    }

    int startSaveBibTempData(const int boardid = 0xFF)
    {
        if (running) {
            std::cout << "startSaveBibTempData already!!!" << std::endl;
            return -1;
        }
        m_save_path = "/tmp/zcqdemouni_" + getCurrentTime(2) + ".csv";
        std::ofstream csvFileHead(m_save_path);
        if (!csvFileHead.is_open()) {
            std::cerr << "Failed to open file for writing filehead: " << m_save_path << std::endl;
            return -1;
        }
        csvFileHead << "data" << "," << "slot" << "," << "BibId" << "," << "CH01" << "," << "CH02" << "," << "CH03" << "," << "CH04" << ","
                    << "CH05" << "," << "CH06" << "," << "CH07" << "," << "CH08" << "," << "CH09" << "," << "CH10" << "," << "CH11" << "," << "CH12" << ","
                    << "CH13" << "," << "CH14" << "," << "CH15" << "," << "CH16" << "," << "CH17" << "," << "CH18" << "," << "CH19" << ","
                    << "CH01-Org" << "," << "CH02-Org" << "," << "CH03-Org" << "," << "CH04-Org" << "," << "CH05-Org" << "," << "CH06-Org" << ","
                    << "CH07-Org" << "," << "CH08-Org" << "," << "CH09-Org" << "," << "CH10-Org" << "," << "CH11-Org" << "," << "CH12-Org" << ","
                    << "CH13-Org" << "," << "CH14-Org" << "," << "CH15-Org" << "," << "CH16-Org" << "," << "CH17-Org" << "," << "CH18-Org" << ","
                    << "CH01-Factor" << "," << "CH01-Offset" << "," << "CH02-Factor" << "," << "CH02-Offset" << "," << "CH03-Factor" << "," << "CH03-Offset" << ","
                    << "CH04-Factor" << "," << "CH04-Offset" << "," << "CH05-Factor" << "," << "CH05-Offset" << "," << "CH06-Factor" << "," << "CH06-Offset" << ","
                    << "CH07-Factor" << "," << "CH07-Offset" << "," << "CH08-Factor" << "," << "CH08-Offset" << "," << "CH09-Factor" << "," << "CH09-Offset" << ","
                    << "CH10-Factor" << "," << "CH10-Offset" << "," << "CH11-Factor" << "," << "CH11-Offset" << "," << "CH12-Factor" << "," << "CH12-Offset" << ","
                    << "CH13-Factor" << "," << "CH13-Offset" << "," << "CH14-Factor" << "," << "CH14-Offset" << "," << "CH15-Factor" << "," << "CH15-Offset" << ","
                    << "CH16-Factor" << "," << "CH16-Offset" << "," << "CH17-Factor" << "," << "CH17-Offset" << "," << "CH18-Factor" << "," << "CH18-Offset" << std::endl;
        csvFileHead.close();
        running = true;
        auto interval_time = std::chrono::minutes(1);
        std::chrono::time_point<std::chrono::high_resolution_clock> cur_time;
        std::chrono::time_point<std::chrono::high_resolution_clock> last_time = std::chrono::high_resolution_clock::now();
                while (1)
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (cv.wait_for(lock, std::chrono::seconds(5), [this] { return !running; })) {
                break;
            }
            lock.unlock();
            cur_time = std::chrono::high_resolution_clock::now();
            if ((cur_time - last_time) < interval_time) {
                continue;
            }
            last_time = cur_time;
            std::vector<DataRecord> data_vec;
            //std::cout << "startSaveBibTempData boardid " << boardid << std::endl;
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            if (boardid == 0xFF) {
                //std::cout << "startSaveBibTempData mboards size " << mboards.size() << std::endl;
                for (const auto& mboard : mboards)
                {
                    DataRecord i_data;
                    std::string now_time = getCurrentTime(1);
                    i_data.time = now_time;
                    i_data.slot = mboard.first;
                    i_data.BibId = mboard.second.first.m_iBibId;

                    size_t num_bib_temp = mboard.second.first.m_iaBibTmp.size();
                    for (size_t i = 0; i < num_bib_temp; ++i) {
                        double i_bib_temp = mboard.second.first.m_iaBibTmp[i]/100.0;
                        i_data.bib_temp.push_back(i_bib_temp);
                    }

                    size_t num_thermocouple_temp = mboard.second.first.m_iaThermocoupleTmp.size();
                    for (size_t i = 0; i < num_thermocouple_temp; ++i) {
                        double i_thermocouple_temp = mboard.second.first.m_iaThermocoupleTmp[i]/100.0;
                        i_data.thermocouple_temp.push_back(i_thermocouple_temp);
                    }

                    size_t num_bib_origin_temp = mboard.second.second.m_iaBibTmp.size();
                    for (size_t i = 0; i < num_bib_origin_temp; ++i) {
                        double i_bib_origin_temp = mboard.second.second.m_iaBibTmp[i].data / 100.0;
                        i_data.bib_origin_temp.push_back(i_bib_origin_temp);
                        double factor = mboard.second.second.m_iaBibTmp[i].factor;
                        double offset = mboard.second.second.m_iaBibTmp[i].offset;
                        i_data.bib_temp_coefficient.push_back(std::make_pair(factor, offset));
                    }

                    size_t num_thermocouple_origin_temp = mboard.second.second.m_iaThermocoupleTmp.size();
                    for (size_t i = 0; i < num_thermocouple_origin_temp; ++i) {
                        double i_thermocouple_origin_temp = mboard.second.second.m_iaThermocoupleTmp[i].data / 100.0;
                        i_data.thermocouple_origin_temp.push_back(i_thermocouple_origin_temp);
                        double factor = mboard.second.second.m_iaThermocoupleTmp[i].factor;
                        double offset = mboard.second.second.m_iaThermocoupleTmp[i].offset;
                        i_data.thermocouple_temp_coefficient.push_back(std::make_pair(factor, offset));
                    }
                    data_vec.push_back(i_data);
                }
            } else {
                auto it = mboards.find(boardid);
                if (it != mboards.end()) {
                    DataRecord i_data;
                    std::string now_time = getCurrentTime(1);
                    i_data.time = now_time;
                    i_data.slot = it->first;
                    i_data.BibId = it->second.first.m_iBibId;

                    size_t num_bib_temp = it->second.first.m_iaBibTmp.size();
                    for (size_t i = 0; i < num_bib_temp; ++i) {
                        double i_bib_temp = it->second.first.m_iaBibTmp[i]/100.0;
                        i_data.bib_temp.push_back(i_bib_temp);
                    }

                    size_t num_thermocouple_temp = it->second.first.m_iaThermocoupleTmp.size();
                    for (size_t i = 0; i < num_thermocouple_temp; ++i) {
                        double i_thermocouple_temp = it->second.first.m_iaThermocoupleTmp[i]/100.0;
                        i_data.bib_temp.push_back(i_thermocouple_temp);
                    }

                    size_t num_bib_origin_temp = it->second.second.m_iaBibTmp.size();
                    for (size_t i = 0; i < num_bib_origin_temp; ++i) {
                        double i_bib_origin_temp = it->second.second.m_iaBibTmp[i].data / 100.0;
                        i_data.bib_origin_temp.push_back(i_bib_origin_temp);
                        double factor = it->second.second.m_iaBibTmp[i].factor;
                        double offset = it->second.second.m_iaBibTmp[i].offset;
                        i_data.bib_temp_coefficient.push_back(std::make_pair(factor, offset));
                    }

                    size_t num_thermocouple_origin_temp = it->second.second.m_iaThermocoupleTmp.size();
                    for (size_t i = 0; i < num_thermocouple_origin_temp; ++i) {
                        double i_thermocouple_origin_temp = it->second.second.m_iaThermocoupleTmp[i].data / 100.0;
                        i_data.thermocouple_origin_temp.push_back(i_thermocouple_origin_temp);
                        double factor = it->second.second.m_iaThermocoupleTmp[i].factor;
                        double offset = it->second.second.m_iaThermocoupleTmp[i].offset;
                        i_data.thermocouple_temp_coefficient.push_back(std::make_pair(factor, offset));
                    }
                    data_vec.push_back(i_data);
                }
            }

            if (data_vec.empty()) {
                std::cout << "can't find this boardid at memory boardid:" << boardid << std::endl;
            }

            for (auto i_savedata : data_vec) {
                writeDataToCSV(m_save_path, i_savedata);
            }
        }
        return 0;
    }

    json getJsonData(const int boardid) {
        json jsonData;
        XBoardData tmp_data;
        {
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            auto it = mboards.find(boardid);
            if (it != mboards.end()) {
                if (abs(std::time(nullptr) - it->second.first.m_iLastTime) > 20) { // 设备连接超时
                    it->second.first.m_iBoardStatus = false;
                    jsonData["SlotId"] = it->second.first.m_iSlotId;
                    jsonData["BoardStatus"] = it->second.first.m_iBoardStatus;
                    mboards.erase(it);
                    std::cout << "pgb not online!!!" << std::endl;
                    return jsonData;
                }
                tmp_data = it->second.first;
            }
        }
        //std::lock_guard<std::mutex> lock_(data_rw_mutex);
        jsonData["PowerVol"] = tmp_data.m_iPgbPowerVol;
        jsonData["BibPowerVol"] = tmp_data.m_iBibPowerVol;
        jsonData["SlotId"] = tmp_data.m_iSlotId;
        jsonData["PgbId"] = tmp_data.m_iPgbId;
        jsonData["BibId"] = tmp_data.m_iBibId;
        jsonData["PayState"] = tmp_data.m_iPayState;
        jsonData["ReportCyc"] = tmp_data.m_iReportCyc;
        jsonData["PgbWarm"] = tmp_data.m_iPgbWarm;
        jsonData["BibWarm"] = tmp_data.m_iBibWarm;
        jsonData["PgbSmokeTmp"] = tmp_data.m_iaPgbSmokeTmp;
        jsonData["PgbHumidity"] = tmp_data.m_iaPgbHumidity;
        std::vector<float> vecBdVol(tmp_data.m_iaBdVol.begin(), tmp_data.m_iaBdVol.end());
        json j_BdVol = vecBdVol;
        jsonData["BdVol"] = j_BdVol;
        std::vector<float> vecBdpw(tmp_data.m_iaBdpw.begin(), tmp_data.m_iaBdpw.end());
        json j_Bdpw = vecBdpw;
        jsonData["Bdpw"] = j_Bdpw;
        std::vector<int> vecPgbTmp_lm(tmp_data.m_iaPgbTmp_lm.begin(), tmp_data.m_iaPgbTmp_lm.end());
        json j_PgbTmp_lm = vecPgbTmp_lm;
        jsonData["PgbTmp_lm"] = j_PgbTmp_lm;
        std::vector<int> vecThermocoupleTmp(tmp_data.m_iaThermocoupleTmp.begin(), tmp_data.m_iaThermocoupleTmp.end());
        json j_ThermocoupleTmp = vecThermocoupleTmp;
        jsonData["ThermocoupleTmp"] = j_ThermocoupleTmp;
        std::vector<int> vecBibTmp(tmp_data.m_iaBibTmp.begin(), tmp_data.m_iaBibTmp.end());
        json j_BibTmp = vecBibTmp;
        jsonData["BibTmp"] = j_BibTmp;
        std::vector<float> vecBibAdcData(tmp_data.m_iaAdcData.begin(), tmp_data.m_iaAdcData.end());
        json j_AdcData = vecBibAdcData;
        jsonData["AdcData"] = j_AdcData;
        jsonData["Ip"] = tmp_data.m_Ip;
        jsonData["BoardStatus"] = tmp_data.m_iBoardStatus;
        jsonData["BibStatus"] = tmp_data.m_iBibStatus;
        jsonData["VoutAlarm"] = tmp_data.m_iVoutAlarm;
        printf("DBG: send UI board info: [slotid: %d PGB vol: %d warm: %d BIB vol: %d warm: %d]\n",
               tmp_data.m_iSlotId, tmp_data.m_iPgbPowerVol, tmp_data.m_iPgbWarm, tmp_data.m_iBibPowerVol, tmp_data.m_iBibWarm);
#if 0
        std::cout << "dbg boardid: " << boardid << std::endl;
        auto it = mboards.find(boardid);
        if (it != mboards.end()) {
            if (abs(std::time(nullptr) - it->second.m_iLastTime) > 256) { // 设备连接超时
                it->second.m_iBoardStatus = false;
                jsonData["SlotId"] = it->second.m_iSlotId;
                jsonData["BoardStatus"] = it->second.m_iBoardStatus;
                mboards.erase(it);
                std::cout << "dbg jsonData " << std::endl;
                return jsonData;
            }
        }
#endif
        //it->second.Statistics();
        //it->second.FPower();
        jsonData["PowerVol"] = it->second.m_iPgbPowerVol;
        jsonData["SlotId"] = it->second.m_iSlotId;
        jsonData["PgbId"] = it->second.m_iPgbId;
        jsonData["BibId"] = it->second.m_iBibId;
        jsonData["PayState"] = it->second.m_iPayState;
        jsonData["ReportCyc"] = it->second.m_iReportCyc;
        jsonData["PgbWarm"] = it->second.m_iPgbWarm;
        jsonData["BibWarm"] = it->second.m_iBibWarm;
        jsonData["PgbSmokeTmp"] = it->second.m_iaPgbSmokeTmp;
        jsonData["PgbHumidity"] = it->second.m_iaPgbHumidity;
        // 将 std::array<int,> 转换为 std::vector<int>
        std::vector<float> vecBdVol(it->second.m_iaBdVol.begin(), it->second.m_iaBdVol.end());
        json j_BdVol = vecBdVol;
        jsonData["BdVol"] = j_BdVol;
        std::vector<float> vecBdpw(it->second.m_iaBdpw.begin(), it->second.m_iaBdpw.end());
        json j_Bdpw = vecBdpw;
        jsonData["Bdpw"] = j_Bdpw;
        std::vector<int> vecPgbTmp_lm(it->second.m_iaPgbTmp_lm.begin(), it->second.m_iaPgbTmp_lm.end());
        json j_PgbTmp_lm = vecPgbTmp_lm;
        jsonData["PgbTmp_lm"] = j_PgbTmp_lm;
        std::vector<int> vecPgbTmp(it->second.m_iaBibTmp.begin(), it->second.m_iaBibTmp.end());
        json j_PgbTmp = vecPgbTmp;
        jsonData["PgbTmp"] = j_PgbTmp;
        std::vector<int> vecBibTmp(it->second.m_iaBibTmp.begin(), it->second.m_iaBibTmp.end());
        json j_BibTmp = vecBibTmp;
        jsonData["BibTmp"] = j_BibTmp;
#if 0 // 新版不使用二维数组
        json j_BibTmp = json::array();
        for (const auto& row : it->second.m_iaBibTmp) {
            json row_json = json::array();
            for (int val : row) {
                row_json.push_back(val);
            }
            j_BibTmp.push_back(row_json);
        }
        jsonData["BibTmp"] = j_BibTmp;
#endif
        jsonData["Ip"] = it->second.m_Ip;
        jsonData["BoardStatus"] = it->second.m_iBoardStatus;
        jsonData["BibStatus"] = it->second.m_iBibStatus;
        jsonData["VoutAlarm"] = it->second.m_iVoutAlarm;
        //std::cout << "jsonData: " << jsonData.dump(4) << std::endl;
    }
    return jsonData;
}

    json getAllJsonData() {
        json jsonData = json::array();
        std::vector<int> board_vec;
        {
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            for (const auto& mboard : mboards)
            {
                board_vec.push_back(mboard.first);
            }
        }

        for (auto iboard : board_vec)
        {
            json jtmp = getJsonData(iboard);
            jsonData.push_back(jtmp);
        }

        std::ofstream o("/tmp/zcqdemououtput.json");
        o << jsonData.dump(4);
        o.close();
        return jsonData;
    }

    json getAllVoutAlarm() {
        json jsonData;
        std::vector<int> board_vec;
        {
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            for (const auto& mboard : mboards)
            {
                if (mboard.second.first.m_iVoutAlarm == true)
                    board_vec.push_back(mboard.first);
            }
        }

        json j_VoutAlarm = board_vec;
        jsonData["VoutAlarm"] = j_VoutAlarm;
        return jsonData;
    }

    std::string check_calibration_to_string() {
        std::unique_lock<std::mutex> lock(calibration_map_mutex);
        std::stringstream ss;
        ss << "===== 内存校准数据校验 =====\n";
        ss << "校准数据总条目数（BIB_ID数量）：" << calibration_map.size() << "\n\n";

        if (calibration_map.empty()) {
            ss << "⚠️  警告：内存中无任何校准数据！\n";
            ss << "===== 校验结束 =====\n";
            return ss.str();
        }

        for (const auto& bib_item : calibration_map) {
            int bib_id = bib_item.first;
            const auto& domin_map = bib_item.second;
            ss << "----------------------------\n";
            ss << "BIB_ID: " << bib_id << "\n";
            ss << "该BIB_ID下区间(domin)数量：" << domin_map.size() << "\n";

            for (const auto& domin_item : domin_map) {
                int domin = domin_item.first;
                const auto& cal_list = domin_item.second;
                ss << "  区间(domin)：" << domin << "\n";
                ss << "  该区间下校准系数对数量：" << cal_list.size() << "\n";
                ss << "  校准系数明细（factor, offset）：\n";

                for (size_t idx = 0; idx < cal_list.size(); ++idx) {
                    double factor = cal_list[idx].first;
                    double offset = cal_list[idx].second;
                    ss << "    传感器索引[" << idx << "]：factor=" << std::fixed << std::setprecision(6) << factor 
                    << ", offset=" << offset << "\n";
                }
            }
            ss << "\n";
        }

        ss << "===== 校验结束 =====\n";
        return ss.str();
    }

    virtual void OnUdpReceive(std::string sFromAddr,const char *pData,int iDataLen)
    {
        unsigned long iHeader = ReadLowByteInt(pData,4);
        std::cout << "OnUdpReceive iHeader: " << iHeader << " iDataLen: " << iDataLen << " iDataLen" << std::endl;
        if(iHeader != 0x68FEFEFE) return; //包头错误
        unsigned char iCmd = (unsigned char)pData[4];
        unsigned char iChn = (unsigned char)pData[6]; //通道号
        
        // 先判断是否是EEPROM响应（命令字0x81，通道号0x87/0x88/0x89）
        if (iCmd == 0x81 && (iChn == EEPROM_READ || iChn == EEPROM_WRITE || iChn == EEPROM_ERASE)) {
            OnEepromResponse(pData, iDataLen);
            return;
        }
        
        std::string sPackStr = HexToStr((unsigned char *)pData, iDataLen, true);
        std::cout << "recv from: " << sFromAddr << " iDataLen: " << iDataLen << " value:" << sPackStr.c_str() << std::endl;
        if(iDataLen ==10) //长度为10的是控制层返回的包
        {
            unsigned char iPgbSlotId = (unsigned char)pData[5]; //PGB板ID
            unsigned char iChn = (unsigned char)pData[6]; //通道号
            unsigned char *PayData = (unsigned char *)&pData[7]; //负载数据
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            auto it = mboards.find(iPgbSlotId);
            if (it != mboards.end()) {
                it->second.first.m_Ip = sFromAddr;
                it->second.first.m_iSlotId = iPgbSlotId;
                it->second.first.m_iBoardStatus = true;
                it->second.first.m_iLastTime = std::time(nullptr);
                if(iChn ==0x81)    it->second.first.m_iPgbWarm = ReadLowByteInt(PayData,1); //PGB加热开关
                else if(iChn ==0x82)    it->second.first.m_iBibWarm = ReadLowByteInt(PayData,1); //BIB加热开关
                else if(iChn ==0x83)    it->second.first.m_iReportCyc = ReadLowByteInt(PayData,1); //上传数据间隔
                else if(iChn ==0x85)    it->second.first.m_iPgbPowerVol = ReadLowByteInt(PayData,1);
                else if(iChn == BIB_POWER_CONFIG)    it->second.first.m_iBibPowerVol = ReadLowByteInt(PayData,1);
            } else {
                printf("add to mboards pgbid:%d\n", iPgbSlotId);
                // 加入到单板管理
                if (0 != ConfigureCalibrationMap())
                {
                    std::cout << "configure calibration file failed!!!" << std::endl;
                }
                mboards[iPgbSlotId].first = XBoardData();
                mboards[iPgbSlotId].first.m_Ip = sFromAddr;
                mboards[iPgbSlotId].first.m_iSlotId = iPgbSlotId;
                mboards[iPgbSlotId].first.m_iBoardStatus = true;
                mboards[iPgbSlotId].first.m_iLastTime = std::time(nullptr);
                if(iChn ==0x81)    mboards[iPgbSlotId].first.m_iPgbWarm = ReadLowByteInt(PayData,1);
                else if(iChn ==0x82)    mboards[iPgbSlotId].first.m_iBibWarm = ReadLowByteInt(PayData,1);
                else if(iChn ==0x83)    mboards[iPgbSlotId].first.m_iReportCyc = ReadLowByteInt(PayData,1);
                else if(iChn ==0x85)    mboards[iPgbSlotId].first.m_iPgbPowerVol = ReadLowByteInt(PayData,1);
                else if(iChn == BIB_POWER_CONFIG)    mboards[iPgbSlotId].first.m_iBibPowerVol = ReadLowByteInt(PayData,1);
            }
        }
        else if(iDataLen >10) //超过10的包是下面主动上报的状态包
        {
            unsigned char iPayState = (unsigned char)pData[5]; //板负载状态
            unsigned char iPgbSlotId = (unsigned char)pData[6]; //PGB板ID
            unsigned char iBibBoardId = (unsigned char)pData[7]; //BIB板ID
            unsigned char iChn1 = (unsigned char)pData[8]; //通道号1
            unsigned char iChn2 = (unsigned char)pData[9]; //通道号2
            unsigned int iPayLen = ReadLowByteInt(&pData[10],2); //负载长度
            unsigned int iPackSeq = ReadLowByteInt(&pData[12],4); //包序号
            unsigned char *PayData = (unsigned char *)&pData[16]; //负载数据
            std::lock_guard<std::mutex> lock_(data_rw_mutex);
            auto it = mboards.find(iPgbSlotId);
            if (it != mboards.end()) {
                it->second.first.m_Ip = sFromAddr;
                it->second.first.m_iSlotId = iPgbSlotId;
                it->second.first.m_iBoardStatus = true;
                it->second.first.m_iLastTime = std::time(nullptr);
                it->second.first.m_iPayState = iPayState;
                it->second.first.m_iBibId = iBibBoardId;
                //it->it.second.m_iBibBoardId = iBibBoardId;
                if(iChn1 == 1)    //PGB温度和湿度 (8+1+1)
                {
                    for(int iT = 0; iT < iPayLen/2 && iT < 10; iT++)
                    {
                        if(iT < 8)
                        {
                            it->second.first.m_iaPgbTmp_lm[iT] = (short)ReadLowByteInt(PayData+iT*2, 2); //lm75b
                        }
                        else if(iT < 9)
                        {
                            it->second.first.m_iaPgbSmokeTmp = (short)ReadLowByteInt(PayData+iT*2, 2); //烟感温度
                        }
                        else
                        {
                            it->second.first.m_iaPgbHumidity = (short)ReadLowByteInt(PayData+iT*2, 2); //湿度
                        }
                    }
                }
                else if(iChn1 == 2)    //BIB温度 (1+2+9+9)
                {
                    it->second.first.m_iBibStatus = (short)ReadLowByteInt(PayData,2);
                    if (0 == it->second.first.m_iBibStatus) {
                        std::cout << "BIB Board is not exist, temp is invalid" << std::endl;
                        return;
                    }
                    std::map<int,std::vector<std::pair<double, double>>> cal_data;
                    for(int iT = 1; iT < (iPayLen / 2) && iT < 21; iT++)
                    {
                        if(iT < 3)
                        {
                            if (iT == 1) {
                                it->second.first.m_iBibId = (int)ReadLowByteInt(PayData+iT*2,4); //bib id
                                cal_data = find_data(it->second.first.m_iBibId);
                                if (cal_data.size() > 0) {
                                    std::cout << "cal_data size:" << cal_data.size() << std::endl;
                                    for(auto iter : cal_data) {
                                        std::cout << mboards[iPgbSlotId].first.m_iBibId << " " << iter.first << " ";
                                        for(auto data : iter.second) {
                                            std::cout << (double)data.first << " " << (double)data.second << " ";
                                        }
                                        std::cout << std::endl;
                                    }
                                } else {
                                    std::cout << "Error: new_board can't find this bibid in calibration_map:bibid(" << mboards[iPgbSlotId].first.m_iBibId << std::endl;
                                }
                            }
                        }
                        else if(iT < 12)
                        {
                            if(cal_data.size())
                            {
                                int iBibTmp = (short)ReadLowByteInt(PayData+iT*2,2);
                                it->second.second.m_iaBibTmp[iT - 3].data = iBibTmp; // 记录原始数据
                                if (iBibTmp < 1500) {
                                    if (cal_data.find(1) != cal_data.end()) {
                                        it->second.first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[1][iT-3].first + cal_data[1][iT-3].second * 100) / 100; //Y1
                                        it->second.second.m_iaBibTmp[iT - 3].factor = cal_data[1][iT-3].first;
                                        it->second.second.m_iaBibTmp[iT - 3].offset = cal_data[1][iT-3].second;
                                        continue;
                                    }
                                } else if (iBibTmp > 12000) {
                                    if (cal_data.find(3) != cal_data.end()) {
                                        it->second.first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[3][iT-3].first + cal_data[3][iT-3].second * 100) / 100; //Y3
                                        it->second.second.m_iaBibTmp[iT - 3].factor = cal_data[3][iT-3].first;
                                        it->second.second.m_iaBibTmp[iT - 3].offset = cal_data[3][iT-3].second;
                                        continue;
                                    }
                                } else {
                                    if (cal_data.find(2) != cal_data.end()) {
                                        it->second.first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[2][iT-3].first + cal_data[2][iT-3].second * 100) / 100; //Y2
                                        it->second.second.m_iaBibTmp[iT - 3].factor = cal_data[2][iT-3].first;
                                        it->second.second.m_iaBibTmp[iT - 3].offset = cal_data[2][iT-3].second;
                                        continue;
                                    }
                                }
                                it->second.first.m_iaBibTmp[iT - 3] = iBibTmp;
                                std::cout << "Calibrate PgbSlot: " << (int)iPgbSlotId << " BibTmp:" << iT - 3 << " value:" << mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] << std::endl;
                            } else {
                                it->second.first.m_iaBibTmp[iT - 3] = (short)ReadLowByteInt(PayData+iT*2,2); //依次取出int01各个温度
                                it->second.second.m_iaBibTmp[iT - 3].data = it->second.first.m_iaBibTmp[iT - 3]; // 记录原始数据
                            }
                        }
                        else if(iT < 21)
                        {
                            if(cal_data.size())
                            {
                                int iBibTmp = (short)ReadLowByteInt(PayData+iT*2,2);
                                it->second.second.m_iaThermocoupleTmp[iT - 12].data = iBibTmp; // 记录原始数据
                                if (iBibTmp < 1500) {
                                    if (cal_data.find(1) != cal_data.end()) {
                                        it->second.first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[1][iT-3].first + cal_data[1][iT-3].second * 100) / 100; //Y1
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[1][iT-3].first;
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[1][iT-3].second;
                                        continue;
                                    }
                                } else if (iBibTmp > 12000) {
                                    if (cal_data.find(3) != cal_data.end()) {
                                        it->second.first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[3][iT-3].first + cal_data[3][iT-3].second * 100) / 100; //Y3
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[3][iT-3].first;
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[3][iT-3].second;
                                        continue;
                                    }
                                } else {
                                    if (cal_data.find(2) != cal_data.end()) {
                                        it->second.first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[2][iT-3].first + cal_data[2][iT-3].second * 100) / 100; //Y2
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[2][iT-3].first;
                                        it->second.second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[2][iT-3].second;
                                        continue;
                                    }
                                }
                                it->second.first.m_iaThermocoupleTmp[iT - 12] = iBibTmp;
                            } else {
                                it->second.first.m_iaThermocoupleTmp[iT - 12] = (short)ReadLowByteInt(PayData+iT*2, 2); //热电偶
                                it->second.second.m_iaThermocoupleTmp[iT - 12].data = it->second.first.m_iaThermocoupleTmp[iT - 12]; // 记录原始数据
                            }
                        }
                    }
                }
                else if(iChn1 == 3) //功率和电压 (PGB功率电阻、BIB功率电阻、单片机供电电压、DAC电压各占4字节 + 8个通道ADC采集电压(8 * 4)) -> (1+1+1+1+8)
                {
                    float* value = (float*)PayData;
                    for (int iT = 0; iT < iPayLen / 4) && iT < 12; iT++ )
                    {
                        //printf("dbg:vol %x %x %x %x", PayData[iT*4], PayData[iT*4+1], PayData[iT*4+2], PayData[iT*4+3]);
                        if (iT < 2) {
                            //it->second.m_iaBdpw[iT] = ReadLowByteInt(PayData+iT*4,4); //依次取出各个电压
                            it->second.first.m_iaBdpw[iT] = value[iT]; //依次取出各个电压
                        } else if (iT < 4) {
                            //it->second.m_iaBdVol[iT-2] = ReadLowByteInt(PayData+iT*4,4); //依次取出各个电压
                            it->second.first.m_iaBdVol[iT-2] = value[iT]; //依次取出各个电压
                        } else {
                            it->second.first.m_iaAdcData[iT - 4] = value[iT];
                        }
                    }
                }
                else if(iChn1 == 4) //单板id (4字节bib id 、1字节pgb id、1字节vout告警、1字节PGB warm、1字节Bib warm、1字节PGB power vol.)
                {
                    if (iPayLen < 9) {
                        std::cout << "iChn1 == 4 data is not correct" << std::endl;
                        return;
                    }
                    it->second.first.m_iBibId = (int)ReadLowByteInt(PayData,4); //bib id
                    it->second.first.m_iPgbId = (unsigned char)ReadLowByteInt(PayData + 4,1); //slot号
                    it->second.first.m_iVoutAlarm = (unsigned char)ReadLowByteInt(PayData + 5,1); //Vout alarm
                    it->second.first.m_iPgbWarm = (unsigned char)ReadLowByteInt(PayData + 6,1); //PGB warm
                    it->second.first.m_iBibWarm = (unsigned char)ReadLowByteInt(PayData + 7,1); //BIB warm
                    it->second.first.m_iPgbPowerVol = (unsigned char)ReadLowByteInt(PayData + 8,1); // Pbg power vol
                }
            } else {
                printf("add to mboards pgbid:%d\n", iPgbSlotId);
                // 单板不存在添加单板数据
                if (0 != ConfigureCalibrationMap())
                {
                    std::cout << "configure calibration file failed!!!" << std::endl;
                }
                mboards[iPgbSlotId].first = XBoardData();
                mboards[iPgbSlotId].first.m_Ip = sFromAddr;
                mboards[iPgbSlotId].first.m_iSlotId = iPgbSlotId;
                mboards[iPgbSlotId].first.m_iBoardStatus = true;
                mboards[iPgbSlotId].first.m_iLastTime = std::time(nullptr);
                mboards[iPgbSlotId].first.m_iPayState = iPayState;
                mboards[iPgbSlotId].first.m_iBibId = iBibBoardId;
                //mboards[iPgbSlotId].m_iBibBoardId = iBibBoardId;
                if(iChn1 == 1)    //PGB温度和湿度 (8+1+1)
                {
                    for(int iT = 0; iT < iPayLen/2 && iT < 10; iT++)
                    {
                        if(iT < 8)
                        {
                            mboards[iPgbSlotId].first.m_iaPgbTmp_lm[iT] = (short)ReadLowByteInt(PayData+iT*2, 2); //lm75b
                        }
                        else if(iT < 9)
                        {
                            mboards[iPgbSlotId].first.m_iaPgbSmokeTmp = (short)ReadLowByteInt(PayData+iT*2, 2); //烟感温度
                        }
                        else
                        {
                            mboards[iPgbSlotId].first.m_iaPgbHumidity = (short)ReadLowByteInt(PayData+iT*2, 2); //湿度
                        }
                    }
                }
                else if(iChn1 == 2) //BIB通道
                {
                    std::map<int,std::vector<std::pair<double, double>>> cal_data;
                    mboards[iPgbSlotId].first.m_iBibStatus = (short)ReadLowByteInt(PayData,2);
                    if (0 == mboards[iPgbSlotId].first.m_iBibStatus) {
                        std::cout << "BIB Board is not exist, temp is invalid" << std::endl;
                        return;
                    }
                    for(int iT = 1; iT < iPayLen / 2 && iT < 21; iT++)
                    {
                        if(iT < 3)
                        {
                            if (iT == 1) {
                                mboards[iPgbSlotId].first.m_iBibId = (int)ReadLowByteInt(PayData+iT*2,4); //bib id
                                cal_data = find_data(mboards[iPgbSlotId].first.m_iBibId);
                                if (cal_data.size() > 0) {
                                    std::cout << "cal_data size:" << cal_data.size() << std::endl;
                                    for(auto iter : cal_data) {
                                        std::cout << mboards[iPgbSlotId].first.m_iBibId << " " << iter.first << " ";
                                        for(auto data : iter.second) {
                                            std::cout << (double)data.first << " " << (double)data.second << " ";
                                        }
                                        std::cout << std::endl;
                                    }
                                } else {
                                    std::cout << "Error: new_board can't find this bibid in calibration_map:bibid(" << mboards[iPgbSlotId].first.m_iBibId << std::endl;
                                }
                            }
                        }
                        else if(iT < 12)
                        {
                            if(cal_data.size())
                            {
                                int iBibTmp = (short)ReadLowByteInt(PayData+iT*2,2);
                                mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].data = iBibTmp;
                                if (iBibTmp < 1500) {
                                    if (cal_data.find(1) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[1][iT-3].first + cal_data[1][iT-3].second * 100) / 100; //Y1
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].factor = cal_data[1][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].offset = cal_data[1][iT-3].second;
                                        continue;
                                    }
                                } else if (iBibTmp > 12000) {
                                    if (cal_data.find(3) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[3][iT-3].first + cal_data[3][iT-3].second * 100) / 100; //Y3
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].factor = cal_data[3][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].offset = cal_data[3][iT-3].second;
                                        continue;
                                    }
                                } else {
                                    if (cal_data.find(2) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] = (iBibTmp * cal_data[2][iT-3].first + cal_data[2][iT-3].second * 100) / 100; //Y2
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].factor = cal_data[2][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].offset = cal_data[2][iT-3].second;
                                        continue;
                                    }
                                }
                                mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] = iBibTmp;
                                std::cout << "Calibrate PgbSlot: " << (int)iPgbSlotId << " BibTmp:" << iT - 3 << " value:" << mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] << std::endl;
                            } else {
                                mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] = (short)ReadLowByteInt(PayData+iT*2,2); //依次取出int01各个温度
                                mboards[iPgbSlotId].second.m_iaBibTmp[iT - 3].data = mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3]; // 记录原始数据
                                std::cout << "Pgbslot: " << (int)iPgbSlotId << " " << " BidTmp:" << iT - 3 << " " << " value:" << mboards[iPgbSlotId].first.m_iaBibTmp[iT - 3] << std::endl;
                            }
                        }
                        else if(iT < 21)
                        {
                            if(cal_data.size())
                            {
                                int iBibTmp = (short)ReadLowByteInt(PayData+iT*2,2);
                                mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].data = iBibTmp;
                                if (iBibTmp < 1500) {
                                    if (cal_data.find(1) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[1][iT-3].first + cal_data[1][iT-3].second * 100) / 100; //Y1
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[1][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[1][iT-3].second;
                                        continue;
                                    }
                                } else if (iBibTmp > 12000) {
                                    if (cal_data.find(3) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[3][iT-3].first + cal_data[3][iT-3].second * 100) / 100; //Y3
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[3][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[3][iT-3].second;
                                        continue;
                                    }
                                } else {
                                    if (cal_data.find(2) != cal_data.end()) {
                                        mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] = (iBibTmp * cal_data[2][iT-3].first + cal_data[2][iT-3].second * 100) / 100; //Y2
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].factor = cal_data[2][iT-3].first;
                                        mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].offset = cal_data[2][iT-3].second;
                                        continue;
                                    }
                                }
                                mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] = iBibTmp;
                            } else {
                                mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] = (short)ReadLowByteInt(PayData+iT*2, 2); //热电偶
                                mboards[iPgbSlotId].second.m_iaThermocoupleTmp[iT - 12].data = mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12];
                                std::cout << "Pgbslot:" << (int)iPgbSlotId << " " << "ThermocoupleTmp:" << iT - 12 << " " << " value:" << mboards[iPgbSlotId].first.m_iaThermocoupleTmp[iT - 12] << std::endl;
                            }
                        }
                    }
                }
                else if(iChn1 == 3) //电压通道
                {
                    float* value = (float*)PayData;
                    for (int iT = 0; iT < iPayLen / 4 && iT < 12; iT++)
                    {
                        if (iT < 2)
                        {
                            mboards[iPgbSlotId].first.m_iaBdpw[iT] = value[iT]; //依次取出各个电压
                        }
                        else if (iT < 4)
                        {
                            mboards[iPgbSlotId].first.m_iaBdVol[iT-2] = value[iT]; //依次取出各个电压
                        }
                        else
                        {
                            mboards[iPgbSlotId].first.m_iaAdcData[iT - 4] = value[iT];
                        }
                    }
                }
                else if(iChn1 == 4) //单板id
                {
                    if (iPayLen < 9) {
                        std::cout << "iChn1 == 4 data is not correct" << std::endl;
                        return;
                    }
                    mboards[iPgbSlotId].first.m_iBibId = (int)ReadLowByteInt(PayData,4); //bib id
                    mboards[iPgbSlotId].first.m_iPgbId = (unsigned char)ReadLowByteInt(PayData + 4,1); //slot号
                    mboards[iPgbSlotId].first.m_iVoutAlarm = (unsigned char)ReadLowByteInt(PayData + 5,1); //Vout alarm
                    mboards[iPgbSlotId].first.m_iPgbWarm = (unsigned char)ReadLowByteInt(PayData + 6,1); //PGB warm
                    mboards[iPgbSlotId].first.m_iBibWarm = (unsigned char)ReadLowByteInt(PayData + 7,1); //BIB warm
                    mboards[iPgbSlotId].first.m_iPgbPowerVol = (unsigned char)ReadLowByteInt(PayData + 8,1); // Pgb power vol
                }
            }
        }
    }

    std::map<int,vector<std::pair<double, double>>> find_data(int id)
    {
        std::unique_lock<std::mutex> lock(calibration_map_mutex);
        auto itdata = calibration_map.find(id);
        if(itdata == calibration_map.end()) return std::map<int,vector<std::pair<double, double>>>();
        return itdata->second;
    }

    int ConfigureCalibrationMap(bool already_locked = false)
    {
        std::unique_lock<std::mutex> lock(calibration_map_mutex, std::defer_lock);
        if (!already_locked)
        {
            lock.lock();
        }
        if (!calibration_map.empty()) { // 校准数据非空，无需再次读取
            return 0;
        }
        std::string data_def_file = ("/usr/share/zcqdemo/ConfigureUniCalibrationMap.csv");
        FILE *file_fd = fopen(data_def_file.c_str(),"r");
        if(!file_fd) {
            std::cout << "Error: open /usr/share/zcqdemo/ConfigureUniCalibrationMap.csv failed, please check this file!!!" << std::endl;
            return 0; // 如果校准文件打开失败，不再返回错误，因为有不存在校准文件的场景，只打印错误日志
        }
        char read_buff[1024] = {0};
        while(fgets(read_buff,sizeof(read_buff)-1,file_fd))
        {
            std::string line_str(read_buff);
            if (line_str.empty() || line_str[0] == '[' || line_str[0] == ',') { // 解决空行问题
                continue;
            }
            trim(line_str); // 去掉前后的空白字符
            std::vector<std::string> vct_cell = split_single_char_delim(line_str, ',');
            if(vct_cell.size() >= 4)
            {
                int id = (int)strtol(vct_cell[0].c_str(),NULL,16);
                int domin = (int)strtol(vct_cell[1].c_str(),NULL,10);
                std::vector<std::pair<double, double>> cal_value;
                double first,second;
                for(int i = 2; i < vct_cell.size(); i=i+2)
                {
                    first = (double)strtod(vct_cell[i].c_str(), NULL);
                    second = (double)strtod(vct_cell[i+1].c_str(), NULL);
                    cal_value.emplace_back(first,second);
                }
                if(cal_value.size() == 9 || cal_value.size() == 18) //热模拟板上传感器数量
                {
                    calibration_map[id][domin] = cal_value;
                }
            }
        }
        if (fclose(file_fd) != 0) { // 关闭文件
            perror("Failed to close file");
            return -1;
        }
        return 0;
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

    bool checkDir(const std::string& path) {
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

    int load_config_calibration_file(const struct mg_str& body) {
        const char* TARGET_FILE = "/usr/share/zcqdemo/ConfigureUniCalibrationMap.csv";
        const char* TARGET_DIR = "/usr/share/zcqdemo";
        // 解析 multipart/form-data
        std::map<std::string, std::string> text_fields;
        std::map<std::string, std::vector<char>> file_data;
        std::map<std::string, std::string> filenames;
        // 使用mongoose的multipart解析函数，循环解析每个part
        struct mg_http_part part;
        size_t ofs = 0;

        while ((ofs = mg_http_next_multipart(body, ofs, &part)) != 0) {
            std::string name(part.name.buf, part.name.len);

            if (part.filename.len > 0) {
                std::string filename(part.filename.buf, part.filename.len);
                std::vector<char> file_content(part.body.buf, part.body.buf + part.body.len);
                file_data[name] = file_content;
                filenames[name] = filename;
                std::cout << "Found calibration file field: " << name
                        << ", filename: " << filename
                        << ", size: " << file_content.size() << " bytes" << std::endl;
            } else {
                // 文本字段
                std::string value(part.body.buf, part.body.len);
                text_fields[name] = value;
                std::cout << "Found calibration text field: " << name << " = " << value << std::endl;
            }
        }

        // 字段校验
        if (file_data.find("file") == file_data.end() ||
            filenames.find("file") == filenames.end() ||
            text_fields.find("md5") == text_fields.end())
        {
            std::cerr << "Calibration form data is missing! Required fields: file, md5" << std::endl;
            return -1;
        }

        std::string expected_md5 = text_fields["md5"];
        std::vector<char>& tmp_data = file_data["file"];
        // 计算文件实际MD5
        std::string actual_md5 = xcryto::get_md5(std::string(tmp_data.begin(), tmp_data.end()));
        if (actual_md5.length() != 32) {
            std::cerr << "Calibration file MD5 calculate error" << std::endl;
            return -1;
        }
        std::cout << "actual MD5: " << actual_md5 << std::endl;
        if (actual_md5 != expected_md5) {
            std::cerr << "Error: MD5 mismatch!" << std::endl;
            std::cerr << "Expected:" << expected_md5 << std::endl;
            std::cerr << "Actual:" << actual_md5 << std::endl;
            return -1;
        }
        std::cout << "MD5 verification passed" << std::endl;
        // 写入文件到目标路径
        std::lock_guard<std::mutex> lock(calibration_map_mutex);
        if (!checkDir(TARGET_DIR)) {
            std::cerr << "Error: Failed to create directory: " << TARGET_DIR << std::endl;
            return -1;
        }
        // 删除目录下所有文件
        deleteDirectory(TARGET_DIR);
        // 直接写入二进制内容
        if (!saveFile(TARGET_FILE, tmp_data)) {
            std::cerr << "Error: Failed to save calibration file: " << TARGET_FILE << std::endl;
            return -1;
        }
        std::cout << "Calibration file saved successfully: " << TARGET_FILE << std::endl;

        // 清空内存表，重新加载，当前已经持有 calibration_map_mutex，传 true
        calibration_map.clear();
        int reload_ret = ConfigureCalibrationMap(true);
        if (reload_ret != 0) {
            std::cout << "Error: Failed to reload calibration map" << std::endl;
            return -1;
        }

        std::cout << "Calibration map updated, total entries:" << calibration_map.size() << std::endl;
        std::string check_result = check_calibration_to_string(); 
        std::cout << check_result << std::endl;
        return 0;
    }
    
private:
    std::map<int,std::pair<XBoardData, XBoardTmpData>> mboards;
    std::mutex data_rw_mutex;
    std::map<int, std::map<int, std::vector<std::pair<double, double>>>> calibration_map;
    std::mutex calibration_map_mutex;
    std::atomic<bool> running;
    std::mutex mtx;
    std::condition_variable cv;
};
#endif // XBOARDDATA_H