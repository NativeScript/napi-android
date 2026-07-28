'use strict';

// QuickJS (bellard) adapter.
//
// The compiler is our blob-emitting shim (native/qjs-compile.c), built per host
// by .github/workflows/bytecode-compilers.yml. It writes a NativeScript bytecode
// container: 8-byte magic + 4-byte LE format version + JS_WriteObject payload.
//
// The runtime side reads this container in napi/quickjs js_execute_script (strips
// the 12-byte header, JS_ReadObject(JS_READ_OBJ_BYTECODE) + JS_EvalFunction).
// Keep the CLI's engine ref in step with the runtime's vendored QuickJS.
const MAGIC = Buffer.from('NSBCQJS\0', 'latin1'); // 8 bytes, must match the shim + runtime

module.exports = {
  key: 'quickjs',
  engineKeys: ['QUICKJS'],
  ready: true,
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
