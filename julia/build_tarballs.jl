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
using BinaryBuilder, Pkg

name = "libhelfem"
version = v"0.0.1"
sources = [
    DirectorySource(abspath(joinpath(@__DIR__, ".."))),
    # # The ArchiveSource is replaced with a DirectorySource if a local clone of the HelFEM
    # # repository is used.
    # if haskey(ENV, "USE_LOCAL_HELFEM")
    #     @warn "Using local clone of HelFEM"
    #     @assert isdir(joinpath(@__DIR__, "HelFEM"))
    #     DirectorySource("./HelFEM", target="HelFEM")
    # else
    #     ArchiveSource(
    #         "https://github.com/mortenpi/HelFEM/archive/v$(HelFEM_version).tar.gz",
    #         "3f1da9f1ef5f20d4c8c31eed28598f6119e64497b2102110205c07a9e01a6a70"
    #     )
    # end,
]

script = raw"""
cp -v ${WORKSPACE}/srcdir/julia/CMake.system ${WORKSPACE}/srcdir/CMake.system

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
cd ${WORKSPACE}/srcdir
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
cd ${WORKSPACE}/srcdir/julia
cmake \
    -DCMAKE_INSTALL_PREFIX=$prefix -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TARGET_TOOLCHAIN} -DCMAKE_BUILD_TYPE=Release \
    -DBLAS_LIBRARIES=${OPENBLAS} \
    -DJulia_PREFIX=$prefix -DCMAKE_FIND_ROOT_PATH=$prefix -DJlCxx_DIR=$prefix/lib/cmake/JlCxx \
    -B build/ -S .
make -C build/ -j${nproc}
make -C build/ install
"""

# These are the platforms the libcxxwrap_julia_jll is built on.
pfkwarg = (; compiler_abi = CompilerABI(cxxstring_abi=:cxx11))
platforms = [
    # x86_64-linux-gnu-cxx11
    Linux(:x86_64; libc=:glibc, pfkwarg...),
    # i686-linux-gnu-cxx11
    Linux(:i686; libc=:glibc, pfkwarg...),
    # armv7l-linux-gnueabihf-cxx11
    Linux(:armv7l; libc=:glibc, pfkwarg...),
    # aarch64-linux-gnu-cxx11
    Linux(:aarch64; libc=:glibc, pfkwarg...),
    # x86_64-apple-darwin14-cxx11
    MacOS(:x86_64; pfkwarg...),
    # x86_64-w64-mingw32-cxx11
    Windows(:x86_64; pfkwarg...),
    # i686-w64-mingw32-cxx11
    Windows(:i686; pfkwarg...),
    # x86_64-unknown-freebsd11.1-cxx11
    FreeBSD(:x86_64; pfkwarg...),
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
