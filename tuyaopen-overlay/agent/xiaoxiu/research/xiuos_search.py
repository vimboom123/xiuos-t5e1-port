#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""XiUOS 资料检索：Tavily（tvly CLI）+ Brave（API，cn/zh-hans）+ Perplexity（sonar-pro）。
结果落到 D:\\ds\\tuya\\research\\，key 从本地 .env 读取，不打印。
"""
import json
import os
import re
import subprocess
import time
import urllib.parse
import urllib.request

OUT = r'D:\ds\tuya\research'
os.makedirs(OUT, exist_ok=True)
HOME = os.path.expanduser('~')


def env_value(path, key):
    for line in open(path, encoding='utf-8'):
        if line.startswith(key + '='):
            return line.split('=', 1)[1].strip().strip('"').strip("'")
    return None


BRAVE_KEY = env_value(os.path.join(HOME, '.claude', 'skills', 'brave-search', '.env'), 'BRAVE_API_KEY')
PPLX_KEY = env_value(os.path.join(HOME, '.claude', 'skills', 'obsidian-second-brain', '.env'), 'PERPLEXITY_API_KEY')
TVLY = os.path.join(HOME, '.local', 'bin', 'tvly')

QUERIES = [
    'XiUOS 矽璓 工业物联操作系统',
    '矽璓 XiZi 内核',
    'XiUOS 泛在操作系统 北京大学信息技术高等研究院',
    'XiUOS 感联知控 应用框架',
    'XiUOS 2.0 发布',
    'XiUOS 开发板 移植 教程',
    'XiUOS 全国大学生操作系统比赛',
    '泛在操作系统 梅宏 人机物融合',
    'xuos xiuos gitlink',
    '矽璓 工业互联网 应用案例',
    'XiZi_AIoT 微内核',
    'XiUOS ubiquitous operating system industrial IoT',
]

stats = {'tavily': 0, 'brave': 0, 'perplexity': 0}
urls = {}


def add_url(u, title, src, q):
    if not u:
        return
    rec = urls.setdefault(u, {'url': u, 'title': title, 'sources': set(), 'queries': set()})
    rec['sources'].add(src)
    rec['queries'].add(q)


# ---------- Tavily ----------
for i, q in enumerate(QUERIES):
    fn = os.path.join(OUT, 'tavily_%02d.json' % i)
    r = subprocess.run([TVLY, 'search', q, '--max-results', '10', '--depth', 'advanced',
                        '--country', 'china', '-o', fn],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    stats['tavily'] += 1
    try:
        j = json.load(open(fn, encoding='utf-8'))
        for it in j.get('results', []):
            add_url(it.get('url'), it.get('title'), 'tavily', q)
    except Exception as e:
        print('tavily fail', q, e, (r.stderr or '')[:200])

# ---------- Brave ----------
for i, q in enumerate(QUERIES):
    params = {'q': q, 'count': 20}
    if re.search(r'[\u4e00-\u9fff]', q):
        params.update({'country': 'cn', 'search_lang': 'zh-hans'})
    req = urllib.request.Request(
        'https://api.search.brave.com/res/v1/web/search?' + urllib.parse.urlencode(params),
        headers={'Accept': 'application/json', 'X-Subscription-Token': BRAVE_KEY})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            j = json.loads(resp.read().decode('utf-8'))
        json.dump(j, open(os.path.join(OUT, 'brave_%02d.json' % i), 'w', encoding='utf-8'), ensure_ascii=False)
        stats['brave'] += 1
        for it in (j.get('web') or {}).get('results', []):
            add_url(it.get('url'), it.get('title'), 'brave', q)
    except Exception as e:
        print('brave fail', q, e)
    time.sleep(1.1)

# ---------- Perplexity ----------
PQ = [
    '请全面介绍矽璓 XiUOS（X Industrial Ubiquitous Operating System）：研发团队与机构、发展时间线（各版本发布时间与主要特性）、'
    'XiZi 内核的宏内核与微内核两条线、感联知控应用框架、许可证与代码仓库地址。请给出处。',
    'XiUOS 支持哪些处理器架构和开发板？有哪些开发者生态活动（例如全国大学生操作系统比赛赛题、开源之夏、教程、书籍）？'
    '与 RT-Thread、NuttX 的融合版本是什么？请给出处。',
    'XiUOS 有哪些公开的工业落地案例与合作单位？2023 年以来有哪些新进展（新版本、新内核、AIoT、标准、奖项）？请给出处，拿不准的请注明。',
]
for i, q in enumerate(PQ):
    body = json.dumps({'model': 'sonar-pro', 'messages': [
        {'role': 'system', 'content': '你是严谨的技术调研助手，只陈述有出处的事实，用中文回答，不确定处明确标注。'},
        {'role': 'user', 'content': q}]}).encode('utf-8')
    req = urllib.request.Request('https://api.perplexity.ai/chat/completions', data=body,
                                 headers={'Content-Type': 'application/json',
                                          'Authorization': 'Bearer ' + PPLX_KEY})
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            j = json.loads(resp.read().decode('utf-8'))
        json.dump(j, open(os.path.join(OUT, 'pplx_%02d.json' % i), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        stats['perplexity'] += 1
        for u in j.get('citations', []) or []:
            add_url(u, '', 'perplexity', q[:30])
        for sr in j.get('search_results', []) or []:
            add_url(sr.get('url'), sr.get('title'), 'perplexity', q[:30])
    except Exception as e:
        print('pplx fail', i, e)

rows = []
for rec in urls.values():
    rec['sources'] = sorted(rec['sources'])
    rec['queries'] = sorted(rec['queries'])
    rows.append(rec)
json.dump(rows, open(os.path.join(OUT, 'urls.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('calls', stats, 'unique urls', len(rows))
