/*
 * host_sniffer.c - HTTP/HTTPS 目标 Host 嗅探组件
 *
 * 用于中转代理程序，从原始 TCP 数据流的首包中提取目标主机:
 *   - HTTP  请求: 解析 Host 头 (支持 CONNECT 方法)
 *   - HTTPS 请求: 解析 TLS ClientHello 中的 SNI (Server Name Indication)
 *
 * 编译: gcc -c host_sniffer.c -Wall -O2
 */

#include "sniff_host.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/* ============================================================
 *  内部工具函数
 * ============================================================ */

/* 不区分大小写比较 n 个字节 */
static int casencmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* 跳过前导空白 */
static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/*
 * 从 "host[:port]" 字符串中分离 host 和 port
 * 支持 IPv6 地址 [::1]:port 格式
 */
static int split_host_port(const char *hostport, size_t len,
                           sniff_result_t *r, uint16_t default_port)
{
    const char *end = hostport + len;
    const char *colon = NULL;
    const char *h_start = hostport;
    const char *h_end = end;

    /* IPv6 字面地址: [xxxx::xxxx]:port */
    if (*h_start == '[') {
        const char *bracket = memchr(h_start, ']', len);
        if (!bracket) return SNIFF_ERR_PARSE;
        h_start++;                     /* 跳过 '[' */
        h_end = bracket;               /* host 到 ']' 之前 */
        if (bracket + 1 < end && bracket[1] == ':')
            colon = bracket + 1;
    } else {
        /* 普通域名或 IPv4: 从右往左找冒号 */
        for (const char *p = end - 1; p > hostport; p--) {
            if (*p == ':') { colon = p; break; }
        }
    }

    /* 如果找到了端口分隔符，host 截止到冒号 */
    if (colon) h_end = colon;

    /* 拷贝 host */
    size_t hlen = (size_t)(h_end - h_start);
    if (hlen == 0 || hlen >= MAX_HOST_LEN) return SNIFF_ERR_OVERFLOW;
    memcpy(r->host, h_start, hlen);
    r->host[hlen] = '\0';

    /* 拷贝/设置 port */
    if (colon && colon + 1 < end) {
        size_t plen = (size_t)(end - colon - 1);
        if (plen == 0 || plen >= MAX_PORT_LEN) return SNIFF_ERR_OVERFLOW;
        uint8_t port[8] = { 0 };
        memcpy(port, colon + 1, plen);
        r->port_num = (uint16_t) atoi((const char *) port);
    } else {
        r->port_num = default_port;
    }
    return SNIFF_OK;
}

/* ============================================================
 *  HTTP Host 嗅探
 * ============================================================ */

/*
 * 判断 buf 是否以 HTTP 方法开头
 * 返回: 1 = 是 HTTP, 0 = 不是
 */
static int is_http(const uint8_t *buf, size_t len)
{
    /* 常见 HTTP 方法 */
    static const char *methods[] = {
        "GET ", "POST ", "PUT ", "DELETE ", "HEAD ",
        "OPTIONS ", "PATCH ", "CONNECT ", "TRACE ",
        NULL
    };
    for (int i = 0; methods[i]; i++) {
        size_t mlen = strlen(methods[i]);
        if (len >= mlen && memcmp(buf, methods[i], mlen) == 0)
            return 1;
    }
    return 0;
}

/*
 * 判断请求行是否为 CONNECT 方法
 * CONNECT host:port HTTP/1.x
 */
static int is_connect_method(const uint8_t *buf, size_t len)
{
    return (len >= 8 && memcmp(buf, "CONNECT ", 8) == 0);
}

