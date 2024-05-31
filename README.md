# Bake
Bake is a C build system with support for compiling both applications and libraries, as well as supporting external dependieces and other goodies.

# NOTE
you need to create the binary output directory as specified in `bake.toml`, or else `bake` will not work
> this is temporary due to my horrendous code

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
Examples can be found in the `bake-example-proj` and `bake-hello-world` dirs. Also, this is the Bakefile that builds `bake` itself:
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
