# Git conventions

## Commit Messages

Commit messages should follow the guidelines in the [GNOME
handbook](https://handbook.gnome.org/development/commit-messages.html). We require an URL
to either an issue or a merge request in each commit. Try to always prefix
commit subjects with a relevant topic, such as `compositor:` or
`clutter/actor:`, and it's always better to write too much in the commit
message body than too little.

If a commit fixes an issue and that issue should be closed, add URL to it in
the bottom of the commit message and prefix with `Closes:`.

Do not add any `Part-of:` line, as that will be handled automatically when
merging.

### The Fixes tag

If a commit fixes a regression caused by a particular commit, it can be marked
with the `Fixes:` tag. To produce such a tag, use

```
git show -s --pretty='format:Fixes: %h ("%s")' <COMMIT>
```

or create an alias

```
git config --global alias.fixes "show -s --pretty='format:Fixes: %h (\"%s\")'"
```

and then use

```
git fixes <COMMIT>
```

### Example

```
compositor: Also consider dark matter when calculating paint volume

Ignoring dark matter when calculating the paint volume missed the case where
compositing happens in complete vacuum.

Fixes: 123abc123ab ("compositor: Calculate paint volume ourselves")
Closes: https://gitlab.gnome.org/GNOME/mutter/-/issues/1234
```
