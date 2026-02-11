#ifndef HOST_SNIFFER_H
#define HOST_SNIFFER_H

#include <stdint.h>
#include <stddef.h>

#define SNIFF_OK            0
#define SNIFF_ERR_NULLPTR  -1
#define SNIFF_ERR_TOO_SHORT -2
#define SNIFF_ERR_NOT_FOUND -3
#define SNIFF_ERR_PARSE     -4
#define SNIFF_ERR_OVERFLOW  -5

#define MAX_HOST_LEN  256
#define MAX_PORT_LEN  8

typedef enum {
    PROTO_UNKNOWN = 0,
    PROTO_HTTP,
    PROTO_HTTPS,
} proto_type_t;

typedef struct {
    char         host[MAX_HOST_LEN];   /* 目标主机名 (不含端口) */
    // char         port[MAX_PORT_LEN];   /* 目标端口字符串 */
    uint16_t     port_num;             /* 目标端口数值 */
    proto_type_t proto;                /* 协议类型 */
} sniff_result_t;

/*
 * sniff_host - 从 buf 中嗅探目标 Host
 *
 * 自动判断 HTTP / HTTPS (TLS ClientHello SNI):
 *   - HTTP:  解析请求行 + Host 头
 *   - HTTPS: 解析 TLS ClientHello 中的 SNI 扩展
 *
 * @buf:      数据缓冲区
 * @buf_len:  缓冲区长度
 * @result:   输出结果
 *
 * 返回: SNIFF_OK 成功, 负值失败
 */
int sniff(const uint8_t *buf, size_t buf_len, sniff_result_t *result);

/* 单独接口: 仅嗅探 HTTP */
int sniff_http_host(const uint8_t *buf, size_t buf_len, sniff_result_t *result);

/* 单独接口: 仅嗅探 TLS SNI */
int sniff_tls_sni(const uint8_t *buf, size_t buf_len, sniff_result_t *result);

#endif /* HOST_SNIFFER_H */
