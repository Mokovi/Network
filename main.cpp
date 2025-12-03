#include<iostream>
#include<cstdio>
#include<arpa/inet.h>

union TestOrderUion
{
    int a;
    char b;
};

/// @brief 测试主机大小端序
void testOrder(){
    TestOrderUion to;
    to.a = 0x12345678;
    printf("a = %x\n",to.a);
    printf("b = %x\n",to.b);
    std::cout << "当前主机为" << ((to.b == 0x78) ? "小端序" : "大端序") << ".\n";
}
/**
 * @brief 端序转换函数
 * hton() ntohl()
 */
void orderConv(){
    int c = 0x12345678;
    auto d = htonl(c);
    printf("c = %x\n",c);
    printf("c = %x\n",d);
    printf("c = %x\n",ntohl(d));
}
/**
 * @brief IP地址字符串与二进制转换
 * inet_pton() inet_ntop()
 */
void ipaddrConv(){
    const char* ipstr = "192.168.1.1";
    uint32_t ip_b;
    inet_pton(AF_INET, ipstr, &ip_b);
    std::cout << "ipstr = " << ipstr << std::endl;
    printf("ip_b = %x\n",ip_b);
    printf("ip_b_low = %x\n",ntohl(ip_b));
    char* ips_conv;
    ips_conv = (char*)malloc(sizeof(char)*16);
    inet_ntop(AF_INET, &ip_b, ips_conv, 16);
    std::cout << "ips_conv = " << ips_conv << std::endl;
    free(ips_conv);
}

int main(){
    //testOrder();
    orderConv();
    ipaddrConv();
    return 0;
}