# Marcel

Marcel is a C++ project that aims to combine all these things:

- Clifford Algebra
- Dear ImGui and ImPlot
- Cling
- LLVM and Clang

This project is named after [Marcel Riesz](https://en.wikipedia.org/wiki/Marcel_Riesz), who came to Lund University as professor of Mathematics 1926, one hundred years ago at the time of writing.

The project name is pronounced roughly "'mortsel", which is the Hungarian pronunciation of "Marcel".

## Build

### Dependencies

```
sudo apt install libglfw3-dev libgtk-3-dev libfmt-dev libcppunit-dev
```

### Build Cling Dependency
```
cd external/root-project
mkdir cling-build && cd cling-build
cmake -DLLVM_EXTERNAL_PROJECTS=cling -DLLVM_EXTERNAL_CLING_SOURCE_DIR=../cling/ -DLLVM_ENABLE_PROJECTS="clang;lldb" -DLLVM_TARGETS_TO_BUILD="host;NVPTX;WebAssembly" -DCMAKE_BUILD_TYPE=RelWithDebInfo ../llvm-project/llvm
cmake --build . --target cling --target clang --target lldb-dap --target lldb-server -j $(nproc)
```

### Build this thing

...

### Use mold
Download a recent mold release from [GitHub](https://github.com/rui314/mold/releases).

Ensure you have a ~/.local/bin, otherwise create it and relog so it gets added to your PATH.

```
cd ~
tar --strip-components=1 -xvf ~/Downloads/mold-2.36.0-x86_64-linux.tar.gz -C .local
```

Ensure you use a CMake setup like the one in .vscode/cmake-kits.json.

### GOTCHAs

When building with clang++ from the external cling/clang build using the provided .vscode/cmake-kits.json, you may
encounter a problem where CMake fails to configure with an error message "/usr/bin/ld: cannot find -lstdc++".

Solution: sudo apt install libstdc++-12-dev
Reference: https://stackoverflow.com/questions/74543715/usr-bin-ld-cannot-find-lstdc-no-such-file-or-directory-on-running-flutte

## Acknowledgements

Thanks to [Jokteur](https://github.com/jokteur) for providing the initial ImGUI + MicroTex integration that got me started down the road to creating Marcel in his [QuickTex](https://github.com/jokteur/QuickTex) project.
