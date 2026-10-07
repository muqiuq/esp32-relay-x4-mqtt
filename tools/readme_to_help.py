# Converts README.md into src/help_html.h (HTML for the web UI's /help page).
# Runs automatically before every build (platformio.ini: extra_scripts), or by hand:
#   python3 tools/readme_to_help.py
# Supports the Markdown subset used in the README: headings, paragraphs, tables,
# fenced code blocks, ordered/unordered lists, bold, italic, inline code, links.
import html
import os
import re

DELIM = "HELPHTML"


def slug(text):
    s = re.sub(r"<[^>]+>", "", text).lower()
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s or "section"


def inline(text):
    tokens = []

    def keep(snippet):
        tokens.append(snippet)
        return "\x00%d\x00" % (len(tokens) - 1)

    text = re.sub(r"`([^`]+)`", lambda m: keep("<code>" + html.escape(m.group(1)) + "</code>"), text)
    text = re.sub(
        r"<(https?://[^>\s]+)>",
        lambda m: keep('<a href="%s" target="_blank">%s</a>' % (html.escape(m.group(1)), html.escape(m.group(1)))),
        text,
    )
    text = re.sub(r"<([^<>\s@]+@[^<>\s]+)>", lambda m: keep('<a href="mailto:%s">%s</a>' % (m.group(1), m.group(1))), text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<i>\1</i>", text)
    return re.sub(r"\x00(\d+)\x00", lambda m: tokens[int(m.group(1))], text)


def table_cells(row):
    row = row.strip()
    if row.startswith("|"):
        row = row[1:]
    if row.endswith("|") and not row.endswith("\\|"):
        row = row[:-1]
    return [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", row)]


def convert(md):
    lines = md.split("\n")
    out, toc, para = [], [], []
    i = 0

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("```"):
            flush()
            i += 1
            code = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                code.append(lines[i])
                i += 1
            i += 1
            out.append("<pre>" + html.escape("\n".join(code)) + "</pre>")
            continue

        m = re.match(r"^(#{1,6})\s+(.*)$", line)
        if m:
            flush()
            level, text = len(m.group(1)), inline(m.group(2).strip())
            anchor = slug(text)
            if level == 2:
                toc.append((anchor, text))
            out.append('<h%d id="%s">%s</h%d>' % (level, anchor, text, level))
            i += 1
            continue

        if stripped.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(lines[i])
                i += 1
            out.append("<table>")
            for n, row in enumerate(rows):
                if n == 1 and re.match(r"^\|?[\s:|-]+\|?$", row.strip()):
                    continue  # separator row
                tag = "th" if n == 0 else "td"
                cells = "".join("<%s>%s</%s>" % (tag, inline(c), tag) for c in table_cells(row))
                out.append("<tr>%s</tr>" % cells)
            out.append("</table>")
            continue

        m = re.match(r"^([-*]|\d+\.)\s+(.*)$", line)
        if m:
            flush()
            ordered = m.group(1)[0].isdigit()
            items = []
            while i < len(lines):
                cur = lines[i]
                mm = re.match(r"^([-*]|\d+\.)\s+(.*)$", cur)
                if mm:
                    items.append(mm.group(2).strip())
                elif cur.startswith((" ", "\t")) and cur.strip() and items:
                    items[-1] += " " + cur.strip()  # continuation line
                else:
                    break
                i += 1
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % inline(it) for it in items), tag))
            continue

        if not stripped:
            flush()
        else:
            para.append(stripped)
        i += 1
    flush()

    nav = "".join('<a href="#%s">%s</a>' % (a, re.sub(r"<[^>]+>", "", t)) for a, t in toc)
    return '<nav class="toc">%s</nav>\n%s' % (nav, "\n".join(out))


def generate(project_dir):
    src = os.path.join(project_dir, "README.md")
    dst = os.path.join(project_dir, "src", "help_html.h")
    with open(src, encoding="utf-8") as f:
        body = convert(f.read())
    if (")" + DELIM + '"') in body:
        raise RuntimeError("README contains the raw string delimiter")
    header = (
        "// GENERATED from README.md by tools/readme_to_help.py - do not edit\n"
        "#pragma once\n"
        "#include <Arduino.h>\n\n"
        'static const char HELP_HTML[] PROGMEM = R"%s(\n%s\n)%s";\n' % (DELIM, body, DELIM)
    )
    old = None
    if os.path.exists(dst):
        with open(dst, encoding="utf-8") as f:
            old = f.read()
    if old != header:  # only touch the file when the README changed
        with open(dst, "w", encoding="utf-8") as f:
            f.write(header)
        print("readme_to_help: src/help_html.h updated")


try:
    Import("env")  # noqa: F821 - provided by PlatformIO/SCons
    PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:  # run by hand
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
generate(PROJECT_DIR)
