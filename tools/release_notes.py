#!/usr/bin/env python3
"""What's new since the previous release, as short Markdown grouped by kind.

    python3 tools/release_notes.py --repo ArunasVell/Vellum-OS > release-notes.md

The changes are the pull requests merged into the current commit since the
newest release tag (v*) behind it, found in the git history (CI checks out
the whole history with tags). Their titles and labels come from GitHub
through the gh command; no author names are written. Each one lands in a
group by its labels (bug, enhancement, documentation), else by its title:

    ### New features

    * Show what's new before and after an update (#60)

    ### Bug fixes

    * Fix ... (#61)

The reader shows the same text on its "What's new" screen (the firmware
embeds it) and the release page lists it. When anything is missing (no tag,
no gh, no network) it prints nothing, so the build still goes on.
"""

import argparse
import json
import re
import subprocess
import sys

GROUPS = ("New features", "Bug fixes", "Other changes")
FEATURES, FIXES, OTHER = range(3)

FIX_WORDS = re.compile(r"\b(fix|fixes|fixed|bug|bugs|crash|crashes|broken|repair)\b", re.I)
OTHER_WORDS = re.compile(
    r"\b(readme|docs?|documentation|guide|changelog|ci|workflow|releases?|release notes|notice|credits)\b", re.I)


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def merged_pull_requests(head):
    """PR numbers merged into `head` since the previous release tag, oldest first."""
    try:
        since = run("git", "describe", "--tags", "--abbrev=0", "--match", "v*", f"{head}^").strip()
        log = run("git", "log", "--first-parent", "--reverse", "--format=%s", f"{since}..{head}", "--")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    numbers = []
    for subject in log.splitlines():
        m = re.match(r"Merge pull request #(\d+) ", subject) or re.search(r"\(#(\d+)\)$", subject)
        if m and int(m.group(1)) not in numbers:
            numbers.append(int(m.group(1)))
    return numbers


def group_of(title, labels):
    labels = {name.lower() for name in labels}
    if labels & {"bug", "fix"}:
        return FIXES
    if labels & {"documentation", "docs"}:
        return OTHER
    if labels & {"enhancement", "feature"}:
        return FEATURES
    if OTHER_WORDS.search(title):
        return OTHER
    if FIX_WORDS.search(title):
        return FIXES
    return FEATURES


def notes(pulls):
    """Markdown for [(number, title, labels)], or "" when there are none."""
    groups = [[], [], []]
    for number, title, labels in pulls:
        groups[group_of(title, labels)].append(f"* {title.strip()} (#{number})")
    parts = [f"### {GROUPS[i]}\n\n" + "\n".join(items) for i, items in enumerate(groups) if items]
    return "\n\n".join(parts) + "\n" if parts else ""


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--repo", required=True, help="owner/name on GitHub")
    parser.add_argument("--head", default="HEAD", help="the commit being released")
    args = parser.parse_args()
    pulls = []
    for number in merged_pull_requests(args.head):
        try:
            pr = json.loads(run("gh", "api", f"repos/{args.repo}/pulls/{number}"))
        except (subprocess.CalledProcessError, FileNotFoundError, json.JSONDecodeError) as e:
            print(f"release_notes: #{number}: {e}", file=sys.stderr)
            continue
        pulls.append((number, pr.get("title", ""), [label.get("name", "") for label in pr.get("labels", [])]))
    sys.stdout.write(notes(pulls))


if __name__ == "__main__":
    main()
