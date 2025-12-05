#include <iostream>
#include <arpa/inet.h>

/**
 * @brief 测试大小端序
 * 
 */
void testEndianness() {
    uint16_t a = 0x1234;
    // uint8_t b = a; // 错了，这本质上是类型转换，会直接丢弃高位数据，所以肯定是0x34。
    uint8_t* b = reinterpret_cast<uint8_t*>(&a);
    if (*b == 0x12) {
        std::cout << "大端序：高位数据在低位地址.\n"; //符合人类习惯；网络传输使用
    }
    else if (*b == 0x34) {
        std::cout << "小端序：高位数据在高位地址.\n"; //更适合cpu计算
    }
    else {
        std::cout << "出错啦!.\n";
    }
}

/**
 * @brief 端序转换
 * 
 */
void endiannessChange() {
    uint16_t port = 8888;
    std::cout << "8888转换为大端序后输出结果: " << htons(port) << std::endl;
    uint32_t ip = 0xFFFFFF00;//255.255.255.0
    std::cout << "255.255.255.0转换为大端序后输出结果: " << htonl(ip) << std::endl;
}

/**
 * @brief ip转换函数
 * 
 */
void ipChange() {
    char ip_str[] = "192.168.0.1";
    struct in_addr addr;

    //字符串 --> 二进制
    if(inet_pton(AF_INET, ip_str, &addr) == 1) {
        printf("192.168.0.1转换为二进制后为:0x%x \n", addr.s_addr);
    }

    //二进制 --> 字符串
    char ip_str2[INET_ADDRSTRLEN];
    if(inet_ntop(AF_INET, &addr.s_addr, ip_str2, INET_ADDRSTRLEN) != NULL) {
        printf("0x%x 转换为字符串后为: %s\n", addr.s_addr, ip_str2);
    }

}

int main() {
    testEndianness();
    endiannessChange();
    ipChange();
    return 0;
}
