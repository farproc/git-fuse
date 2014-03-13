
# Summary

git-fuse is a quick a dirty way of _mounting_ a git repo. All branches (and soon tags) are directories in the root of the mount directory and then all the files of the repo (as you'd expect) are under each directory. Its read-only, but a nice quick way of browsing repos - maybe automatted builds (could place a unionfs over the top).

It's currently dependant on osxfuse and libgit2.

# Quick Start

```
git clone https://github.com/farproc/git-fuse.git
cd git-fuse
make
./git-fuse <mount-point> <path-to-repo>
```

