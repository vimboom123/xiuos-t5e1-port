#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 scrapling CLI 抓 XiUOS 资料，逐页落盘到 D:\\ds\\tuya\\research\\pages\\。"""
import concurrent.futures as cf
import hashlib
import json
import os
import re
import subprocess
import sys
import time

SCR = os.path.expanduser(r'~\.local\bin\scrapling.exe')
OUT = r'D:\ds\tuya\research\pages'
os.makedirs(OUT, exist_ok=True)
LOG = open(r'D:\ds\tuya\research\crawl.log', 'a', encoding='utf-8')


def fname(url, ext):
    h = hashlib.md5(url.encode()).hexdigest()[:8]
    slug = re.sub(r'[^A-Za-z0-9]+', '_', re.sub(r'^https?://', '', url))[:70]
    return os.path.join(OUT, '%s_%s.%s' % (slug, h, ext))


def grab(url, mode='get', ext='md', timeout=90):
    out = fname(url, ext)
    if os.path.exists(out) and os.path.getsize(out) > 200:
        return url, out, 'cached'
    cmd = [SCR, 'extract', mode, url, out]
    if mode == 'get':
        cmd += ['--timeout', '40']
    elif mode in ('fetch', 'stealthy-fetch'):
        cmd += ['--network-idle']
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=timeout)
        ok = os.path.exists(out) and os.path.getsize(out) > 0
        LOG.write('%s %s %s %s\n' % (time.strftime('%H:%M:%S'), mode, 'OK' if ok else 'FAIL', url))
        if not ok:
            LOG.write((r.stderr or r.stdout or '')[-300:] + '\n')
        return url, out if ok else None, mode
    except Exception as e:
        LOG.write('%s %s EXC %s %s\n' % (time.strftime('%H:%M:%S'), mode, url, e))
        return url, None, mode


# ---------- 1. xuos.io 全站文档 ----------
SECTIONS = ['/doc/intro.html', '/doc/hardware/', '/doc/kernel/', '/doc/component/', '/doc/framework/',
            '/doc/appdev/', '/doc/demo/', '/newsevents/index.html', '/resource/tech.html',
            '/resource/download.html', '/about/xuos.html', '/about/license.html', '/about/contact.html',
            '/community/contrib.html']
BASE = 'https://xuos.io'
seen = set()
queue = [BASE + s for s in SECTIONS]
doc_pages = []
while queue and len(doc_pages) < 160:
    u = queue.pop(0)
    if u in seen:
        continue
    seen.add(u)
    _, html, _ = grab(u, 'get', 'html')
    if not html:
        continue
    doc_pages.append(u)
    text = open(html, encoding='utf-8', errors='replace').read()
    for href in re.findall(r'href="(/(?:doc|newsevents|resource|about|community)/[^"#?]*)"', text):
        nu = BASE + href
        if nu not in seen and nu not in queue:
            queue.append(nu)
print('xuos.io pages:', len(doc_pages))

with cf.ThreadPoolExecutor(6) as ex:
    md_ok = sum(1 for _, p, _ in ex.map(lambda u: grab(u, 'get', 'md'), doc_pages) if p)
print('xuos.io markdown saved:', md_ok)

# ---------- 2. 其它筛选出的网页 ----------
STATIC = [
    'https://cs.pku.edu.cn/info/1263/2366.htm',
    'https://www.cs.sjtu.edu.cn/NewsDetail.aspx?id=494',
    'https://www.ccf.org.cn/ccfdl/ccf_dl_focus/computer-architecture/volume4/zllb1/2022-07-26/766855.shtml',
    'https://www.ccf.org.cn/Media_list/gzwyh/jsjsysdwyh/2022-10-01/789795.shtml',
    'http://old2022.bulletin.cas.cn/publish_article/2022/1/20220105.htm',
    'https://news.pku.edu.cn/xwzh/01a30afa54f4436caf6eaf7fd7b5768c.htm',
    'https://news.pku.edu.cn/xwzh/1792a65e7188464e8914d3db27944994.htm',
    'https://cloud.tencent.com/developer/article/2251145',
    'http://www.21jingji.com/article/20220812/herald/8a53e21844e0294cbe2ac26794adc830.html',
    'https://github.com/xuos',
    'https://github.com/xuos/xiuos',
    'https://github.com/oscomp/proj252-JerryScript-for-XiUOS',
    'https://github.com/oscomp/proj253-SQLite-for-XiUOS',
    'https://github.com/oscomp/proj327-ESP32-C3-riscv32-mcu--for-XiUOS',
    'https://link.springer.com/article/10.1007/s11432-021-3294-y',
    'https://cacm.acm.org/opinion/toward-ubiquitous-operating-systems-lessons-from-the-field',
    'https://bulletinofcas.researchcommons.org/journal/vol38/iss4/12/',
    'https://bulletinofcas.researchcommons.org/journal/vol37/iss1/5/',
    'https://m.c114.com.cn/w52-1205291.html',
    'https://www.sohu.com/a/576447761_355140',
    'https://m.bjnews.com.cn/detail/1766148143019253.html',
    'https://www.dataarobotics.com/zh/blog/news-388.html',
    'https://netinfo-security.org/CN/abstract/abstract7246.shtml',
]
STEALTH = [
    'https://blog.csdn.net/AIIT_Ubiquitous/article/details/116209686',
    'https://blog.csdn.net/AIIT_Ubiquitous/article/details/116259873',
    'https://blog.csdn.net/AIIT_Ubiquitous/article/details/116210058',
    'https://blog.csdn.net/qq_15557181/article/details/121717200',
    'https://blog.csdn.net/qq_15557181/article/details/122282639',
    'https://blog.csdn.net/weixin_63630449/article/details/126811461',
    'https://zhuanlan.zhihu.com/p/676878515',
    'https://zhuanlan.zhihu.com/p/572104472',
    'https://www.bilibili.com/read/cv23906809/',
]
DYNAMIC = [
    'https://www.gitlink.org.cn/xuos/xiuos',
    'https://www.gitlink.org.cn/xuos/xuos-web',
    'https://www.gitlink.org.cn/wangtao/lwext4_filesystem_support_XiUOS',
    'https://www.sciengine.com/SCIS/doi/10.1007/s11432-021-3294-y?trans=true',
]
res = []
with cf.ThreadPoolExecutor(6) as ex:
    res += list(ex.map(lambda u: grab(u, 'get', 'md'), STATIC))
for u in STEALTH:
    res.append(grab(u, 'stealthy-fetch', 'md', timeout=150))
for u in DYNAMIC:
    res.append(grab(u, 'fetch', 'md', timeout=150))

manifest = [{'url': u, 'file': p, 'mode': m} for u, p, m in res] + \
           [{'url': u, 'file': fname(u, 'md'), 'mode': 'get'} for u in doc_pages]
json.dump(manifest, open(r'D:\ds\tuya\research\pages_manifest.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('other pages ok:', sum(1 for _, p, _ in res if p), '/', len(res))
