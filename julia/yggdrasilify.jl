using LibGit2

let
    head = LibGit2.head(joinpath(@__DIR__, "..", ".git"))
    @info "Using Git hash: $head"
    lines = readlines(joinpath(@__DIR__, "build_tarballs.jl"))
    mode = :plain
    ygg_lines = filter(lines) do line
        @debug "In mode: $mode" line
        if mode == :yggonly
            if occursin(r"^\s*#=!YGG", line)
                mode = :noygg
                return false
            elseif occursin(r"^\s*YGG=#", line)
                mode = :plain
                return false
            end
        elseif mode == :noygg
            if occursin(r"^\s*#=YGG", line)
                mode = :yggonly
                return false
            elseif occursin(r"^.*!YGG=#$", line)
                mode = :plain
            end
            return false
        else
            if occursin(r"^\s*#=YGG", line)
                mode = :yggonly
                return false
            elseif occursin(r"^\s*#=!YGG", line)
                mode = :noygg
                return false
            end
        end
        return true
    end
    open(joinpath(@__DIR__, "build_tarballs.jl.ygg"), "w") do io
        for line in ygg_lines
            if occursin("<commit sha>", line)
                line = replace(line, "<commit sha>" => head)
            end
            write(io, line, '\n')
        end
    end
end
