#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""涂鸦知识库会拒收含「敏感信息」的文档，上传前统一处理。用法: python sanitize_kb.py <目录>...

平台的检测部分由模型判定，实测会拦：邮箱、电话、URL 里像手机号的长数字、
设备序列号、QQ/微信用户号，以及「编号」一类占位符。所以这里不留占位符，
链接只保留域名。
"""
import glob
import os
import re
import sys
from urllib.parse import urlsplit

IP = r'(?:\d{1,3}\.){3}\d{1,3}'


def _host(url):
    host = urlsplit(url.replace('\\_', '_')).hostname or ''
    return '' if re.fullmatch(IP, host) else host


def _md_link(m):
    text, host = m.group(1), _host(m.group(2))
    return f'{text}（{host}）' if host and host not in text else text


def _bare_url(m):
    host = _host(m.group(0))
    return host or '内网地址'


RULES = [
    (r'(\d+(?:\.\d+)?mA)@(\d+(?:\.\d+)?V)', r'\1（\2）'),
    (r'(?:邮箱[：:\s]*)?[\w.+-]+@[\w-]+(?:\.[\w-]+)+', '（联系方式见官网联系页）'),
    (r'(?:电话[：:\s]*)?(?<!\d)0\d{2,3}-?\d{7,8}(?!\d)', '（电话见官网联系页）'),
    (r'(?<!\d)1[3-9]\d{9}(?!\d)', '（手机号已略）'),
    (r'\[([^\]]*)\]\((https?://[^)\s]+)\)', _md_link),
    (r'https?://[^\s)\]>）】，。、；]+', _bare_url),
    (r'\b((?:[\w-]+\.)+(?:com|net|cn|org|io))/[^\s)\]>）】，。、；]*', r'\1'),
    (r'(基金[^（\n]{0,12})（[\d,，、\s]+）', r'\1'),
    (r'<IP地址>', '本机地址'),
    (r'(?i)\([^)\n]*\)(\s*WIFI\s+(?:ssid|password))', r'(按实际网络填写)\1'),
    (r'code\\?_([\d.]+)-\d+\\?_amd64\.deb', r'VSCode \1 安装包'),
    (r'(?i)\bS/?N[:：]\s*\w+\s*', ''),
    (r'许可证编号[：:]\s*\d+', ''),
    (r'(?i)\b(qq|weixin)_\d{5,}', r'\1用户'),
    (r'(?<!\d)\d{9,}(?!\d)', ''),
    (rf'(?<![\d.]){IP}(?![\d.])', '内网地址'),
]


def clean(text):
    for p, r in RULES:
        text = re.sub(p, r, text)
    return text


if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    if len(args) >= 2 and args[0] == '-o':
        out, args = args[1], args[2:]
    for src in args:
        files = sorted(glob.glob(os.path.join(src, '*.md'))) if os.path.isdir(src) else [src]
        for f in files:
            t = open(f, encoding='utf-8').read()
            c = clean(t)
            dst = os.path.join(out, os.path.basename(f)) if out else f
            if c != t or out:
                open(dst, 'w', encoding='utf-8').write(c)
                print('cleaned' if c != t else 'copied', os.path.basename(f))
