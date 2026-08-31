# Pronk downstream integration

The `pronk` branch is the Mutter integration tree for the Pronk network-display
stack. It is not an upstream GNOME release branch. The supported Fedora 45
assembly is pinned by the
[Pronk packaging manifest](https://github.com/pronkproject/packaging/blob/main/compatibility.toml).

The packaged patch series is exported from upstream commit
`2dd1dfa674f96fc649535a291c6404710451c5e6` through the commit pinned in that
manifest. The early commits are general native-rendering and monitor-name
fixes; the remaining commits implement the experimental CastKMS grant broker
and its tests. No upstream merge request is currently recorded for these
series, so their presence here must not be read as upstream acceptance.

Packaging generates the series directly from this history:

```sh
git clone --recurse-submodules https://github.com/pronkproject/packaging.git
cd packaging
scripts/make-srpm mutter ./srpms
```

Report bugs that reproduce in unmodified Mutter to the
[GNOME issue tracker](https://gitlab.gnome.org/GNOME/mutter/-/issues). Report
Pronk-, CastKMS-, or casting-specific failures to the
[Pronk issue tracker](https://github.com/pronkproject/pronk/issues).
