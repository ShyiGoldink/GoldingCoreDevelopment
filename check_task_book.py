#!/usr/bin/env python3
"""任务书体检：把 `docs/TASK-SDK-JSON.md` §1 的写法约定变成可跑的检查。

用法：

    python check_task_book.py docs/TASK-SDK-JSON.md

退出码 = 错误条数（0 = 通过）。查三件事（对应 §1.1–§1.3）：

1. 每个 `### T-xx` 段落都得有一行状态行（`- T-xx … — 已完成 / 已放弃 / 已拆分为 … /
   状态：阻塞`）。
2. **完成即压缩**：状态是 已完成 / 已放弃 / 已拆分 的段落里，不许还留着
   「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险与回退」这五个细目——
   它们在 git 历史里，不留在文件里。
3. **两处必须同改**：§6.2 进度表里的状态要和段落里的状态一致
   （已完成 ↔ 已完成，未开始 ↔ 未开始，阻塞 ↔ 阻塞）。

不查内容对不对（那要靠 ctest 与断言），只查"这本子自己有没有写坏"。
"""
import re
import sys

DETAILS = ('准备怎么改', '大致改动逻辑', '与改动前对比', '验收', '风险与回退')
DONE_STATES = ('已完成', '已放弃', '已拆分')


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 1

    with open(argv[1], encoding='utf-8') as handle:
        lines = handle.read().split('\n')

    errors = []

    # ---- 切段：`### T-xx` 开头，到下一个 `### ` / `## ` 之前 ----
    sections = []
    for index, line in enumerate(lines):
        if line.startswith('### T-'):
            start = index
            end = len(lines)
            for later in range(index + 1, len(lines)):
                if lines[later].startswith('### ') or lines[later].startswith('## '):
                    end = later
                    break
            sections.append((line[4:].strip(), start, lines[start:end]))

    if not sections:
        errors.append('没找到任何 `### T-xx` 段落')

    section_state = {}
    for title, start, body in sections:
        tag = title.split()[0]

        state = None
        marked = re.compile(r'\*\*状态\*\*[：:]\s*(\S+)')
        for line in body:
            stripped = line.strip()
            if not stripped.startswith('- '):
                continue
            # 两种写法都收：压缩后的 `- T-xx <标题> — 已完成（…）`，
            # 与未开始 / 阻塞时的 `- **状态**：未开始`。
            if stripped.startswith('- ' + tag):
                for candidate in DONE_STATES:
                    if candidate in stripped:
                        state = candidate
                        break
                found = marked.search(stripped)
                if found:
                    state = found.group(1)
                if state is not None:
                    break
            elif marked.search(stripped):
                state = marked.search(stripped).group(1)
                break

        if state is None:
            errors.append('%s：没有可识别的状态行（要写成 `- %s <标题> — 已完成（…）`）' % (tag, tag))
            continue

        section_state[tag] = state

        if state in DONE_STATES:
            for offset, line in enumerate(body):
                for detail in DETAILS:
                    if line.startswith('**' + detail):
                        errors.append('%s：状态是「%s」，但第 %d 行还留着细目「%s」（§1.2：完成即压缩）'
                                      % (tag, state, start + offset + 1, detail))

    # ---- §6.2 进度表 ----
    progress = {}
    in_table = False
    for line in lines:
        if line.startswith('### 6.2') or '进度表' in line:
            in_table = True
            continue
        if in_table and line.startswith('## '):
            in_table = False
        if in_table and line.startswith('| T-'):
            cells = [cell.strip() for cell in line.strip('|').split('|')]
            if len(cells) >= 4:
                progress[cells[0]] = cells[3]

    for tag, state in sorted(section_state.items()):
        if tag not in progress:
            errors.append('%s：§6.2 进度表里没有这一行' % tag)
            continue
        table_state = progress[tag]
        same = (table_state == state) or (state == '阻塞' and '阻塞' in table_state) or \
               (state in DONE_STATES and table_state in DONE_STATES and table_state == state)
        if not same:
            errors.append('%s：段落状态「%s」与 §6.2 表格「%s」不一致（§1.2：两处必须同改）'
                          % (tag, state, table_state))

    if errors:
        print('== %d 处错误 ==' % len(errors))
        for item in errors:
            print(' - ' + item)
        return len(errors)

    print('OK：%d 个任务，段落与 §6.2 进度表一致，完成的任务已压缩' % len(sections))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
