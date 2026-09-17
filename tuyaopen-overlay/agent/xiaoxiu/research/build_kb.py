#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把抓下来的页面清洗、按主题合并成知识库文档，输出到 D:\\ds\\tuya\\research\\kb\\。"""
import hashlib
import json
import os
import re

ROOT = r'D:\ds\tuya\research'
PAGES = os.path.join(ROOT, 'pages')
KB = os.path.join(ROOT, 'kb')
os.makedirs(KB, exist_ok=True)

manifest = json.load(open(os.path.join(ROOT, 'pages_manifest.json'), encoding='utf-8'))


def md_path(url):
    h = hashlib.md5(url.encode()).hexdigest()[:8]
    slug = re.sub(r'[^A-Za-z0-9]+', '_', re.sub(r'^https?://', '', url))[:70]
    return os.path.join(PAGES, '%s_%s.md' % (slug, h))


def clean_xuos(text):
    m = re.search(r'\[#\]\([^)]*\)\s*', text)
    if m:
        text = text[m.start():]
    text = re.sub(r'\[#\]\([^)]*\)\s*', '', text)
    text = re.sub(r'\(opens new window\)', '', text)
    text = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', text)
    text = re.sub(r'\[([^\]]+)\]\((?:/|#)[^)]*\)', r'\1', text)
    text = re.split(r'\nLast Updated:', text)[0]
    text = re.sub(r'\n[←→].*', '', text)
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip()


JUNK = re.compile(r'登录|注册|关注|扫码|京ICP|ICP备|公网安备|版权所有|Copyright|©|首页|返回|English|分享|收藏|点赞|'
                  r'评论|举报|javascript|Cookie|cookie|广告|Advertisement|下载客户端|会员|Navigation Menu|'
                  r'Sign in|Sign up|Skip to')


def clean_generic(text):
    text = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', text)
    text = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'<https?://[^>]+>', '', text)
    out = []
    for ln in text.splitlines():
        s = ln.strip().strip('*').strip()
        if not s:
            out.append('')
            continue
        if JUNK.search(s) and len(s) < 120:
            continue
        if s.startswith(('#', '|', '```')):
            out.append(s)
            continue
        zh = len(re.findall(r'[\u4e00-\u9fff]', s))
        if zh >= 12 or (len(s) >= 60 and re.search(r'[.;:]', s)):
            out.append(s)
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(out)).strip()


def write(name, title, intro, parts):
    body = '# %s\n\n%s\n\n%s' % (title, intro, '\n'.join(parts))
    open(os.path.join(KB, name + '.md'), 'w', encoding='utf-8').write(body)
    print('%-44s 段数 %3d  字符 %7d' % (name, len(parts), len(body)))


# ---------- xuos.io 官网 ----------
GROUPS = [
    ('01_XiUOS概览与泛在操作系统研究计划', r'xuos\.io/(doc/intro|about/|newsevents)'),
    ('02_XiUOS硬件设备与开发板', r'xuos\.io/doc/hardware'),
    ('03_XiUOS系统内核', r'xuos\.io/doc/kernel'),
    ('04_XiUOS功能构件', r'xuos\.io/doc/component'),
    ('05_XiUOS应用框架', r'xuos\.io/doc/framework'),
    ('06_XiUOS应用开发与应用案例', r'xuos\.io/doc/(appdev|demo)'),
    ('07_XiUOS技术资料与社区', r'xuos\.io/(resource|community)'),
]
xuos_urls = sorted({m['url'] for m in manifest if 'xuos.io' in m['url']})
used = set()
for name, pat in GROUPS:
    parts = []
    for u in xuos_urls:
        if not re.search(pat, u) or u in used or not os.path.exists(md_path(u)):
            continue
        body = clean_xuos(open(md_path(u), encoding='utf-8', errors='replace').read())
        if len(body) < 80:
            continue
        used.add(u)
        parts.append('## 来源：%s\n\n%s\n' % (u, body))
    if parts:
        write(name, name.split('_', 1)[1],
              '资料来源：XiUOS 矽璓工业物联操作系统官网 xuos.io（木兰宽松许可证 MulanPSL-2.0），2026-09-17 抓取。', parts)

