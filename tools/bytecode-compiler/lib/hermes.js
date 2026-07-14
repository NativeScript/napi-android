'use strict';

// Hermes (HBC) adapter. The only engine-specific logic is: which binary to run
// (hermesc), how to build its command line, and what its bytecode looks like.
//
// Bytecode magic: first 8 bytes (little-endian) of every HBC file.
// See hermes BytecodeFileFormat.h (MAGIC = 0x1F1903C103BC1FC6). Must match the
// runtime detection in napi/hermes/jsr.cpp (js_run_bytecode_file).
const MAGIC = Buffer.from([0xc6, 0x1f, 0xbc, 0x03, 0xc1, 0x03, 0x19, 0x1f]);

module.exports = {
  key: 'hermes',
  engineKeys: ['HERMES'],
  ready: true,
  magic: MAGIC,
  supportsSourceMaps: true,
  defaultOptimize: '-O',

  binName(hostKey) {
    return hostKey.startsWith('win32') ? 'hermesc.exe' : 'hermesc';
  },

  isBytecode(head) {
    return head.length >= MAGIC.length && head.subarray(0, MAGIC.length).equals(MAGIC);
  },

  buildArgs({ input, output, sourceMap, optimize }) {
    const args = [];
    if (optimize) args.push(optimize);
    args.push('-emit-binary');
    if (sourceMap) args.push('-output-source-map');
    args.push('-out', output, input);
    return args;
  },

  // hermesc writes the source map to "<out>.map".
  sourceMapOutput(output) {
    return output + '.map';
  },
};