int sniff_http_host(const uint8_t *buf, size_t buf_len, sniff_result_t *result)
{
    if (!buf || !result) return SNIFF_ERR_NULLPTR;
    if (buf_len < 16)   return SNIFF_ERR_TOO_SHORT;
    if (!is_http(buf, buf_len)) return SNIFF_ERR_NOT_FOUND;

    memset(result, 0, sizeof(*result));
    result->proto = PROTO_HTTP;

    /*
     * CONNECT 方法特殊处理:
     *   "CONNECT example.com:443 HTTP/1.1\r\n"
     *   直接从请求行取 host:port
     */
    if (is_connect_method(buf, buf_len)) {
        result->proto = PROTO_HTTPS;  /* CONNECT 通常用于 HTTPS 隧道 */
        const char *start = (const char *)buf + 8;
        const char *eol = memchr(start, '\r', buf_len - 8);
        if (!eol) eol = memchr(start, '\n', buf_len - 8);
        if (!eol) return SNIFF_ERR_PARSE;

        /* 找到 host:port 和 " HTTP/" 之间的部分 */
        const char *space = memchr(start, ' ', (size_t)(eol - start));
        if (!space) return SNIFF_ERR_PARSE;

        return split_host_port(start, (size_t)(space - start), result, 443);
    }

    /*
     * 普通 HTTP 请求: 搜索 "Host:" 头
     * 逐行查找，不区分大小写
     */
    const char *p   = (const char *)buf;
    const char *end = (const char *)buf + buf_len;

    /* 跳过请求行 */
    const char *line = memchr(p, '\n', (size_t)(end - p));
    if (!line) return SNIFF_ERR_PARSE;
    line++;

    while (line < end) {
        /* 遇到空行则 header 结束 */
        if (line[0] == '\r' || line[0] == '\n') break;

        const char *next = memchr(line, '\n', (size_t)(end - line));
        if (!next) next = end;

        size_t line_len = (size_t)(next - line);
        /* 去掉尾部 \r */
        if (line_len > 0 && line[line_len - 1] == '\r') line_len--;

        /* 匹配 "Host:" (不区分大小写) */
        if (line_len > 5 && casencmp(line, "host:", 5) == 0) {
            const char *val = skip_space(line + 5);
            size_t vlen = line_len - (size_t)(val - line);
            /* 去掉尾部空白 */
            while (vlen > 0 && (val[vlen-1] == ' ' || val[vlen-1] == '\t'))
                vlen--;

            if (vlen == 0) return SNIFF_ERR_NOT_FOUND;
            return split_host_port(val, vlen, result, 80);
        }

        line = next + 1;
    }

    /*
     * 备选: 如果请求行中有绝对 URI, 例如:
     *   GET http://example.com/path HTTP/1.1
     * 尝试从中提取 host
     */
    p = (const char *)buf;
    const char *buf_end = (const char *)buf + buf_len;
    const char *uri = memchr(p, ' ', buf_len);
    if (uri) {
        uri = skip_space(uri);
        if ((size_t)(buf_end - uri) > 7 && casencmp(uri, "http://", 7) == 0) {
            const char *host_start = uri + 7;
            const char *slash = memchr(host_start, '/', (size_t)(buf_end - host_start));
            const char *sp = memchr(host_start, ' ', (size_t)(buf_end - host_start));
            const char *host_end = buf_end;
            if (slash && slash < host_end) host_end = slash;
            if (sp && sp < host_end) host_end = sp;
            if (host_end > host_start) {
                return split_host_port(host_start,
                    (size_t)(host_end - host_start), result, 80);
            }
        }
    }

    return SNIFF_ERR_NOT_FOUND;
}

/* ============================================================
 *  TLS SNI 嗅探
 * ============================================================ */

/*
 * TLS Record Layer:
 *   byte 0:     ContentType (0x16 = Handshake)
 *   byte 1-2:   Version (0x0301 = TLS 1.0, 0x0303 = TLS 1.2 ...)
 *   byte 3-4:   Length (big-endian)
 *   byte 5...:  Handshake payload
 *
 * Handshake: ClientHello:
 *   byte 0:     HandshakeType (0x01 = ClientHello)
 *   byte 1-3:   Length (24-bit big-endian)
 *   byte 4-5:   ClientVersion
 *   byte 6-37:  Random (32 bytes)
 *   byte 38:    SessionID length (N)
 *   byte 39...: SessionID (N bytes)
 *   ...         CipherSuites, CompressionMethods
 *   ...         Extensions
 *
 * SNI Extension (type = 0x0000):
 *   2 bytes: extension type (0x0000)
 *   2 bytes: extension data length
 *     2 bytes: server name list length
 *       1 byte:  name type (0x00 = host_name)
 *       2 bytes: name length
 *       N bytes: name
 */

/* 大端读取 2 字节 */
static inline uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

/* 大端读取 3 字节 (24-bit) */
static inline uint32_t rd24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

