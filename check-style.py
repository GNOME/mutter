#!/bin/env python3

import argparse
import os
import re
import subprocess
import sys
import tempfile

# Path relative to this script
uncrustify_cfg = 'tools/uncrustify.cfg'

def run_diff(sha):
    proc = subprocess.run(
        ["git", "diff", "-U0", "--function-context", sha, "HEAD"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
    )
    return proc.stdout.strip().splitlines()

def find_chunks(diff):
    file_entry_re = re.compile(r'^\+\+\+ b/(.*)$')
    diff_chunk_re = re.compile(r'^@@ -\d+,\d+ \+(\d+),(\d+)')
    file = None
    chunks = []

    for line in diff:
        match = file_entry_re.match(line)
        if match:
            file = match.group(1)

        match = diff_chunk_re.match(line)
        if match:
            start = int(match.group(1))
            len = int(match.group(2))
            end = start + len

            if len > 0 and (file.endswith('.c') or file.endswith('.h') or file.endswith('.vala')):
                chunks.append({ 'file': file, 'start': start, 'end': end })

    return chunks

def reformat_chunks(chunks, rewrite):
    # Creates temp file with INDENT-ON/OFF comments
    def create_temp_file(file, start, end):
        with open(file) as f:
            tmp = tempfile.NamedTemporaryFile()
            if start > 1:
                tmp.write(b'/** *INDENT-OFF* **/\n')
            for i, line in enumerate(f, start=1):
                if i == start - 1:
                    tmp.write(b'/** *INDENT-ON* **/\n')

                tmp.write(bytes(line, 'utf-8'))

                if i == end - 1:
                    tmp.write(b'/** *INDENT-OFF* **/\n')

            tmp.seek(0)

        return tmp

    # Removes uncrustify INDENT-ON/OFF helper comments
    def remove_indent_comments(output):
        tmp = tempfile.NamedTemporaryFile()

        for line in output:
            if line != b'/** *INDENT-OFF* **/\n' and line != b'/** *INDENT-ON* **/\n':
                tmp.write(line)

        tmp.seek(0)

        return tmp

    changed = None

    for chunk in chunks:
        # Add INDENT-ON/OFF comments
        tmp = create_temp_file(chunk['file'], chunk['start'], chunk['end'])

        # uncrustify chunk
        proc = subprocess.run(
            ["uncrustify", "-c", uncrustify_cfg, "-f", tmp.name],
            stdout=subprocess.PIPE,
        )
        reindented = proc.stdout.splitlines(keepends=True)
        if proc.returncode != 0:
            continue

        tmp.close()

        # Remove INDENT-ON/OFF comments
        formatted = remove_indent_comments(reindented)

        if dry_run is True:
            # Show changes
            proc = subprocess.run(
                ["diff", "-up", "--color=always", chunk['file'], formatted.name],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                encoding="utf-8",
            )
            diff = proc.stdout
            if diff != '':
                output = re.sub('\t', '↦\t', diff)
                print(output)
                changed = True
        else:
            # Apply changes
            diff = subprocess.run(
                ["diff", "-up", chunk['file'], formatted.name],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            patch = subprocess.run(["patch", chunk['file']], input=diff.stdout)

        formatted.close()

    return changed


parser = argparse.ArgumentParser(description='Check code style. Needs uncrustify installed.')
parser.add_argument('--sha', metavar='SHA', type=str,
                    help='SHA for the commit to compare HEAD with')
parser.add_argument('--dry-run', '-d', type=bool,
                    action=argparse.BooleanOptionalAction,
                    help='Only print changes to stdout, do not change code')
parser.add_argument('--rewrite', '-r', type=bool,
                    action=argparse.BooleanOptionalAction,
                    help='Whether to amend the result to the last commit (e.g. \'git rebase --exec "%(prog)s -r"\')')

# Change CWD to script location, necessary for always locating the configuration file
os.chdir(os.path.dirname(os.path.abspath(sys.argv[0])))

args = parser.parse_args()
sha = args.sha or 'HEAD^'
rewrite = args.rewrite
dry_run = args.dry_run

diff = run_diff(sha)
chunks = find_chunks(diff)
changed = reformat_chunks(chunks, rewrite)

if dry_run is not True and rewrite is True:
    proc = subprocess.run(["git", "add", "-p"])
    if proc.returncode == 0:
        # Commit the added changes as a squash commit
        subprocess.run(
            ["git", "commit", "--squash", "HEAD", "-C", "HEAD"],
            stdout=subprocess.DEVNULL)
        # Delete the unapplied changes
        subprocess.run(["git", "reset", "--hard"], stdout=subprocess.DEVNULL)
    os._exit(0)
elif dry_run is True and changed is True:
    print(f"""
Issue the following commands in your local tree to apply the suggested changes:

    $ git rebase {sha} --exec "./check-style.py -r"
    $ git rebase --autosquash {sha}
""")
    os._exit(-1)

os._exit(0)