# ---------- 外部报道、论文、社区文章 ----------
SKIP = ('sohu.com', 'bjnews.com.cn', 'github.com/xuos', 'sciengine.com', 'lwext4')
parts = []
for m in manifest:
    u, p = m['url'], m.get('file')
    if not p or 'xuos.io' in u or any(k in u for k in SKIP) or not os.path.exists(p):
        continue
    body = clean_generic(open(p, encoding='utf-8', errors='replace').read())
    if len(body) < 300:
        continue
    parts.append('## 来源：%s\n\n%s\n' % (u, body[:20000]))
write('08_XiUOS外部报道论文与社区文章', 'XiUOS 与泛在操作系统：外部报道、论文与社区文章',
      '2026-09-17 抓取，各段标明原始网址，内容以原文为准。', parts)

# ---------- 希秀泛在 / Robonix ----------
SYS = [('https://www.sysoul.com/', 'pages/sysoul_home.html'),
       ('https://www.sysoul.com/solutions/course-materials', 'pages/www_sysoul_com_solutions_course_materials.md'),
       ('https://robonix.ai', 'pages/robonix_ai.md'),
       ('https://book.robonix.ai', 'pages/book_robonix_ai.md')]
parts = []
about = open(os.path.join(ROOT, 'sysoul_about.md'), encoding='utf-8').read() if os.path.exists(os.path.join(ROOT, 'sysoul_about.md')) else ''
if about:
    parts.append('## 来源：https://www.sysoul.com/about\n\n%s\n' % clean_generic(about))
for u, f in SYS[1:]:
    fp = os.path.join(ROOT, f)
    if os.path.exists(fp):
        parts.append('## 来源：%s\n\n%s\n' % (u, clean_generic(open(fp, encoding='utf-8', errors='replace').read())))
write('09_希秀泛在计算与Robonix具身智能操作系统', '杭州希秀泛在计算技术有限公司（希秀计算 / Sysoul）与 Robonix',
      '2026-09-17 抓取自 sysoul.com、robonix.ai、book.robonix.ai。', parts)

# ---------- 上游仓库 ----------
UP = r'D:\ds\xiuos\Ubiquitous'
parts = []
for rel in ['XiZi_IIoT_Macro/README.md', 'XiZi_AIoT_Micro/README.md', 'RT-Thread_Fusion_XiUOS/README.md',
            'Nuttx_Fusion_XiUOS/readme.md']:
    fp = os.path.join(UP, rel)
    if os.path.exists(fp):
        t = open(fp, encoding='utf-8', errors='replace').read()
        t = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', t).replace('&emsp;', '')
        parts.append('## 上游文件：Ubiquitous/%s\n\n%s\n' % (rel, t[:12000]))
boards = sorted(d for d in os.listdir(os.path.join(UP, 'XiZi_IIoT_Macro', 'board'))
                if os.path.isdir(os.path.join(UP, 'XiZi_IIoT_Macro', 'board', d)))
mboards = sorted(os.listdir(os.path.join(UP, 'XiZi_AIoT_Micro', 'services', 'boards')))
parts.insert(0, '## 仓库结构与板级清单（gitlink xuos/xiuos，HEAD 0af75ee，2026-02-05）\n\n'
             'Ubiquitous 目录下有四条系统线：XiZi_IIoT_Macro（宏内核）、XiZi_AIoT_Micro（微内核）、'
             'RT-Thread_Fusion_XiUOS（基于 RT-Thread 的系统层）、Nuttx_Fusion_XiUOS（基于 NuttX 的系统层）。'
             'APP_Framework 是共用的感联知控应用框架，Framework 下有 sensor（感）、connection（联）、'
             'knowing（知，含 TensorFlow Lite、NNoM、CMSIS-NN、KPU、图像处理、滤波、OTA）、'
             'control（控，含 PLC 协议与 IPC 协议）、security（mbedTLS 与加密）、transform_layer 等目录。\n\n'
             '宏内核 XiZi_IIoT_Macro 的 board 目录共 %d 个板级支持包：%s。\n\n'
             '微内核 XiZi_AIoT_Micro 的 services/boards 目录有：%s。\n' % (len(boards), '、'.join(boards), '、'.join(mboards)))
write('10_XiUOS上游仓库分支与板级清单', 'XiUOS 上游仓库：分支结构、README 与板级清单',
      '来源：本地克隆的 gitlink xuos/xiuos 仓库，HEAD 0af75ee（2026-02-05）。', parts)

print('xuos.io 未归组/空页:', len([u for u in xuos_urls if u not in used]))
