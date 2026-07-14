# Placeholder — primjs compiler for host `linux-arm64`

Expected binary: `nsbc-primjs`

Produced by `.github/workflows/bytecode-compilers.yml` (artifact
`bytecode-compiler-primjs-linux-arm64`). Drop the built binary here, keeping this name.
The driver treats a slot with no real (>1 KB) executable as "no compiler for this
host" and leaves the app as plain JS source.