int sniff_tls_sni(const uint8_t *buf, size_t buf_len, sniff_result_t *result)
{
    if (!buf || !result) return SNIFF_ERR_NULLPTR;
    if (buf_len < 5)    return SNIFF_ERR_TOO_SHORT;

    memset(result, 0, sizeof(*result));
    result->proto = PROTO_HTTPS;

    /* --- TLS Record Layer --- */
    if (buf[0] != 0x16) return SNIFF_ERR_NOT_FOUND;  /* 非 Handshake */

    uint16_t tls_ver = rd16(buf + 1);
    /* 宽松检查: 0x0300 (SSL3.0) ~ 0x0304 (TLS 1.3) */
    if (tls_ver < 0x0300 || tls_ver > 0x0304)
        return SNIFF_ERR_NOT_FOUND;

    uint16_t record_len = rd16(buf + 3);
    size_t total = 5 + (size_t)record_len;
    if (buf_len < total && buf_len < 5 + (size_t)record_len) {
        /* 允许截断的包，但至少要有 record header + handshake header */
    }
    /* 实际可用长度 */
    size_t avail = (buf_len < total) ? buf_len : total;

    const uint8_t *p   = buf + 5;
    const uint8_t *end = buf + avail;

    /* --- Handshake: ClientHello --- */
    if (p + 4 > end) return SNIFF_ERR_TOO_SHORT;
    if (p[0] != 0x01) return SNIFF_ERR_NOT_FOUND;  /* 非 ClientHello */

    uint32_t hs_len = rd24(p + 1);
    (void)hs_len; /* 用 avail 限制即可 */
    p += 4;

    /* ClientVersion (2) + Random (32) = 34 bytes */
    if (p + 34 > end) return SNIFF_ERR_TOO_SHORT;
    p += 34;

    /* Session ID */
    if (p + 1 > end) return SNIFF_ERR_TOO_SHORT;
    uint8_t sid_len = p[0];
    p += 1 + sid_len;
    if (p > end) return SNIFF_ERR_TOO_SHORT;

    /* Cipher Suites */
    if (p + 2 > end) return SNIFF_ERR_TOO_SHORT;
    uint16_t cs_len = rd16(p);
    p += 2 + cs_len;
    if (p > end) return SNIFF_ERR_TOO_SHORT;

    /* Compression Methods */
    if (p + 1 > end) return SNIFF_ERR_TOO_SHORT;
    uint8_t cm_len = p[0];
    p += 1 + cm_len;
    if (p > end) return SNIFF_ERR_TOO_SHORT;

    /* Extensions */
    if (p + 2 > end) return SNIFF_ERR_NOT_FOUND; /* 无扩展 */
    uint16_t ext_total = rd16(p);
    p += 2;

    const uint8_t *ext_end = p + ext_total;
    if (ext_end > end) ext_end = end;

    while (p + 4 <= ext_end) {
        uint16_t ext_type = rd16(p);
        uint16_t ext_len  = rd16(p + 2);
        p += 4;

        if (p + ext_len > ext_end) break;

        if (ext_type == 0x0000) {
            /* SNI Extension */
            const uint8_t *sni = p;
            const uint8_t *sni_end = p + ext_len;

            if (sni + 2 > sni_end) return SNIFF_ERR_PARSE;
            uint16_t sni_list_len = rd16(sni);
            (void)sni_list_len;
            sni += 2;

            while (sni + 3 <= sni_end) {
                uint8_t name_type = sni[0];
                uint16_t name_len = rd16(sni + 1);
                sni += 3;

                if (sni + name_len > sni_end) return SNIFF_ERR_PARSE;

                if (name_type == 0x00) {
                    /* host_name */
                    if (name_len == 0 || name_len >= MAX_HOST_LEN)
                        return SNIFF_ERR_OVERFLOW;

                    memcpy(result->host, sni, name_len);
                    result->host[name_len] = '\0';
                    result->port_num = 443;
                    return SNIFF_OK;
                }

                sni += name_len;
            }
            /* SNI 扩展中未找到 host_name 类型 */
            return SNIFF_ERR_NOT_FOUND;
        }

        p += ext_len;
    }

    return SNIFF_ERR_NOT_FOUND;
}

/* ============================================================
 *  统一入口
 * ============================================================ */

int sniff(const uint8_t *buf, size_t buf_len, sniff_result_t *result)
{
    if (!buf || !result) return SNIFF_ERR_NULLPTR;
    if (buf_len < 3)    return SNIFF_ERR_TOO_SHORT;

    /*
     * 快速分流:
     *   - 0x16 开头 → TLS Handshake → 尝试 SNI
     *   - ASCII 字母开头 → 可能是 HTTP 方法
     */
    if (buf[0] == 0x16) {
        return sniff_tls_sni(buf, buf_len, result);
    }

    if (isalpha(buf[0])) {
        return sniff_http_host(buf, buf_len, result);
    }

    return SNIFF_ERR_NOT_FOUND;
}
