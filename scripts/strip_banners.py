import re, os

test_files = [
    "datagrid/tests",
    "datagrid/examples/databrowser",
]


# glob current directory and subdirectories for .cpp files





# Three-line banner: // ====... / //  Title / // ====...
banner_block = re.compile(
    r'// ={5,}[^\n]*\n'
    r'//\s+[A-Za-z][^\n]*\n'
    r'// ={5,}[^\n]*\n'
)

# Single // ──── style header lines
dash_banner = re.compile(r'^// \u2500{4,}[^\n]*\n', re.MULTILINE)

for path in test_files:
    with open(path) as f:
        src = f.read()
    cleaned = banner_block.sub('', src)
    cleaned = dash_banner.sub('', cleaned)
    # Collapse 3+ blank lines to 2
    cleaned = re.sub(r'\n{3,}', '\n\n', cleaned)
    if cleaned != src:
        with open(path, 'w') as f:
            f.write(cleaned)
        print("cleaned:", os.path.basename(path))
    else:
        print("no change:", os.path.basename(path))

