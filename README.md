# Bake
C build system. I made `bake` as a challenge initially and it turned out to be better than I expected it to be. `bake` is quite nice.

# NOTE
you need to create the binary output directory as specified in `bake.toml`, or else `bake` will not work

## Building bake
```sh
$ # Get the required dependieces for bake
$ gh repo clone cktan/tomlc99
$ cd tomlc99
$ make
$ cd ..
$ # compile bake now
$ make
$ ./bake # optional, bake can compile itself
```

## Examples
This is the Bakefile that builds `bake` itself:
```toml
[config]
cc = "clang"
ld = "clang"
as = "clang"

[project]
sub = [["bake", "bake"]]
ext = [["tomlc99", "libtoml"]]

[project.bake]
srcs = "."
bin = "."
ccflags = ["-std=c23", "-g"]
incflags = ["-I.", "-Itomlc99/"]
ldflags = ["-Ltomlc99", "-ltoml"]
type = "exec"
binname = "bake"
deps = []

[ext.libtoml]
loc = "tomlc99"
chdir = "."
buildcmd = ["make"]
```
