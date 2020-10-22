#=!YGG
# Modes:
#
# * julia --project build_tarballs.jl --verbose --devdir=dev --deploy=local
#   Non-interactive local build. Can also add --debug to drop into the debug shell on
#   failure.
#
# * julia --project --color=yes build_tarballs.jl --verbose --deploy="mortenpi/libhelfem_jll.jl"
#   Deploy to GitHub. This requires ENV["GITHUB_TOKEN"] to be set up for authentication.
#
# * julia --project -i build_tarballs.jl
#   Interactive mode, call build_helfem() to run the full local build.
#
# * julia --project -i -e'using Revise; includet("build_tarballs.jl")'
#   Interactive mode, with Revise also called on the main file.
#
# !YGG=#
using BinaryBuilder, Pkg

name = "HelFEM"
version = v"0.0.1"
sources = [
    #=!YGG: use the checked out directory as the HelFEM source =#
    DirectorySource(abspath(joinpath(@__DIR__, "..")), target="HelFEM"),
    #=YGG: In the Yggdrasil script, this should be replaced with:
    GitSource("https://github.com/mortenpi/HelFEM.git", "<commit sha>")
    YGG=#
]

script = raw"""
cp -v ${WORKSPACE}/srcdir/HelFEM/julia/CMake.system ${WORKSPACE}/srcdir/HelFEM/CMake.system

# Set up some platform specific CMake configuration. This is more or less borrowed from
# the Armadillo build_tarballs.jl script:
#   https://github.com/JuliaPackaging/Yggdrasil/blob/48d7a89b4aa46b1a8c91269bb138a660f4ee4ece/A/armadillo/build_tarballs.jl#L23-L52
#
# We need to manually set up OpenBLAS because FindOpenBLAS.cmake does not work with BB:
if [[ "${nbits}" == 64 ]] && [[ "${target}" != aarch64* ]]; then
    OPENBLAS="${libdir}/libopenblas64_.${dlext}"
else
    OPENBLAS="${libdir}/libopenblas.${dlext}"
fi

# Compile libhelfem as a static library
cd ${WORKSPACE}/srcdir/HelFEM/
cmake \
    -DCMAKE_INSTALL_PREFIX=$prefix -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TARGET_TOOLCHAIN} -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DUSE_OPENMP=OFF \
    -DHELFEM_BINARIES=OFF -DHELFEM_FIND_DEPS=ON \
    -B build/ -S .
make -C build/ -j${nproc} helfem
make -C build/ install
# Copy the HelFEM license
install_license LICENSE

# Compile the CxxWrap wrapper as a shared library
cd ${WORKSPACE}/srcdir/HelFEM/julia
cmake \
    -DCMAKE_INSTALL_PREFIX=$prefix -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TARGET_TOOLCHAIN} -DCMAKE_BUILD_TYPE=Release \
    -DBLAS_LIBRARIES=${OPENBLAS} \
    -DJulia_PREFIX=$prefix -DCMAKE_FIND_ROOT_PATH=$prefix -DJlCxx_DIR=$prefix/lib/cmake/JlCxx \
    -B build/ -S .
make -C build/ -j${nproc}
make -C build/ install
"""

# These are the platforms the libcxxwrap_julia_jll is built on.
pfkwarg = (; cxxstring_abi = "cxx11")
platforms = [
    # x86_64-linux-gnu-cxx11
    Platform("x86_64", "linux"; libc="glibc", pfkwarg...),
    # i686-linux-gnu-cxx11
    Platform("i686", "linux"; libc="glibc", pfkwarg...),
    # armv7l-linux-gnueabihf-cxx11
    Platform("armv7l", "linux"; libc="glibc", pfkwarg...),
    # aarch64-linux-gnu-cxx11
    Platform("aarch64", "linux"; libc="glibc", pfkwarg...),
    # x86_64-apple-darwin14-cxx11
    Platform("x86_64", "macos"; pfkwarg...),
    # x86_64-w64-mingw32-cxx11
    Platform("x86_64", "windows"; pfkwarg...),
    # i686-w64-mingw32-cxx11
    Platform("i686", "windows"; pfkwarg...),
    # x86_64-unknown-freebsd11.1-cxx11
    Platform("x86_64", "freebsd"; pfkwarg...),
]

products = [
    LibraryProduct("libhelfem-cxxwrap", :libhelfem),
]

dependencies = [
    BuildDependency(PackageSpec(name = "Julia_jll",version = "1.4.1")),
    Dependency(PackageSpec(name = "libcxxwrap_julia_jll", version = "0.8.0")),
    Dependency(PackageSpec(name = "armadillo_jll", version = "9.850.1")),
    Dependency(PackageSpec(name = "GSL_jll", version = "2.6.0")),
    Dependency(PackageSpec(name = "OpenBLAS_jll", version = "0.3.9")),
]

#=!YGG=#
build_helfem(args) = mktempdir() do path
    @info "Building in $path"
    cd(path) do
        build_tarballs(
            args, name, version, sources, script, platforms, products, dependencies,
            preferred_gcc_version = v"7.1.0",
        )
    end
end
build_helfem() = build_helfem(split("--verbose --deploy=local x86_64-linux-gnu-cxx11"))
if isinteractive()
    @info "Running is interactive mode (-i passed). Skipping build_tarballs(), run build_helfem()"
else
    build_helfem(ARGS)
end
#=YGG
build_tarballs(
    ARGS, name, version, sources, script, platforms, products, dependencies,
    preferred_gcc_version = v"7.1.0",
)
YGG=#
