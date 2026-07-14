'use strict';

// QuickJS (bellard) adapter.
//
// The compiler is our blob-emitting shim (native/qjs-compile.c), built per host
// by .github/workflows/bytecode-compilers.yml. It writes a NativeScript bytecode
// container: 8-byte magic + 4-byte LE format version + JS_WriteObject payload.
//
// Still gated OFF (`ready: false`): the runtime side, napi/quickjs
// `js_run_bytecode_file`, is a stub. To enable: implement it (strip the 12-byte
// header, JS_ReadObject(JS_READ_OBJ_BYTECODE) + JS_EvalFunction) so it matches
// this `magic`, confirm the CLI's engine ref matches the runtime's vendored
// QuickJS, then flip `ready` to true.
const MAGIC = Buffer.from('NSBCQJS\0', 'latin1'); // 8 bytes, must match the shim + runtime

module.exports = {
  key: 'quickjs',
  engineKeys: ['QUICKJS'],
  ready: false,
  magic: MAGIC,
  supportsSourceMaps: false,
  defaultOptimize: null,

  binName(hostKey) {
    return hostKey.startsWith('win32') ? 'nsbc-quickjs.exe' : 'nsbc-quickjs';
  },

  isBytecode(head) {
    return head.length >= MAGIC.length && head.subarray(0, MAGIC.length).equals(MAGIC);
  },

  // Our shim takes: <input.js> <output.bc>
  buildArgs({ input, output }) {
    return [input, output];
  },

  sourceMapOutput(output) {
    return output + '.map';
  },
};
