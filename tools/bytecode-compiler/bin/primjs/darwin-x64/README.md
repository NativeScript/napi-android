# Placeholder — primjs compiler for host `darwin-x64`

Expected binary: `nsbc-primjs`

Produced by `.github/workflows/bytecode-compilers.yml` (artifact
`bytecode-compiler-primjs-darwin-x64`). Drop the built binary here, keeping this name.
The driver treats a slot with no real (>1 KB) executable as "no compiler for this
host" and leaves the app as plain JS source.
